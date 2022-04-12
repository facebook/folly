/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/experimental/channels/Channel-fwd.h>
#include <folly/experimental/channels/detail/ChannelBridge.h>

namespace folly {
namespace channels {

/*
 * A channel is a sender and receiver pair that allows one component to send
 * values to another. A sender and receiver pair is similar to an AsyncPipe and
 * AsyncGenerator pair. However, unlike AsyncPipe/AsyncGenerator, senders and
 * receivers can be used by memory-efficient higher level transformation
 * abstractions.
 *
 * Typical usage:
 *   auto [receiver, sender] = Channel<T>::create();
 *   sender.write(val1);
 *   auto val2 = co_await receiver.next();
 */
template <typename TValue>
class Channel {
 public:
  /**
   * Creates a new channel with a sender/receiver pair. The channel will be
   * closed if the sender is destroyed, and will be cancelled if the receiver is
   * destroyed.
   */
  static std::pair<Receiver<TValue>, Sender<TValue>> create() {
    auto senderBridge = detail::ChannelBridge<TValue>::create();
    auto receiverBridge = senderBridge->copy();
    return std::make_pair(
        Receiver<TValue>(std::move(receiverBridge)),
        Sender<TValue>(std::move(senderBridge)));
  }
};

/**
 * A sender sends values to be consumed by a receiver.
 */
template <typename TValue>
class Sender {
 public:
  friend Channel<TValue>;
  using ValueType = TValue;

  Sender(Sender&& other) noexcept : bridge_(std::move(other.bridge_)) {}

  Sender& operator=(Sender&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (bridge_) {
      std::move(*this).close();
    }
    bridge_ = std::move(other.bridge_);
    return *this;
  }

  ~Sender() {
    if (bridge_) {
      std::move(*this).close();
    }
  }

  /**
   * Returns whether or not this sender instance is valid. This will return
   * false if the sender was closed or moved away.
   */
  explicit operator bool() const { return bridge_ != nullptr; }

  /**
   * Writes a value into the pipe.
   */
  template <typename U = TValue>
  void write(U&& element) {
    if (!bridge_->isSenderClosed()) {
      bridge_->senderPush(std::forward<U>(element));
    }
  }

  /**
   * Closes the pipe without an exception.
   */
  void close() && {
    if (!bridge_->isSenderClosed()) {
      bridge_->senderClose();
    }
    bridge_ = nullptr;
  }

  /**
   * Closes the pipe with an exception.
   */
  void close(folly::exception_wrapper exception) && {
    if (!bridge_->isSenderClosed()) {
      bridge_->senderClose(std::move(exception));
    }
    bridge_ = nullptr;
  }

  /**
   * Returns whether or not the corresponding receiver has been cancelled or
   * destroyed.
   */
  bool isReceiverCancelled() {
    if (bridge_->isSenderClosed()) {
      return true;
    }
    auto values = bridge_->senderGetValues();
    if (!values.empty()) {
      bridge_->senderClose();
      return true;
    }
    return false;
  }

 private:
  friend detail::ChannelBridgePtr<TValue>& detail::senderGetBridge<>(
      Sender<TValue>&);

  explicit Sender(detail::ChannelBridgePtr<TValue> bridge)
      : bridge_(std::move(bridge)) {}

  detail::ChannelBridgePtr<TValue> bridge_;
};

/**
 * A receiver that receives values sent by a sender. There are several ways that
 * a receiver can be consumed:
 *
 * 1. Call co_await receiver.next() to get the next value. See the docstring of
 *    next() for more details. This is the easiest way to consume the values
 *    from a receiver, but it is also the most expensive memory-wise (as it
 *    creates a long-lived coroutine frame). This is typically used in scenarios
 *    where O(1) channels are being consumed (and therefore coroutine memory
 *    overhead is negligible).
 *
 * 2. Call consumeChannelWithCallback to get a callback when each value comes
 *    in. See ConsumeChannel.h for more details. This uses less memory than
 *    #1, as it only needs to allocate coroutine frames when processing values
 *    (rather than always having such frames allocated when waiting for values).
 *
 * 3. Use MergeChannel in folly/experimental/channels/MergeChannel.h.
 *    This construct allows you to consume the merged output of a dynamically
 *    changing set of receivers. This is the cheapest way to consume the output
 *    of a large number of receivers. It is useful when the consumer wants to
 *    process all values from all receivers sequentially.
 *
 * 4. Use ChannelProcessor in folly/experimental/channels/ChannelProcessor.h.
 *    This construct allows you to consume a dynamically changing set of
 *    receivers in parallel.
 *
 * 5. A receiver may also be passed to other framework primitives that consume
 *    the receiver (such as transform). As with options 2-4, these primitives
 *    do not require coroutine frames to be allocated when waiting for values.
 */
template <typename TValue>
class Receiver {
  class Waiter;
  struct NextSemiAwaitable;

