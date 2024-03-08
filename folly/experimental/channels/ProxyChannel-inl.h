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

#include <folly/experimental/channels/ProxyChannel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

template <typename ValueType>
ProxyChannel<ValueType>::ProxyChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename ValueType>
ProxyChannel<ValueType>::ProxyChannel(ProxyChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename ValueType>
ProxyChannel<ValueType>& ProxyChannel<ValueType>::operator=(
    ProxyChannel&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  if (processor_) {
    std::move(*this).close();
  }
  processor_ = std::exchange(other.processor_, nullptr);
  return *this;
}

template <typename ValueType>
ProxyChannel<ValueType>::~ProxyChannel() {
  if (processor_) {
    std::move(*this).close();
  }
}

template <typename ValueType>
ProxyChannel<ValueType>::operator bool() const {
  return processor_;
}

template <typename ValueType>
void ProxyChannel<ValueType>::setInputReceiver(Receiver<ValueType> receiver) {
  processor_->setInputReceiver(std::move(receiver));
}

template <typename ValueType>
void ProxyChannel<ValueType>::removeInputReceiver() {
  processor_->removeInputReceiver();
}

template <typename ValueType>
void ProxyChannel<ValueType>::close(folly::exception_wrapper&& ex) && {
  processor_->destroyHandle(
      ex ? detail::CloseResult(std::move(ex)) : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

/**
 * This object does the proxying of values from the input receiver to the output
 * receiver.
 */
template <typename ValueType>
class ProxyChannelProcessor : public IChannelCallback {
 private:
  struct State {
    explicit State(ChannelBridgePtr<ValueType> _sender)
        : sender(std::move(_sender)) {}

    ChannelState getSenderState() {
      return detail::getSenderState(sender.get());
    }

    // The output sender for the proxy channel.
    ChannelBridgePtr<ValueType> sender;

    // The current input receiver for the proxy channel.
    ChannelBridge<ValueType>* receiver{nullptr};

    // The refcount for this proxy channel. The handle (if not yet destroyed),
    // the sender (if not yet cancelled), the current input receiver (if any),
    // and any previous input receivers not yet joined (if any) will contribute
    // to this refcount. It starts at 2, since a new ProxyChannel always has
    // one handle, one output receiver, and no input receivers.
    size_t refCount{2};
  };

  using WLockedStatePtr = typename folly::Synchronized<State>::WLockedPtr;

 public:
  ProxyChannelProcessor(
      Sender<ValueType> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
      : executor_(std::move(executor)),
        state_(State(std::move(detail::senderGetBridge(sender)))) {
    auto state = state_.wlock();
    CHECK(state->sender->senderWait(this));
  }

  /**
   * Sets a new input receiver (removing the old input receiver, if any).
   */
  void setInputReceiver(Receiver<ValueType> receiver) {
    auto state = state_.wlock();
    if (state->getSenderState() != ChannelState::Active) {
      return;
    }
    auto [unbufferedReceiver, buffer] =
        detail::receiverUnbuffer(std::move(receiver));
    cancelInputReceiverIfExists(state);
    auto receiverPtr = unbufferedReceiver.release();
    state->receiver = receiverPtr;
    state->refCount++;
    processAllAvailableValues(std::move(state), receiverPtr, std::move(buffer));
  }

  /**
   * Removes the current input receiver.
   */
  void removeInputReceiver() {
    auto state = state_.wlock();
    if (state->getSenderState() != ChannelState::Active) {
      return;
    }
    cancelInputReceiverIfExists(state);
  }

  /**
   * Called when the user's ProxyChannel object is destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    processHandleDestroyed(state_.wlock(), std::move(closeResult));
  }

  /**
   * Called when one of the channels we are listening to has an update (either
   * a value from an input receiver or a cancellation from the output receiver).
   */
  void consume(ChannelBridgeBase* bridge) override {
    executor_->add([=, this]() {
      auto state = state_.wlock();
      if (bridge == state->sender.get()) {
        // The consumer of the output receiver has stopped consuming.
        state->sender->senderClose();
        processSenderCancelled(std::move(state));
      } else {
        // One or more values are now available from an input receiver.
        auto* receiver = static_cast<ChannelBridge<ValueType>*>(bridge);
        processAllAvailableValues(std::move(state), receiver);
      }
    });
  }

  /**
   * Called after we cancelled one of the channels we were listening to (either
   * the sender or an input receiver).
   */
  void canceled(ChannelBridgeBase* bridge) override {
    executor_->add([=, this]() {
      auto state = state_.wlock();
      if (bridge == state->sender.get()) {
        // We previously cancelled the sender due to an input receiver closure.
        // Process the cancellation for the sender.
        CHECK(state->getSenderState() == ChannelState::CancellationTriggered);
        processSenderCancelled(std::move(state));
      } else {
        // We previously cancelled this input receiver. Process the cancellation
        // for this input receiver.
        auto* receiver = static_cast<ChannelBridge<ValueType>*>(bridge);
        processReceiverCancelled(std::move(state), receiver, CloseResult());
      }
    });
  }

 protected:
  /**
   * Processes all available values from the current input receiver channel
   * (starting from the provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * we will process cancellation for the input receiver.
   */
  void processAllAvailableValues(
      WLockedStatePtr state,
      ChannelBridge<ValueType>* receiver,
      std::optional<ReceiverQueue<ValueType>> buffer = std::nullopt) {
    CHECK_NOTNULL(receiver);
    if (!receiver->isReceiverCancelled()) {
      CHECK_EQ(receiver, state->receiver);
    }
    auto closeResult = receiver->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value() ? processValues(state, std::move(buffer.value()))
                              : std::nullopt);
    while (!closeResult.has_value()) {
      if (receiver->receiverWait(this)) {
        // There are no more values available right now. We will stop processing
        // until the channel fires the consume() callback (indicating that more
        // values are available).
        break;
      }
      auto values = receiver->receiverGetValues();
      CHECK(!values.empty());
      closeResult = processValues(state, std::move(values));
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      receiver->receiverCancel();
      processReceiverCancelled(
          std::move(state), receiver, std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for an input receiver. Returns a
   * CloseResult if the given channel was closed, so the caller can stop
   * attempting to process values from it.
   */
  std::optional<CloseResult> processValues(
      WLockedStatePtr& state, ReceiverQueue<ValueType> values) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      if (inputResult.hasValue()) {
        // We have received a normal value from an input receiver. Write it to
        // the output receiver.
        state->sender->senderPush(std::move(inputResult.value()));
      } else {
        // The input receiver was closed.
        return inputResult.hasException()
            ? CloseResult(std::move(inputResult.exception()))
            : CloseResult();
      }
    }
    return std::nullopt;
  }

  /**
   * Processes the cancellation of an input receiver.
   */
  void processReceiverCancelled(
      WLockedStatePtr state,
      ChannelBridge<ValueType>* receiver,
      CloseResult closeResult) {
    CHECK(receiver->isReceiverCancelled());
    if (receiver == state->receiver &&
        state->getSenderState() == ChannelState::Active) {
      if (closeResult.exception.has_value()) {
        state->sender->senderClose(std::move(closeResult.exception.value()));
      } else {
        state->sender->senderClose();
      }
    }
    if (state->receiver == receiver) {
      state->receiver = nullptr;
    }
    (ChannelBridgePtr<ValueType>(receiver)); // Delete the receiver
    state->refCount--;
    maybeDelete(std::move(state));
  }

  /**
   * Processes the cancellation of the sender (indicating that the consumer of
   * the output receiver has stopped consuming). We will trigger cancellation
   * for the input receiver if it is not already cancelled.
   */
  void processSenderCancelled(WLockedStatePtr state) {
    CHECK(state->getSenderState() == ChannelState::CancellationTriggered);
    state->sender.reset();
    state->refCount--;
    cancelInputReceiverIfExists(state);
    maybeDelete(std::move(state));
  }

  /**
   * Processes the destruction of the user's ProxyChannel object.  We will
   * close the sender and trigger cancellation for the input receiver (if any).
   */
  void processHandleDestroyed(WLockedStatePtr state, CloseResult closeResult) {
    if (state->getSenderState() == ChannelState::Active) {
      if (closeResult.exception.has_value()) {
        state->sender->senderClose(std::move(closeResult.exception.value()));
      } else {
        state->sender->senderClose();
      }
    }
    cancelInputReceiverIfExists(state);
    state->refCount--;
    maybeDelete(std::move(state));
  }

  /**
   * Cancels the current input receiver if it exists.
   */
  void cancelInputReceiverIfExists(WLockedStatePtr& state) {
    if (state->receiver != nullptr) {
      CHECK(!state->receiver->isReceiverCancelled());
      state->receiver->receiverCancel();
      state->receiver = nullptr;
    }
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * sender, the current input receiver, and all previous input receivers, and
   * if the user's ProxyChannel object was destroyed.
   */
  void maybeDelete(WLockedStatePtr state) {
    if (state->refCount == 0) {
      CHECK_NULL(state->sender.get());
      CHECK_NULL(state->receiver);
      state.unlock();
      delete this;
    }
  }

  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  folly::Synchronized<State> state_;
};
} // namespace detail

template <typename ValueType>
std::pair<Receiver<ValueType>, ProxyChannel<ValueType>> createProxyChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor) {
  auto [receiver, sender] = Channel<ValueType>::create();
  auto* processor = new detail::ProxyChannelProcessor<ValueType>(
      std::move(sender), std::move(executor));
  return std::make_pair(
      std::move(receiver), ProxyChannel<ValueType>(processor));
}
} // namespace channels
} // namespace folly
