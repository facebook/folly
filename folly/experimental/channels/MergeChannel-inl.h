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

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

template <typename TValue, typename TSubscriptionId>
MergeChannel<TValue, TSubscriptionId>::MergeChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename TValue, typename TSubscriptionId>
MergeChannel<TValue, TSubscriptionId>::MergeChannel(
    MergeChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename TValue, typename TSubscriptionId>
MergeChannel<TValue, TSubscriptionId>&
MergeChannel<TValue, TSubscriptionId>::operator=(
    MergeChannel&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  if (processor_) {
    std::move(*this).close();
  }
  processor_ = std::exchange(other.processor_, nullptr);
  return *this;
}

template <typename TValue, typename TSubscriptionId>
MergeChannel<TValue, TSubscriptionId>::~MergeChannel() {
  if (processor_) {
    std::move(*this).close(std::nullopt /* ex */);
  }
}

template <typename TValue, typename TSubscriptionId>
MergeChannel<TValue, TSubscriptionId>::operator bool() const {
  return processor_;
}

template <typename TValue, typename TSubscriptionId>
template <typename TReceiver>
void MergeChannel<TValue, TSubscriptionId>::addNewReceiver(
    TSubscriptionId subscriptionId, TReceiver receiver) {
  processor_->addNewReceiver(subscriptionId, std::move(receiver));
}

template <typename TValue, typename TSubscriptionId>
void MergeChannel<TValue, TSubscriptionId>::removeReceiver(
    TSubscriptionId subscriptionId) {
  processor_->removeReceiver(subscriptionId);
}

template <typename TValue, typename TSubscriptionId>
void MergeChannel<TValue, TSubscriptionId>::close(
    std::optional<folly::exception_wrapper> ex) && {
  processor_->destroyHandle(
      ex.has_value() ? detail::CloseResult(std::move(ex.value()))
                     : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

template <typename TValue, typename TSubscriptionId>
class IMergeChannelProcessor : public IChannelCallback {
 public:
  virtual void addNewReceiver(
      TSubscriptionId subscriptionId, Receiver<TValue> receiver) = 0;

  virtual void removeReceiver(TSubscriptionId subscriptionId) = 0;

  virtual void destroyHandle(CloseResult closeResult) = 0;
};

/**
 * This object does the merging of values from the input receivers to the output
 * receiver. The lifetime of this object is described by the following state
 * machine.
 *
 * The sender and all active receivers can be in one of three conceptual states:
 * Active, CancellationTriggered, or CancellationProcessed (removed). When the
 * sender and all receivers reach the CancellationProcessed state AND the user's
 * MergeChannel object is deleted, this object is deleted.
 *
 * When an input receiver receives a value indicating that the channel has
 * been closed, the state of that receiver transitions from Active directly to
 * CancellationProcessed and the receiver is removed.
 *
 * If the receiver closed with an exception, the state of the sender and all
 * other receivers transitions from Active to CancellationTriggered. In that
 * case, once we receive callbacks indicating the cancellation signal has been
 * received for all other receivers and the sender, the state of the sender and
 * all other receivers transitions to CancellationProcessed (and this object
 * will be deleted once the user destroys their MergeChannel object).
 *
 * When the sender receives notification that the consumer of the output
 * receiver has stopped consuming, the state of the sender transitions from
 * Active directly to CancellationProcessed, and the state of all remaining
 * input receivers transitions from Active to CancellationTriggered. Once we
 * receive callbacks for all input receivers indicating that the cancellation
 * signal has been received, each such receiver is transitioned to the
 * CancellationProcessed state (and this object will be deleted once the user
 * destroys their MergeChannel object).
 *
 * When the user destroys their MergeChannel object, the state of the sender and
 * all remaining receivers transition from Active to CancellationTriggered. This
 * object will then be deleted once the sender and each remaining input receiver
 * transitions to the CancellationProcessed state (after we receive each
 * cancelled callback).
 */
template <typename TValue, typename TSubscriptionId>
class MergeChannelProcessor
    : public IMergeChannelProcessor<TValue, TSubscriptionId> {
 public:
  MergeChannelProcessor(
      Sender<TValue> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
      : sender_(std::move(detail::senderGetBridge(sender))),
        executor_(std::move(executor)) {
    CHECK(sender_->senderWait(this));
  }

  /**
   * Adds a new receiver to be merged, along with a subscription ID to allow for
   * later removal.
   */
  void addNewReceiver(
      TSubscriptionId subscriptionId, Receiver<TValue> receiver) {
    executor_->add([=,
                    subscriptionId = std::move(subscriptionId),
                    receiver = std::move(receiver)]() mutable {
      if (getSenderState() != ChannelState::Active) {
        return;
      }
      auto [unbufferedReceiver, buffer] =
          detail::receiverUnbuffer(std::move(receiver));
      auto existingReceiverIt = receiversBySubscriptionId_.find(subscriptionId);
      if (existingReceiverIt != receiversBySubscriptionId_.end() &&
          receivers_.contains(existingReceiverIt->second)) {
        // We already have a receiver with the given subscription ID. Trigger
        // cancellation on that previous receiver.
        if (!existingReceiverIt->second->isReceiverCancelled()) {
          existingReceiverIt->second->receiverCancel();
        }
        receiversBySubscriptionId_.erase(existingReceiverIt);
      }
      receiversBySubscriptionId_.insert(
          std::make_pair(subscriptionId, unbufferedReceiver.get()));
      auto* receiverPtr = unbufferedReceiver.get();
      receivers_.insert(std::move(unbufferedReceiver));
      processAllAvailableValues(receiverPtr, std::move(buffer));
    });
  }

  /**
   * Removes the receiver with the given subscription ID.
   */
  void removeReceiver(TSubscriptionId subscriptionId) {
    executor_->add([=]() {
      if (getSenderState() != ChannelState::Active) {
        return;
      }
      auto receiverIt = receiversBySubscriptionId_.find(subscriptionId);
      if (receiverIt == receiversBySubscriptionId_.end()) {
        return;
      }
      if (!receiverIt->second->isReceiverCancelled()) {
        receiverIt->second->receiverCancel();
      }
      receiversBySubscriptionId_.erase(receiverIt);
    });
  }

  /**
   * Called when the user's MergeChannel object is destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    executor_->add([=, closeResult = std::move(closeResult)]() mutable {
      processHandleDestroyed(std::move(closeResult));
    });
  }

  /**
   * Called when one of the channels we are listening to has an update (either
   * a value from an input receiver or a cancellation from the output receiver).
   */
  void consume(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (bridge == sender_.get()) {
        // The consumer of the output receiver has stopped consuming.
        CHECK(getSenderState() != ChannelState::CancellationProcessed);
        sender_->senderClose();
        processSenderCancelled();
      } else {
        // One or more values are now available from an input receiver.
        auto* receiver = static_cast<ChannelBridge<TValue>*>(bridge);
        CHECK(
            getReceiverState(receiver) != ChannelState::CancellationProcessed);
        processAllAvailableValues(receiver);
      }
    });
  }

  /**
   * Called after we cancelled one of the channels we were listening to (either
   * the sender or an input receiver).
   */
  void canceled(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (bridge == sender_.get()) {
        // We previously cancelled the sender due to an input receiver closure
        // with an exception (or the closure of all input receivers without an
        // exception). Process the cancellation for the sender.
        CHECK(getSenderState() == ChannelState::CancellationTriggered);
        processSenderCancelled();
      } else {
        // We previously cancelled this input receiver, either because the
        // consumer of the output receiver stopped consuming or because another
        // input receiver received an exception. Process the cancellation for
        // this input receiver.
        auto* receiver = static_cast<ChannelBridge<TValue>*>(bridge);
        processReceiverCancelled(receiver, CloseResult());
      }
    });
  }

 protected:
  /**
   * Processes all available values from the given input receiver channel
   * (starting from the provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  void processAllAvailableValues(
      ChannelBridge<TValue>* receiver,
      std::optional<ReceiverQueue<TValue>> buffer = std::nullopt) {
    auto closeResult = receiver->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value() ? processValues(std::move(buffer.value()))
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
      closeResult = processValues(std::move(values));
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      receiver->receiverCancel();
      processReceiverCancelled(receiver, std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for an input receiver. Returns a
   * CloseResult if the given channel was closed, so the caller can stop
   * attempting to process values from it.
   */
  std::optional<CloseResult> processValues(ReceiverQueue<TValue> values) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      if (inputResult.hasValue()) {
        // We have received a normal value from an input receiver. Write it to
        // the output receiver.
        sender_->senderPush(std::move(inputResult.value()));
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
   * Processes the cancellation of an input receiver. If the cancellation was
   * due to receipt of an exception, we will also trigger cancellation for the
   * sender (and all other input receivers).
   */
  void processReceiverCancelled(
      ChannelBridge<TValue>* receiver, CloseResult closeResult) {
    CHECK(getReceiverState(receiver) == ChannelState::CancellationTriggered);
    receivers_.erase(receiver);
    if (closeResult.exception.has_value()) {
      // We received an exception. We need to close the sender and all
      // receivers.
      if (getSenderState() == ChannelState::Active) {
        sender_->senderClose(std::move(closeResult.exception.value()));
      }
      for (auto& otherReceiver : receivers_) {
        if (getReceiverState(otherReceiver.get()) == ChannelState::Active) {
          otherReceiver->receiverCancel();
        }
      }
    }
    maybeDelete();
  }

  /**
   * Processes the cancellation of the sender (indicating that the consumer of
   * the output receiver has stopped consuming). We will trigger cancellation
   * for all input receivers not already cancelled.
   */
  void processSenderCancelled() {
    CHECK(getSenderState() == ChannelState::CancellationTriggered);
    sender_ = nullptr;
    for (auto& receiver : receivers_) {
      if (getReceiverState(receiver.get()) == ChannelState::Active) {
        receiver->receiverCancel();
      }
    }
    maybeDelete();
  }

  /**
   * Processes the destruction of the user's MergeChannel object.  We will
   * close the sender and trigger cancellation for all input receivers not
   * already cancelled.
   */
  void processHandleDestroyed(CloseResult closeResult) {
    CHECK(!handleDestroyed_);
    handleDestroyed_ = true;
    if (getSenderState() == ChannelState::Active) {
      if (closeResult.exception.has_value()) {
        sender_->senderClose(std::move(closeResult.exception.value()));
      } else {
        sender_->senderClose();
      }
    }
    for (auto& receiver : receivers_) {
      if (getReceiverState(receiver.get()) == ChannelState::Active) {
        receiver->receiverCancel();
      }
    }
    maybeDelete();
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * sender and all input receivers, and if the user's MergeChannel object was
   * destroyed.
   */
  void maybeDelete() {
    if (getSenderState() == ChannelState::CancellationProcessed &&
        receivers_.empty() && handleDestroyed_) {
      delete this;
    }
  }

  ChannelState getReceiverState(ChannelBridge<TValue>* receiver) {
    return detail::getReceiverState(receiver);
  }

  ChannelState getSenderState() {
    return detail::getSenderState(sender_.get());
  }

  ChannelBridgePtr<TValue> sender_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  folly::F14FastSet<
      ChannelBridgePtr<TValue>,
      ChannelBridgeHash<TValue>,
      ChannelBridgeEqual<TValue>>
      receivers_;
  folly::F14FastMap<TSubscriptionId, ChannelBridge<TValue>*>
      receiversBySubscriptionId_;
  bool handleDestroyed_{false};
};
} // namespace detail

template <typename TValue, typename TSubscriptionId>
std::pair<Receiver<TValue>, MergeChannel<TValue, TSubscriptionId>>
createMergeChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor) {
  auto [receiver, sender] = Channel<TValue>::create();
  auto* processor = new detail::MergeChannelProcessor<TValue, TSubscriptionId>(
      std::move(sender), std::move(executor));
  return std::make_pair(
      std::move(receiver), MergeChannel<TValue, TSubscriptionId>(processor));
}
} // namespace channels
} // namespace folly
