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

#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/Utility.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {

namespace detail {

/**
 * This object transforms values from the input receiver to the output receiver.
 * It is not an object that the user is aware of or holds a pointer to. The
 * lifetime of this object is described by the following state machine.
 *
 * Both the sender and receiver can be in one of three states: Active,
 * CancellationTriggered, or CancellationProcessed. When both the sender and
 * receiver reach the CancellationProcessed state, this object is deleted.
 *
 * When the input receiver receives a value indicating that the channel has been
 * closed, the state of the receiver transitions from Active directly to
 * CancellationProcessed and the state of the sender transitions from Active to
 * CancellationTriggered. Once we receive a callback indicating the sender's
 * cancellation signal has been received, the sender's state is transitioned
 * from CancellationTriggered to CancellationProcessed (and the object is
 * deleted).
 *
 * When the sender receives notification that the consumer of the output
 * receiver has stopped consuming, the state of the sender transitions from
 * Active directly to CancellationProcessed, and the state of the input receiver
 * transitions from Active to CancellationTriggered. Once we receive a callback
 * indicating that the input receiver's cancellation signal has been received,
 * the input receiver's state is transitioned from CancellationTriggered to
 * to CancellationProcessed (and the object is deleted).
 */
template <
    typename TInputValue,
    typename TOutputValue,
    typename TransformValueFunc>
class TransformProcessorBase : public IChannelCallback {
 public:
  TransformProcessorBase(
      Sender<TOutputValue> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      TransformValueFunc transformValue)
      : sender_(std::move(senderGetBridge(sender))),
        executor_(std::move(executor)),
        transformValue_(std::move(transformValue)) {}

  template <typename TReceiver>
  void startTransform(TReceiver receiver) {
    executor_->add([=, receiver = std::move(receiver)]() mutable {
      runOperationWithSenderCancellation(
          this->executor_,
          this->sender_,
          false /* alreadyStartedWaiting */,
          this /* channelCallbackToRestore */,
          startTransformImpl(std::move(receiver)));
    });
  }

 protected:
  /**
   * Starts transforming values from the input receiver and sending the
   * resulting transformed values to the output receiver.
   *
   * @param inputReceiver: The input receiver to transform values from.
   */
  folly::coro::Task<void> startTransformImpl(Receiver<TInputValue> receiver) {
    auto [unbufferedInputReceiver, buffer] =
        detail::receiverUnbuffer(std::move(receiver));
    receiver_ = std::move(unbufferedInputReceiver);
    co_await processAllAvailableValues(std::move(buffer));
  }

