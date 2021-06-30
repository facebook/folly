/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

template <typename TValue>
FanoutChannel<TValue>::FanoutChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename TValue>
FanoutChannel<TValue>::FanoutChannel(FanoutChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename TValue>
FanoutChannel<TValue>& FanoutChannel<TValue>::operator=(
    FanoutChannel&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  if (processor_) {
    std::move(*this).close();
  }
  processor_ = std::exchange(other.processor_, nullptr);
  return *this;
}

template <typename TValue>
FanoutChannel<TValue>::~FanoutChannel() {
  if (processor_ != nullptr) {
    std::move(*this).close(std::nullopt /* ex */);
  }
}

template <typename TValue>
FanoutChannel<TValue>::operator bool() const {
  return processor_ != nullptr;
}

template <typename TValue>
Receiver<TValue> FanoutChannel<TValue>::getNewReceiver(
    folly::Function<std::vector<TValue>()> getInitialValues) {
  return processor_->newReceiver(std::move(getInitialValues));
}

template <typename TValue>
bool FanoutChannel<TValue>::anyReceivers() {
  return processor_->anySenders();
}

template <typename TValue>
void FanoutChannel<TValue>::close(
    std::optional<folly::exception_wrapper> ex) && {
  processor_->destroyHandle(
      ex.has_value() ? detail::CloseResult(std::move(ex.value()))
                     : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

template <typename TValue>
class IFanoutChannelProcessor : public IChannelCallback {
 public:
  virtual Receiver<TValue> newReceiver(
      folly::Function<std::vector<TValue>()> getInitialValues) = 0;

  virtual bool anySenders() = 0;

  virtual void destroyHandle(CloseResult closeResult) = 0;
};

/**
 * This object fans out values from the input receiver to all output receivers.
 * The lifetime of this object is described by the following state machine.
 *
 * The input receiver and all active senders can be in one of three conceptual
 * states: Active, CancellationTriggered, or CancellationProcessed (removed).
 * When the input receiver and all senders reach the CancellationProcessed state
 * AND the user's FanoutChannel object is deleted, this object is deleted.
 *
 * When a sender receives a value indicating that the channel has been closed,
 * the state of that sender transitions from Active directly to
 * CancellationProcessed and the sender is removed.
 *
 * When an input receiver receives a value indicating that the channel has
 * been closed, the state of the input receiver transitions from Active directly
 * to CancellationProcessed, and the state of all remaining senders transitions
 * from Active to CancellationTriggered. Once we receive callbacks for all
 * senders indicating that the cancellation signal has been received, each such
 * sender is transitioned to the CancellationProcessed state (and this object
 * will be deleted once the user destroys their FanoutChannel object).
 *
 * When the user destroys their FanoutChannel object, the state of the input
 * receiver and all senders transitions from Active to  CancellationTriggered.
 * This object will then be deleted once the input receiver and each remaining
 * input receiver transitions to the CancellationProcessed state.
 */
template <typename TValue>
class FanoutChannelProcessor : public IFanoutChannelProcessor<TValue> {
 public:
  explicit FanoutChannelProcessor(
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
      : executor_(std::move(executor)), numSendersPlusHandle_(1) {}

  /**
   * Starts fanning out values from the input receiver to all output receivers.
   *
   * @param inputReceiver: The input receiver to fan out values from.
   */
  void start(Receiver<TValue> inputReceiver) {
    executor_->add([=, inputReceiver = std::move(inputReceiver)]() mutable {
      auto [unbufferedInputReceiver, buffer] =
          detail::receiverUnbuffer(std::move(inputReceiver));
      receiver_ = std::move(unbufferedInputReceiver);

      // Start processing new values that come in from the input receiver.
      processAllAvailableValues(std::move(buffer));
    });
  }

  /**
   * Returns a new output receiver that will receive all values from the input
   * receiver. If a getInitialValues parameter is provided, it will be executed
   * to determine the set of initial values that will (only) go to the new input
   * receiver.
   */
  Receiver<TValue> newReceiver(
      folly::Function<std::vector<TValue>()> getInitialValues) override {
    numSendersPlusHandle_++;
    auto [receiver, sender] = Channel<TValue>::create();
    executor_->add([=,
                    sender = std::move(senderGetBridge(sender)),
                    getInitialValues = std::move(getInitialValues)]() mutable {
      if (getInitialValues) {
        auto initialValues = getInitialValues();
        for (auto& value : initialValues) {
          sender->senderPush(std::move(value));
        }
      }
      if (getReceiverState() != ChannelState::Active ||
          !sender->senderWait(this)) {
        sender->senderClose();
        numSendersPlusHandle_--;
      } else {
        senders_.insert(std::move(sender));
      }
    });
    return std::move(receiver);
  }

  /**
   * This is called when the user's FanoutChannel object has been destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    executor_->add([=, closeResult = std::move(closeResult)]() mutable {
      processHandleDestroyed(std::move(closeResult));
    });
  }

  /**
   * Returns whether this fanout channel has any output receivers.
   */
  bool anySenders() override { return numSendersPlusHandle_ > 1; }

  ChannelState getReceiverState() {
    return detail::getReceiverState(receiver_.get());
  }

  ChannelState getSenderState(ChannelBridge<TValue>* sender) {
    return detail::getSenderState(sender);
  }

 private:
  bool isClosed() {
    if (!receiver_) {
      CHECK(!anySenders());
      return true;
    } else {
      return false;
    }
  }

  /**
   * Called when one of the channels we are listening to has an update (either
   * a value from the input receiver or a cancellation from an output receiver).
   */
  void consume(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (receiver_.get() == bridge) {
        // One or more values are now available from the input receiver.
        CHECK_NE(getReceiverState(), ChannelState::CancellationProcessed);
        processAllAvailableValues();
      } else {
        // The consumer of an output receiver has stopped consuming.
        auto* sender = static_cast<ChannelBridge<TValue>*>(bridge);
        CHECK_NE(getSenderState(sender), ChannelState::CancellationProcessed);
        sender->senderClose();
        processSenderCancelled(sender);
      }
    });
  }

  void canceled(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (receiver_.get() == bridge) {
        // We previously cancelled this input receiver, either because the
        // consumer of the output receiver stopped consuming or because of the
        // destruction of the user's FanoutChannel object. Process the
        // cancellation for this input receiver.
        processReceiverCancelled(CloseResult());
      } else {
        // We previously cancelled the sender due to the closure of the input
        // receiver or the destruction of the user's FanoutChannel object.
        // Process the cancellation for the sender.
        auto* sender = static_cast<ChannelBridge<TValue>*>(bridge);
        CHECK_EQ(getSenderState(sender), ChannelState::CancellationTriggered);
        processSenderCancelled(sender);
      }
    });
  }

  /**
   * Processes all available values from the input receiver (starting from the
   * provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  void processAllAvailableValues(
      std::optional<ReceiverQueue<TValue>> buffer = std::nullopt) {
    auto closeResult = receiver_->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value() ? processValues(std::move(buffer.value()))
                              : std::nullopt);
    while (!closeResult.has_value()) {
      if (receiver_->receiverWait(this)) {
        // There are no more values available right now. We will stop processing
        // until the channel fires the consume() callback (indicating that more
        // values are available).
        break;
      }
      auto values = receiver_->receiverGetValues();
      CHECK(!values.empty());
      closeResult = processValues(std::move(values));
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      receiver_->receiverCancel();
      processReceiverCancelled(std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for the input receiver. Returns a
   * CloseResult if channel was closed, so the caller can stop attempting to
   * process values from it.
   */
  std::optional<CloseResult> processValues(ReceiverQueue<TValue> values) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      if (inputResult.hasValue()) {
        // We have received a normal value from the input receiver. Write it to
        // all output senders.
        for (auto& sender : senders_) {
          sender->senderPush(inputResult.value());
        }
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
   * Processes the cancellation of the input receiver. We will close all senders
   * with the exception received from the input receiver (if any).
   */
  void processReceiverCancelled(CloseResult closeResult) {
    CHECK_EQ(getReceiverState(), ChannelState::CancellationTriggered);
    receiver_ = nullptr;
    for (auto& sender : senders_) {
      if (closeResult.exception.has_value()) {
        sender->senderClose(closeResult.exception.value());
      } else {
        sender->senderClose();
      }
    }
    maybeDelete();
  }

  /**
   * Processes the cancellation of a sender (indicating that the consumer of
   * the corresponding output receiver has stopped consuming).
   */
  void processSenderCancelled(ChannelBridge<TValue>* sender) {
    CHECK_EQ(getSenderState(sender), ChannelState::CancellationTriggered);
    senders_.erase(sender);
    numSendersPlusHandle_--;
    maybeDelete();
  }

  /**
   * Processes the destruction of the user's FanoutChannel object.  We will
   * cancel the receiver and trigger cancellation for all senders not already
   * cancelled.
   */
  void processHandleDestroyed(CloseResult closeResult) {
    CHECK_GT(numSendersPlusHandle_, 0);
    numSendersPlusHandle_--;
    if (getReceiverState() == ChannelState::Active) {
      receiver_->receiverCancel();
    }
    for (auto& sender : senders_) {
      if (closeResult.exception.has_value()) {
        sender->senderClose(closeResult.exception.value());
      } else {
        sender->senderClose();
      }
    }
    maybeDelete();
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * receiver and all senders, and if the user's FanoutChannel object was
   * destroyed.
   */
  void maybeDelete() {
    if (getReceiverState() == ChannelState::CancellationProcessed &&
        numSendersPlusHandle_ == 0) {
      delete this;
    }
  }

  ChannelBridgePtr<TValue> receiver_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  folly::F14FastSet<
      ChannelBridgePtr<TValue>,
      ChannelBridgeHash<TValue>,
      ChannelBridgeEqual<TValue>>
      senders_;
  std::atomic<size_t> numSendersPlusHandle_;
};
} // namespace detail

template <typename TReceiver, typename TValue>
FanoutChannel<TValue> createFanoutChannel(
    TReceiver inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor) {
  auto* processor =
      new detail::FanoutChannelProcessor<TValue>(std::move(executor));
  processor->start(std::move(inputReceiver));
  return FanoutChannel<TValue>(processor);
}
} // namespace channels
} // namespace folly
