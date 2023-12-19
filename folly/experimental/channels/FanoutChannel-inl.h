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
#include <folly/experimental/channels/FanoutSender.h>
#include <folly/experimental/channels/detail/Utility.h>

namespace folly {
namespace channels {

template <typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType>::FanoutChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType>::FanoutChannel(
    FanoutChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType>&
FanoutChannel<ValueType, ContextType>::operator=(
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

template <typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType>::~FanoutChannel() {
  if (processor_ != nullptr) {
    std::move(*this).close(exception_wrapper());
  }
}

template <typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType>::operator bool() const {
  return processor_ != nullptr;
}

template <typename ValueType, typename ContextType>
Receiver<ValueType> FanoutChannel<ValueType, ContextType>::subscribe(
    folly::Function<std::vector<ValueType>(const ContextType&)>
        getInitialValues) {
  return processor_->subscribe(std::move(getInitialValues));
}

template <typename ValueType, typename ContextType>
bool FanoutChannel<ValueType, ContextType>::anySubscribers() const {
  return processor_->anySubscribers();
}

template <typename ValueType, typename ContextType>
void FanoutChannel<ValueType, ContextType>::closeSubscribers(
    exception_wrapper ex) {
  processor_->closeSubscribers(
      ex ? detail::CloseResult(std::move(ex)) : detail::CloseResult());
}

template <typename ValueType, typename ContextType>
void FanoutChannel<ValueType, ContextType>::close(exception_wrapper ex) && {
  processor_->destroyHandle(
      ex ? detail::CloseResult(std::move(ex)) : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

template <typename ValueType, typename ContextType>
class IFanoutChannelProcessor : public IChannelCallback {
 public:
  virtual Receiver<ValueType> subscribe(
      folly::Function<std::vector<ValueType>(const ContextType&)>
          getInitialValues) = 0;

  virtual bool anySubscribers() = 0;

  virtual void closeSubscribers(CloseResult closeResult) = 0;

  virtual void destroyHandle(CloseResult closeResult) = 0;
};

/**
 * This object fans out values from the input receiver to all output receivers.
 * The lifetime of this object is described by the following state machine.
 *
 * The input receiver can be in one of three conceptual states: Active,
 * CancellationTriggered, or CancellationProcessed (removed). When the input
 * receiver reaches the CancellationProcessed state AND the user's FanoutChannel
 * object is deleted, this object is deleted.
 *
 * When an input receiver receives a value indicating that the channel has
 * been closed, the state of the input receiver transitions from Active directly
 * to CancellationProcessed (and this object will be deleted once the user
 * destroys their FanoutChannel object).
 *
 * When the user destroys their FanoutChannel object, the state of the input
 * receiver transitions from Active to CancellationTriggered. This object will
 * then be deleted once the input receiver transitions to the
 * CancellationProcessed state.
 */
template <typename ValueType, typename ContextType>
class FanoutChannelProcessor
    : public IFanoutChannelProcessor<ValueType, ContextType> {
 private:
  struct State {
    State(ContextType _context) : context(std::move(_context)) {}

    ChannelState getReceiverState() {
      return detail::getReceiverState(receiver.get());
    }

    ChannelBridgePtr<ValueType> receiver;
    FanoutSender<ValueType> fanoutSender;
    ContextType context;
    bool handleDeleted{false};
  };

  using WLockedStatePtr = typename folly::Synchronized<State>::WLockedPtr;

 public:
  explicit FanoutChannelProcessor(
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      ContextType context)
      : executor_(std::move(executor)), state_(std::move(context)) {}

  /**
   * Starts fanning out values from the input receiver to all output receivers.
   *
   * @param inputReceiver: The input receiver to fan out values from.
   */
  void start(Receiver<ValueType> inputReceiver) {
    auto state = state_.wlock();
    auto [unbufferedInputReceiver, buffer] =
        detail::receiverUnbuffer(std::move(inputReceiver));
    state->receiver = std::move(unbufferedInputReceiver);

    // Start processing new values that come in from the input receiver.
    processAllAvailableValues(state, std::move(buffer));
  }

  /**
   * Returns a new output receiver that will receive all values from the input
   * receiver. If a getInitialValues parameter is provided, it will be executed
   * to determine the set of initial values that will (only) go to the new input
   * receiver.
   */
  Receiver<ValueType> subscribe(
      folly::Function<std::vector<ValueType>(const ContextType&)>
          getInitialValues) override {
    auto state = state_.wlock();
    auto initialValues = getInitialValues ? getInitialValues(state->context)
                                          : std::vector<ValueType>();
    if (!state->receiver) {
      auto [receiver, sender] = Channel<ValueType>::create();
      for (auto&& value : initialValues) {
        sender.write(std::move(value));
      }
      std::move(sender).close();
      return std::move(receiver);
    }
    return state->fanoutSender.subscribe(std::move(initialValues));
  }

  /**
   * Closes all subscribers without closing the fanout channel.
   */
  void closeSubscribers(CloseResult closeResult) {
    auto state = state_.wlock();
    std::move(state->fanoutSender)
        .close(
            closeResult.exception.has_value() ? closeResult.exception.value()
                                              : exception_wrapper());
  }

  /**
   * This is called when the user's FanoutChannel object has been destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    auto state = state_.wlock();
    processHandleDestroyed(state, std::move(closeResult));
  }

  /**
   * Returns whether this fanout channel has any output receivers.
   */
  bool anySubscribers() override {
    return state_.wlock()->fanoutSender.anySubscribers();
  }

 private:
  /**
   * Called when one of the channels we are listening to has an update (either
   * a value from the input receiver or a cancellation from an output receiver).
   */
  void consume(ChannelBridgeBase*) override {
    executor_->add([=, this]() {
      // One or more values are now available from the input receiver.
      auto state = state_.wlock();
      CHECK_NE(state->getReceiverState(), ChannelState::CancellationProcessed);
      processAllAvailableValues(state);
    });
  }

  void canceled(ChannelBridgeBase*) override {
    executor_->add([=, this]() {
      // We previously cancelled this input receiver, due to the destruction of
      // the handle. Process the cancellation for this input receiver.
      auto state = state_.wlock();
      processReceiverCancelled(state, CloseResult());
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
      WLockedStatePtr& state,
      std::optional<ReceiverQueue<ValueType>> buffer = std::nullopt) {
    auto closeResult = state->receiver->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value() ? processValues(state, std::move(buffer.value()))
                              : std::nullopt);
    while (!closeResult.has_value()) {
      if (state->receiver->receiverWait(this)) {
        // There are no more values available right now. We will stop processing
        // until the channel fires the consume() callback (indicating that more
        // values are available).
        break;
      }
      auto values = state->receiver->receiverGetValues();
      CHECK(!values.empty());
      closeResult = processValues(state, std::move(values));
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      state->receiver->receiverCancel();
      processReceiverCancelled(state, std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for the input receiver. Returns a
   * CloseResult if channel was closed, so the caller can stop attempting to
   * process values from it.
   */
  std::optional<CloseResult> processValues(
      WLockedStatePtr& state, ReceiverQueue<ValueType> values) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      if (inputResult.hasValue()) {
        // We have received a normal value from the input receiver. Write it to
        // all output senders.
        state->context.update(
            inputResult.value(), state->fanoutSender.numSubscribers());
        state->fanoutSender.write(std::move(inputResult.value()));
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
  void processReceiverCancelled(
      WLockedStatePtr& state, CloseResult closeResult) {
    CHECK_EQ(state->getReceiverState(), ChannelState::CancellationTriggered);
    state->receiver = nullptr;
    std::move(state->fanoutSender)
        .close(
            closeResult.exception.has_value() ? closeResult.exception.value()
                                              : exception_wrapper());
    maybeDelete(state);
  }

  /**
   * Processes the destruction of the user's FanoutChannel object.  We will
   * cancel the receiver and trigger cancellation for all senders not already
   * cancelled.
   */
  void processHandleDestroyed(WLockedStatePtr& state, CloseResult closeResult) {
    state->handleDeleted = true;
    if (state->getReceiverState() == ChannelState::Active) {
      state->receiver->receiverCancel();
    }
    std::move(state->fanoutSender)
        .close(
            closeResult.exception.has_value() ? closeResult.exception.value()
                                              : exception_wrapper());
    maybeDelete(state);
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * receiver and all senders, and if the user's FanoutChannel object was
   * destroyed.
   */
  void maybeDelete(WLockedStatePtr& state) {
    if (state->getReceiverState() == ChannelState::CancellationProcessed &&
        state->handleDeleted) {
      state.unlock();
      delete this;
    }
  }

  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  folly::Synchronized<State> state_;
};
} // namespace detail

template <typename TReceiver, typename ValueType, typename ContextType>
FanoutChannel<ValueType, ContextType> createFanoutChannel(
    TReceiver inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    ContextType context) {
  auto* processor = new detail::FanoutChannelProcessor<ValueType, ContextType>(
      std::move(executor), std::move(context));
  processor->start(std::move(inputReceiver));
  return FanoutChannel<ValueType, ContextType>(processor);
}
} // namespace channels
} // namespace folly