 public:
  friend Channel<TValue>;
  using ValueType = TValue;

  Receiver() {}

  Receiver(Receiver&& other) noexcept
      : bridge_(std::move(other.bridge_)), buffer_(std::move(other.buffer_)) {}

  Receiver& operator=(Receiver&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (bridge_ != nullptr) {
      std::move(*this).cancel();
    }
    bridge_ = std::move(other.bridge_);
    buffer_ = std::move(other.buffer_);
    return *this;
  }

  ~Receiver() {
    if (bridge_ != nullptr) {
      std::move(*this).cancel();
    }
  }

  /**
   * Returns whether or not this receiver instance is valid. This will return
   * false if the receiver was cancelled or moved away.
   */
  explicit operator bool() const { return bridge_ != nullptr; }

  /**
   * Returns the next value sent by a sender. The behavior similar to the
   * behavior of next() on folly::coro::AsyncGenerator<TValue>.
   *
   * When closeOnCancel is true, if the returned semi-awaitable is cancelled,
   * the underlying channel will be closed. No more values will be received,
   * even if they were sent by the sender. This matches the behavior of
   * folly::coro::AsyncGenerator.
   *
   * When closeOnCancel is false, cancelling the returned semi-awaitable will
   * not close the underlying channel. Instead, it will just cancel the next()
   * operation. This means that the caller can call next() again and continue
   * to receive values sent by the sender.
   *
   * If consumed directly with co_await, next() will return an std::optional:
   *
   *    std::optional<TValue> value = co_await receiver.next();
   *
   *    - If a value is sent, the std::optional will contain the value.
   *    - If the channel is closed by the sender with no exception, the optional
   *        will be empty.
   *    - If the channel is closed by the sender with an exception, next() will
   *        throw the exception.
   *    - If the next() call was cancelled, next() will throw an exception of
   *        type folly::OperationCancelled.
   *
   * If consumed with folly::coro::co_awaitTry, this will return a folly::Try:
   *
   *    folly::Try<TValue> value = co_await folly::coro::co_awaitTry(
   *        receiver.next());
   *
   *    - If a value is sent, the folly::Try will contain the value.
   *    - If the channel is closed by the sender with no exception, the try will
   *        be empty (with no value or exception).
   *    - If the channel is closed by the sender with an exception, the try will
   *        contain the exception.
   *    - If the next() call was cancelled, the try will contain an exception of
   *        type folly::OperationCancelled.
   */
  NextSemiAwaitable next(bool closeOnCancel = true) {
    return NextSemiAwaitable(*this ? this : nullptr, closeOnCancel);
  }

  /**
   * Cancels this receiver. If the receiver is currently being consumed, the
   * consumer will receive a folly::OperationCancelled exception.
   */
  void cancel() && {
    bridge_->receiverCancel();
    bridge_ = nullptr;
    buffer_.clear();
  }

 private:
  explicit Receiver(detail::ChannelBridgePtr<TValue> bridge)
      : bridge_(std::move(bridge)) {}

  friend bool detail::receiverWait<>(
      Receiver<TValue>&, detail::IChannelCallback*);

  friend detail::IChannelCallback* detail::cancelReceiverWait<>(
      Receiver<TValue>&);

  friend std::optional<folly::Try<TValue>> detail::receiverGetValue<>(
      Receiver<TValue>&);

  friend std::
      pair<detail::ChannelBridgePtr<TValue>, detail::ReceiverQueue<TValue>>
      detail::receiverUnbuffer<>(Receiver<TValue>&& receiver);

  detail::ChannelBridgePtr<TValue> bridge_;
  detail::ReceiverQueue<TValue> buffer_;
};
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/Channel-inl.h>
