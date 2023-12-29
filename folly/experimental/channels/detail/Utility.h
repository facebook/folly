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

#include <optional>
#include <folly/ExceptionWrapper.h>
#include <folly/Function.h>
#include <folly/ScopeGuard.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/RateLimiter.h>
#include <folly/experimental/coro/Promise.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {
namespace detail {

struct CloseResult {
  CloseResult() {}

  explicit CloseResult(exception_wrapper _exception)
      : exception(std::move(_exception)) {}

  std::optional<exception_wrapper> exception;
};

enum class ChannelState {
  Active,
  CancellationTriggered,
  CancellationProcessed
};

template <typename TSender>
ChannelState getSenderState(TSender* sender) {
  if (sender == nullptr) {
    return ChannelState::CancellationProcessed;
  } else if (sender->isSenderClosed()) {
    return ChannelState::CancellationTriggered;
  } else {
    return ChannelState::Active;
  }
}

template <typename TReceiver>
ChannelState getReceiverState(TReceiver* receiver) {
  if (receiver == nullptr) {
    return ChannelState::CancellationProcessed;
  } else if (receiver->isReceiverCancelled()) {
    return ChannelState::CancellationTriggered;
  } else {
    return ChannelState::Active;
  }
}

inline std::ostream& operator<<(std::ostream& os, ChannelState state) {
  switch (state) {
    case ChannelState::Active:
      return os << "Active";
    case ChannelState::CancellationTriggered:
      return os << "CancellationTriggered";
    case ChannelState::CancellationProcessed:
      return os << "CancellationProcessed";
    default:
      return os << "Should never be hit";
  }
}

/**
 * A cancellation callback that wraps an existing channel callback. When the
 * callback is fired, this object will trigger cancellation on its cancellation
 * source (in addition to firing the wrapped callback).
 */
template <typename TSender>
class SenderCancellationCallback : public IChannelCallback {
 public:
  explicit SenderCancellationCallback(
      TSender& sender,
      folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
      IChannelCallback* channelCallback)
      : sender_(sender),
        executor_(std::move(executor)),
        channelCallback_(channelCallback),
        callbackToFire_(folly::coro::makePromiseContract<CallbackToFire>()) {
    if (channelCallback_ == nullptr) {
      // The sender was already canceled runOperationWithSenderCancellation was
      // even called. This means the cancelled callback already was fired, so
      // we will not set the callback to fire here.
      cancelSource_.requestCancellation();
      return;
    }
    CHECK(sender_);
    if (!sender_->senderWait(this)) {
      // The sender was cancelled after runOperationWithSenderCancellation was
      // called, but before we had a chance to start the operation. This means
      // that the cancelled callback was never called. We will therefore set it
      // to fire here, when the operation is complete.
      cancelSource_.requestCancellation();
      callbackToFire_.first.setValue(CallbackToFire::Consume);
    }
  }

  folly::coro::Task<void> onTaskCompleted() {
    if (!channelCallback_) {
      co_return;
    }
    auto callbackToFire = std::optional<CallbackToFire>();
    bool promiseSet = false;
    if (callbackToFire_.second.isReady()) {
      // The callback was fired.
      promiseSet = true;
      callbackToFire = co_await std::move(callbackToFire_.second);
    } else {
      // The callback has not yet been fired.
      if (!sender_->cancelSenderWait()) {
        // The sender has been cancelled, but the callback has not been called
        // yet. Wait for the callback to be called.
        promiseSet = true;
        callbackToFire = co_await std::move(callbackToFire_.second);
      } else if (!sender_->senderWait(channelCallback_)) {
        // The sender was cancelled between the call to cancelSenderWait and
        // the call to senderWait. This means that the cancelled callback was
        // never called. We will therefore set it to fire here.
        callbackToFire = CallbackToFire::Consume;
      }
    }
    if (!promiseSet) {
      // Set a default value here, so we don't need to waste time constructing a
      // broken promise exception when the promise is destructed. This value
      // will not be read.
      callbackToFire_.first.setValue(CallbackToFire::Consume);
    }
    if (callbackToFire.has_value()) {
      switch (callbackToFire.value()) {
        case CallbackToFire::Consume:
          channelCallback_->consume(sender_.get());
          co_return;
        case CallbackToFire::Canceled:
          channelCallback_->canceled(sender_.get());
          co_return;
      }
    }
    // The sender has not yet been cancelled, and we are now back in the state
    // where the sender is waiting on the user-provided callback. We are done.
  }

