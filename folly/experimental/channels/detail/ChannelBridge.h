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

#include <folly/Try.h>
#include <folly/experimental/channels/detail/AtomicQueue.h>

namespace folly {
namespace channels {
namespace detail {

class ChannelBridgeBase {};

class IChannelCallback {
 public:
  virtual ~IChannelCallback() = default;

  virtual void consume(ChannelBridgeBase* bridge) = 0;

  virtual void canceled(ChannelBridgeBase* bridge) = 0;
};

using SenderQueue = typename folly::channels::detail::Queue<folly::Unit>;

template <typename TValue>
using ReceiverQueue =
    typename folly::channels::detail::Queue<folly::Try<TValue>>;

template <typename TValue>
class ChannelBridge : public ChannelBridgeBase {
 public:
  struct Deleter {
    void operator()(ChannelBridge<TValue>* ptr) { ptr->decref(); }
  };
  using Ptr = std::unique_ptr<ChannelBridge<TValue>, Deleter>;

  static Ptr create() { return Ptr(new ChannelBridge<TValue>()); }

  Ptr copy() {
    auto refCount = refCount_.fetch_add(1, std::memory_order_relaxed);
    DCHECK(refCount > 0);
    return Ptr(this);
  }

  // These should only be called from the sender thread

  template <typename U = TValue>
  void senderPush(U&& value) {
    receiverQueue_.push(
        folly::Try<TValue>(std::forward<U>(value)),
        static_cast<ChannelBridgeBase*>(this));
  }

  bool senderWait(IChannelCallback* callback) {
    return senderQueue_.wait(callback, static_cast<ChannelBridgeBase*>(this));
  }

  IChannelCallback* cancelSenderWait() { return senderQueue_.cancelCallback(); }

  void senderClose() {
    if (!isSenderClosed()) {
      receiverQueue_.push(
          folly::Try<TValue>(), static_cast<ChannelBridgeBase*>(this));
      senderQueue_.close(static_cast<ChannelBridgeBase*>(this));
    }
  }

  void senderClose(folly::exception_wrapper ex) {
    if (!isSenderClosed()) {
      receiverQueue_.push(
          folly::Try<TValue>(std::move(ex)),
          static_cast<ChannelBridgeBase*>(this));
      senderQueue_.close(static_cast<ChannelBridgeBase*>(this));
    }
  }

  bool isSenderClosed() { return senderQueue_.isClosed(); }

  SenderQueue senderGetValues() {
    return senderQueue_.getMessages(static_cast<ChannelBridgeBase*>(this));
  }

  // These should only be called from the receiver thread

  void receiverCancel() {
    if (!isReceiverCancelled()) {
      senderQueue_.push(folly::Unit(), static_cast<ChannelBridgeBase*>(this));
      receiverQueue_.close(static_cast<ChannelBridgeBase*>(this));
    }
  }

  bool isReceiverCancelled() { return receiverQueue_.isClosed(); }

  bool receiverWait(IChannelCallback* callback) {
    return receiverQueue_.wait(callback, static_cast<ChannelBridgeBase*>(this));
  }

  IChannelCallback* cancelReceiverWait() {
    return receiverQueue_.cancelCallback();
  }

  ReceiverQueue<TValue> receiverGetValues() {
    return receiverQueue_.getMessages(static_cast<ChannelBridgeBase*>(this));
  }

 private:
  using ReceiverAtomicQueue = typename folly::channels::detail::
      AtomicQueue<IChannelCallback, folly::Try<TValue>>;

  using SenderAtomicQueue = typename folly::channels::detail::
      AtomicQueue<IChannelCallback, folly::Unit>;

  void decref() {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  ReceiverAtomicQueue receiverQueue_;
  SenderAtomicQueue senderQueue_;
  std::atomic<int8_t> refCount_{1};
};

template <typename TValue>
using ChannelBridgePtr = typename ChannelBridge<TValue>::Ptr;
} // namespace detail
} // namespace channels
} // namespace folly