  /**
   * This is called when one of the channels we are listening to has an update
   * (either a value from the input receiver or a cancellation signal from the
   * sender).
   */
  void consume(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (bridge == receiver_.get()) {
        // We have received new values from the input receiver.
        CHECK_NE(getReceiverState(), ChannelState::CancellationProcessed);
        runOperationWithSenderCancellation(
            this->executor_,
            this->sender_,
            true /* alreadyStartedWaiting */,
            this /* channelCallbackToRestore */,
            processAllAvailableValues());
      } else {
        CHECK_NE(getSenderState(), ChannelState::CancellationProcessed);
        // The consumer of the output receiver has stopped consuming.
        if (getSenderState() == ChannelState::Active) {
          sender_->senderClose();
        }
        processSenderCancelled();
      }
    });
  }

  /**
   * This is called after we explicitly cancel one of the channels we are
   * listening to.
   */
  void canceled(ChannelBridgeBase* bridge) override {
    executor_->add([=]() {
      if (bridge == receiver_.get()) {
        // We previously cancelled the input receiver (because the consumer of
        // the output receiver stopped consuming). Process the cancellation for
        // the input receiver.
        CHECK_EQ(getReceiverState(), ChannelState::CancellationTriggered);
        processReceiverCancelled(CloseResult()).scheduleOn(executor_).start();
      } else {
        // We previously cancelled the sender due to the closure of the input
        // receiver. Process the cancellation for the sender.
        CHECK_EQ(getSenderState(), ChannelState::CancellationTriggered);
        processSenderCancelled();
      }
    });
  }

  /**
   * Processes all available values from the input receiver (starting from the
   * provided buffer, if present).
   *
   * If a value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  folly::coro::Task<void> processAllAvailableValues(
      std::optional<ReceiverQueue<TInputValue>> buffer = std::nullopt) {
    auto closeResult = buffer.has_value()
        ? co_await processValues(std::move(buffer.value()))
        : std::nullopt;
    while (!closeResult.has_value()) {
      if (receiver_->receiverWait(this)) {
        // There are no more values available right now, but more values may
        // come in the future. We will stop processing for now, until we
        // re-start processing when the consume() callback is fired.
        break;
      }
      auto values = receiver_->receiverGetValues();
      CHECK(!values.empty());
      closeResult = co_await processValues(std::move(values));
    }
    if (closeResult.has_value()) {
      // The output receiver should be closed (either because the input receiver
      // was closed or the transform function desired the closure of the output
      // receiver).
      receiver_->receiverCancel();
      co_await processReceiverCancelled(std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for the input receiver. If the output
   * receiver should be closed (either because the input receiver was closed or
   * the transform function desired the closure of the output receiver), a
   * CloseResult is returned containing the exception (if any) that should be
   * used to close the output receiver.
   */
  folly::coro::Task<std::optional<CloseResult>> processValues(
      ReceiverQueue<TInputValue> values) {
    auto cancelToken = co_await folly::coro::co_current_cancellation_token;
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      bool inputClosed = !inputResult.hasValue();
      if (!inputResult.hasValue() && !inputResult.hasException()) {
        inputResult = folly::Try<TInputValue>(OnClosedException());
      }
      auto outputGen = folly::makeTryWith(
          [&]() { return transformValue_(std::move(inputResult)); });
      if (!outputGen.hasValue()) {
        // The transform function threw an exception and was not a coroutine.
        // We will close the output receiver.
        co_return outputGen.template hasException<OnClosedException>()
            ? CloseResult()
            : CloseResult(std::move(outputGen.exception()));
      }
      while (true) {
        auto outputResult =
            co_await folly::coro::co_awaitTry(outputGen->next());
        if (!outputResult.hasValue() && !outputResult.hasException()) {
          break;
        }
        if (cancelToken.isCancellationRequested()) {
          co_return CloseResult();
        }
        if (outputResult.hasValue()) {
          sender_->senderPush(std::move(outputResult.value()));
        } else {
          // The transform coroutine threw an exception. We will close the
          // output receiver.
          co_return outputResult.template hasException<OnClosedException>()
              ? CloseResult()
              : CloseResult(std::move(outputResult.exception()));
        }
      }
      if (inputClosed) {
        // The input receiver was closed, and the transform function did not
        // explicitly close the output receiver. We will therefore close it
        // anyway, as it does not make sense to keep it open when no future
        // values will arrive.
        co_return CloseResult();
      }
    }
    co_return std::nullopt;
  }

  /**
   * Process cancellation for the input receiver.
   */
  virtual folly::coro::Task<void> processReceiverCancelled(
      CloseResult closeResult, bool noRetriesAllowed = false) = 0;

  /**
   * Process cancellation for the sender.
   */
  void processSenderCancelled() {
    CHECK_EQ(getSenderState(), ChannelState::CancellationTriggered);
    sender_ = nullptr;
    if (getReceiverState() == ChannelState::Active) {
      receiver_->receiverCancel();
    }
    maybeDelete();
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * receiver and the sender.
   */
  void maybeDelete() {
    if (getReceiverState() == ChannelState::CancellationProcessed &&
        getSenderState() == ChannelState::CancellationProcessed) {
      delete this;
    }
  }

  ChannelState getReceiverState() {
    return detail::getReceiverState(receiver_.get());
  }

  ChannelState getSenderState() {
    return detail::getSenderState(sender_.get());
  }

  ChannelBridgePtr<TInputValue> receiver_;
  ChannelBridgePtr<TOutputValue> sender_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  TransformValueFunc transformValue_;
};

/**
 * This subclass is used for simple transformations triggered by a call to the
 * transform function (i.e. with a single input receiver and no initialization
 * function).
 */
template <
    typename TInputValue,
    typename TOutputValue,
    typename TransformValueFunc>
class TransformProcessor : public TransformProcessorBase<
                               TInputValue,
                               TOutputValue,
                               TransformValueFunc> {
 public:
  using Base =
      TransformProcessorBase<TInputValue, TOutputValue, TransformValueFunc>;
  using Base::Base;

 private:
  /**
   * Process cancellation for the input receiver.
   */
  folly::coro::Task<void> processReceiverCancelled(
      CloseResult closeResult, bool /* noRetriesAllowed */) override {
    CHECK_EQ(this->getReceiverState(), ChannelState::CancellationTriggered);
    this->receiver_ = nullptr;
    if (this->getSenderState() == ChannelState::Active) {
      if (closeResult.exception.has_value()) {
        this->sender_->senderClose(std::move(closeResult.exception.value()));
      } else {
        this->sender_->senderClose();
      }
    }
    this->maybeDelete();
    co_return;
  }
};

/**
 * This subclass is used for resumable transformations triggered by a call to
 * the resumableTransform function.
 */
template <
    typename TInputValue,
    typename TOutputValue,
    typename InitializeTransformFunc,
    typename TransformValueFunc>
