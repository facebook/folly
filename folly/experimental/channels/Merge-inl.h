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

#include <folly/container/F14Set.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

namespace detail {

/**
 * This object does the merging of values from the input receiver to the output
 * receiver. It is not an object that the user is aware of or holds a pointer
 * to. The lifetime of this object is described by the following state machine.
 *
 * The sender and all receivers can be in one of three conceptual states:
 * Active, CancellationTriggered, or CancellationProcessed. When the sender and
 * all receivers reach the CancellationProcessed state, this object is deleted.
 *
 * When an input receiver receives a value indicating that the channel has been
 * closed, the state of that receiver transitions from Active directly to
 * CancellationProcessed.
 *
 * If this is the last receiver to be closed, or if the receiver closed with an
 * exception, the state of the sender and all other receivers transitions from
 * Active to CancellationTriggered. In that case, once we receive callbacks
 * indicating the cancellation signal has been received for all other receivers
 * and the sender, the state of the sender and all other receivers transitions
 * to CancellationProcessed (and this object is deleted).
 *
 * When the sender receives notification that the consumer of the output
 * receiver has stopped consuming, the state of the sender transitions from
 * Active directly to CancellationProcessed, and the state of all remaining
 * input receivers transitions from Active to CancellationTriggered. This
 * object will then be deleted once each remaining input receiver transitions to
 * the CancellationProcessed state (after we receive each cancelled callback).
 */
template <typename TValue>
class MergeProcessor : public IChannelCallback {
 public:
  MergeProcessor(
      Sender<TValue> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
      : sender_(std::move(detail::senderGetBridge(sender))),
        executor_(std::move(executor)) {}

  /**
   * Starts merging inputs from all input receivers into the output receiver.
   *
   * @param inputReceivers: The collection of input receivers to merge.
   */
  void start(std::vector<Receiver<TValue>> inputReceivers) {
    executor_->add([=, inputReceivers = std::move(inputReceivers)]() mutable {
      if (!sender_->senderWait(this)) {
        processSenderCancelled();
        return;
      }
      auto buffers =
          folly::F14FastMap<ChannelBridge<TValue>*, ReceiverQueue<TValue>>();
      receivers_.reserve(inputReceivers.size());
      buffers.reserve(inputReceivers.size());
      for (auto& inputReceiver : inputReceivers) {
        auto [unbufferedInputReceiver, buffer] =
            detail::receiverUnbuffer(std::move(inputReceiver));
        CHECK(buffers
                  .insert(std::make_pair(
                      unbufferedInputReceiver.get(), std::move(buffer)))
                  .second);
        receivers_.insert(unbufferedInputReceiver.release());
      }
      for (auto* receiver : receivers_) {
        processAllAvailableValues(
            receiver,
            !buffers.empty()
                ? std::make_optional(std::move(buffers.at(receiver)))
                : std::nullopt);
      }
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
        CHECK_NE(getSenderState(), ChannelState::CancellationProcessed);
        sender_->senderClose();
        processSenderCancelled();
      } else {
        // One or more values are now available from an input receiver.
        auto* receiver = static_cast<ChannelBridge<TValue>*>(bridge);
        CHECK_NE(
            getReceiverState(receiver), ChannelState::CancellationProcessed);
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
        CHECK_EQ(getSenderState(), ChannelState::CancellationTriggered);
        processSenderCancelled();
      } else {
        // We previously cancelled this input receiver, either because the
        // consumer of the output receiver stopped consuming or because another
        // input receiver received an exception. Process the cancellation for
        // this input receiver.
        auto* receiver = static_cast<ChannelBridge<TValue>*>(bridge);
        CHECK_EQ(
            getReceiverState(receiver), ChannelState::CancellationTriggered);
        processReceiverCancelled(receiver, CloseResult());
      }
    });
  }

 private:
  /**
   * Processes all available values from the input receiver (starting from the
   * provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  void processAllAvailableValues(
      ChannelBridge<TValue>* receiver,
      std::optional<ReceiverQueue<TValue>> buffer = std::nullopt) {
    auto closeResult =
        getReceiverState(receiver) == ChannelState::CancellationTriggered
        ? CloseResult()
        : buffer.has_value() ? processValues(std::move(buffer.value()))
                             : std::nullopt;
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
   * due to receipt of an exception (or the cancellation was the last input
   * receiver to be closed), we will also trigger cancellation for the sender
   * (and all other input receivers).
   */
  void processReceiverCancelled(
      ChannelBridge<TValue>* receiver, CloseResult closeResult) {
    CHECK_EQ(getReceiverState(receiver), ChannelState::CancellationTriggered);
    receivers_.erase(receiver);
    (ChannelBridgePtr<TValue>(receiver));
    if (closeResult.exception.has_value()) {
      // We received an exception. We need to close the sender and all other
      // receivers.
      if (getSenderState() == ChannelState::Active) {
        sender_->senderClose(std::move(closeResult.exception.value()));
      }
      for (auto* otherReceiver : receivers_) {
        if (getReceiverState(otherReceiver) == ChannelState::Active) {
          otherReceiver->receiverCancel();
        }
      }
    } else if (receivers_.empty()) {
      // We just closed the last receiver. Close the sender.
      if (getSenderState() == ChannelState::Active) {
        sender_->senderClose();
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
    CHECK_EQ(getSenderState(), ChannelState::CancellationTriggered);
    sender_ = nullptr;
    for (auto* receiver : receivers_) {
      if (getReceiverState(receiver) == ChannelState::Active) {
        receiver->receiverCancel();
      }
    }
    maybeDelete();
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * sender and all input receivers.
   */
  void maybeDelete() {
    if (getSenderState() == ChannelState::CancellationProcessed &&
        receivers_.empty()) {
      delete this;
    }
  }

  ChannelState getReceiverState(ChannelBridge<TValue>* receiver) {
    return detail::getReceiverState(receiver);
  }

  ChannelState getSenderState() {
    return detail::getSenderState(sender_.get());
  }

  folly::F14FastSet<ChannelBridge<TValue>*> receivers_;
  ChannelBridgePtr<TValue> sender_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
};
} // namespace detail

template <typename TReceiver, typename TValue>
Receiver<TValue> merge(
    std::vector<TReceiver> inputReceivers,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor) {
  auto [outputReceiver, outputSender] = Channel<TValue>::create();
  auto* processor = new detail::MergeProcessor<TValue>(
      std::move(outputSender), std::move(executor));
  processor->start(std::move(inputReceivers));
  return std::move(outputReceiver);
}
} // namespace channels
} // namespace folly