  /**
   * Returns a cancellation token that will trigger when the sender
   */
  folly::CancellationToken getCancellationToken() {
    return cancelSource_.getToken();
  }

  /**
   * Requests cancellation, and triggers the consume function on the callback
   * if the callback was not previously triggered.
   */
  void consume(ChannelBridgeBase*) override {
    cancelSource_.requestCancellation();
    executor_->add([=, this]() {
      CHECK(!callbackToFire_.second.isReady());
      callbackToFire_.first.setValue(CallbackToFire::Consume);
    });
  }

  /**
   * Requests cancellation, and triggers the canceled function on the callback
   * if the callback was not previously triggered.
   */
  void canceled(ChannelBridgeBase*) override {
    cancelSource_.requestCancellation();
    executor_->add([=, this]() {
      CHECK(!callbackToFire_.second.isReady());
      callbackToFire_.first.setValue(CallbackToFire::Canceled);
    });
  }

 private:
  enum class CallbackToFire { Consume, Canceled };

  TSender& sender_;
  folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
  IChannelCallback* channelCallback_;
  folly::CancellationSource cancelSource_;
  std::pair<
      folly::coro::Promise<CallbackToFire>,
      folly::coro::Future<CallbackToFire>>
      callbackToFire_;
};

/**
 * Any object that produces an output receiver (transform, merge,
 * MergeChannel, etc) will listen for a cancellation signal from that output
 * receiver. Once the consumer of the output receiver stops consuming, a
 * callback will be called that triggers these objects to start cleaning
 * themselves up (and eventually destroy themselves).
 *
 * However, when one of these objects decides to run a user coroutine, they
 * would like that user coroutine to be able to get notified when that
 * cancellation signal is received. That allows the coroutine to stop any
 * long-running operations quickly, rather than running a long time when the
 * consumer of the output receiver no longer cares about the result.
 *
 * This function enables that behavior. It will run the provided operation
 * coroutine. While that coroutine is running, it will listen to cancellation
 * events from the output receiver (through its sender). If it receives a
 * cancellation signal from the sender, it will trigger cancellation of the
 * operation coroutine.
 *
 * Once the coroutine finishes, it will then call the given channel callback
 * to notify it of the cancellation event (the same way that callback would
 * have been notified if no coroutine had been started). It will also resume
 * waiting on the channel callback.
 *
 * @param executor: The executor to run the coroutine on.
 *
 * @param sender: The sender to use to listen for cancellation. If this is
 * null, we will assume that cancellation already occurred.
 *
 * @param alreadyStartedWaiting: Whether or not the caller already started
 * listening for a cancellation signal from the output receiver. If so, this
 * function will temporarily stop waiting with that callback (so it can listen
 * for the cancellation signal to stop the coroutine).
 *
 * @param channelCallbackToRestore: The channel callback to restore once the
 *  coroutine operation is complete.
 *
 * @param operation: The operation to run.
 *
 * @param token: The rate limiter token for this operation.
 */
template <typename TSender>
void runOperationWithSenderCancellation(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    TSender& sender,
    bool alreadyStartedWaiting,
    IChannelCallback* channelCallbackToRestore,
    folly::coro::Task<void> operation,
    std::unique_ptr<RateLimiter::Token> token) noexcept {
  if (alreadyStartedWaiting && (!sender || !sender->cancelSenderWait())) {
    // The output receiver was cancelled before starting this operation
    // (indicating that the channel callback already ran).
    channelCallbackToRestore = nullptr;
  }
  folly::coro::co_invoke(
      [&sender,
       executor,
       channelCallbackToRestore,
       token = std::move(token),
       operation = std::move(operation)]() mutable -> folly::coro::Task<void> {
        auto senderCancellationCallback = SenderCancellationCallback(
            sender, executor, channelCallbackToRestore);
        auto result =
            co_await folly::coro::co_awaitTry(folly::coro::co_withCancellation(
                senderCancellationCallback.getCancellationToken(),
                std::move(operation)));
        if (result.hasException()) {
          LOG(FATAL) << fmt::format(
              "Unexpected exception when running coroutine operation with "
              "sender cancellation: {}",
              result.exception().what());
        }
        co_await senderCancellationCallback.onTaskCompleted();
      })
      .scheduleOn(executor)
      .start();
}
} // namespace detail
} // namespace channels
} // namespace folly