class ResumableTransformProcessor : public TransformProcessorBase<
                                        TInputValue,
                                        TOutputValue,
                                        TransformValueFunc> {
 public:
  using Base =
      TransformProcessorBase<TInputValue, TOutputValue, TransformValueFunc>;

  ResumableTransformProcessor(
      Sender<TOutputValue> sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      InitializeTransformFunc initializeTransform,
      TransformValueFunc transformValue)
      : Base(std::move(sender), std::move(executor), std::move(transformValue)),
        initializeTransform_(std::move(initializeTransform)) {}

  void initialize() {
    this->executor_->add([=]() {
      runOperationWithSenderCancellation(
          this->executor_,
          this->sender_,
          false /* currentlyWaiting */,
          this /* channelCallbackToRestore */,
          initializeImpl());
    });
  }

 private:
  /**
   * Runs the user-provided initialization function to get a set of initial
   * values and a receiver to continue transforming. This is called when the
   * resumableTransform is created, and again whenever the previous input
   * receiver closed without an exception.
   */
  folly::coro::Task<void> initializeImpl() {
    auto cancelToken = co_await folly::coro::co_current_cancellation_token;
    auto initializeResult =
        co_await folly::coro::co_awaitTry(initializeTransform_());
    if (initializeResult.hasException()) {
      co_await processReceiverCancelled(
          initializeResult.template hasException<OnClosedException>()
              ? CloseResult()
              : CloseResult(std::move(initializeResult.exception())),
          true /* noRetriesAllowed */);
      co_return;
    }
    auto [initialValues, inputReceiver] = std::move(initializeResult.value());
    CHECK(inputReceiver)
        << "The initialize function of a resumableTransform returned an "
           "invalid receiver.";
    if (cancelToken.isCancellationRequested()) {
      // The sender was closed before we finished running the initialization
      // function. We will ignore the results from that function and proceed
      // to process cancellation for the receiver.
      co_await processReceiverCancelled(
          CloseResult(), true /* noRetriesAllowed */);
      co_return;
    }
    for (auto& initialValue : initialValues) {
      this->sender_->senderPush(std::move(initialValue));
    }
    co_await this->startTransformImpl(std::move(inputReceiver));
  }

  /**
   * Process cancellation for the input receiver.
   */
  folly::coro::Task<void> processReceiverCancelled(
      CloseResult closeResult, bool noRetriesAllowed) override {
    if (this->receiver_) {
      CHECK_EQ(this->getReceiverState(), ChannelState::CancellationTriggered);
      this->receiver_ = nullptr;
    }
    auto cancelToken = co_await folly::coro::co_current_cancellation_token;
    if (this->getSenderState() == ChannelState::Active &&
        !cancelToken.isCancellationRequested()) {
      if (closeResult.exception.has_value()) {
        // We were closed with an exception. We will close the sender with that
        // exception.
        this->sender_->senderClose(std::move(closeResult.exception.value()));
      } else if (noRetriesAllowed) {
        // We were closed without an exception, but no retries are allowed. This
        // means that the user-provided initialization function threw an
        // OnClosedException.
        this->sender_->senderClose();
      } else {
        // We were not closed with an exception. We will re-run the user's
        // initialization function and resume the resumableTransform.
        co_await initializeImpl();
        co_return;
      }
    }
    this->maybeDelete();
  }

  InitializeTransformFunc initializeTransform_;
};
} // namespace detail

template <
    typename TReceiver,
    typename TransformValueFunc,
    typename TInputValue,
    typename TOutputValue>
Receiver<TOutputValue> transform(
    TReceiver inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    TransformValueFunc transformValue) {
  auto [outputReceiver, outputSender] = Channel<TOutputValue>::create();
  using TProcessor =
      detail::TransformProcessor<TInputValue, TOutputValue, TransformValueFunc>;
  auto* processor = new TProcessor(
      std::move(outputSender), std::move(executor), std::move(transformValue));
  processor->startTransform(std::move(inputReceiver));
  return std::move(outputReceiver);
}

template <
    typename InitializeTransformFunc,
    typename TransformValueFunc,
    typename TReceiver,
    typename TInputValue,
    typename TOutputValue>
Receiver<TOutputValue> resumableTransform(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    InitializeTransformFunc initializeTransformFunc,
    TransformValueFunc transformValue) {
  auto [outputReceiver, outputSender] = Channel<TOutputValue>::create();
  using TProcessor = detail::ResumableTransformProcessor<
      TInputValue,
      TOutputValue,
      InitializeTransformFunc,
      TransformValueFunc>;
  auto* processor = new TProcessor(
      std::move(outputSender),
      executor,
      std::move(initializeTransformFunc),
      std::move(transformValue));
  processor->initialize();
  return std::move(outputReceiver);
}
} // namespace channels
} // namespace folly
