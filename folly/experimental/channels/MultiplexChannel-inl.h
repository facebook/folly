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

#include <folly/experimental/channels/MultiplexChannel.h>
#include <folly/experimental/channels/RateLimiter.h>
#include <folly/experimental/channels/detail/Utility.h>
#include <folly/experimental/coro/FutureUtil.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Promise.h>

namespace folly {
namespace channels {

template <typename MultiplexerType>
MultiplexedSubscriptions<MultiplexerType>::MultiplexedSubscriptions(
    SubscriptionMap& subscriptions)
    : subscriptions_(subscriptions) {}

template <typename MultiplexerType>
bool MultiplexedSubscriptions<MultiplexerType>::hasSubscription(
    const MultiplexedSubscriptions::KeyType& key) {
  return subscriptions_.contains(key) && !closedSubscriptionKeys_.contains(key);
}

template <typename MultiplexerType>
typename MultiplexedSubscriptions<MultiplexerType>::KeyContextType&
MultiplexedSubscriptions<MultiplexerType>::getKeyContext(
    const MultiplexedSubscriptions::KeyType& key) {
  ensureKeyExists(key);
  return std::get<KeyContextType>(subscriptions_.at(key));
}

template <typename MultiplexerType>
template <typename U>
void MultiplexedSubscriptions<MultiplexerType>::write(
    const MultiplexedSubscriptions::KeyType& key, U&& value) {
  ensureKeyExists(key);
  auto& sender =
      std::get<FanoutSender<OutputValueType>>(subscriptions_.at(key));
  sender.write(std::forward<U>(value));
}

template <typename MultiplexerType>
void MultiplexedSubscriptions<MultiplexerType>::close(
    const MultiplexedSubscriptions::KeyType& key, exception_wrapper ex) {
  ensureKeyExists(key);
  auto& sender =
      std::get<FanoutSender<OutputValueType>>(subscriptions_.at(key));
  if (ex) {
    std::move(sender).close(std::move(ex));
  } else {
    std::move(sender).close();
  }
  // We do not erase from the subscriptions_ map yet, because we do not want
  // to invalidate the view returned by getSubscriptionKeys.
  closedSubscriptionKeys_.insert(key);
}

template <typename MultiplexerType>
void MultiplexedSubscriptions<MultiplexerType>::ensureKeyExists(
    const KeyType& key) {
  if (!subscriptions_.contains(key) || closedSubscriptionKeys_.contains(key)) {
    throw std::runtime_error("Subscription with the given key does not exist.");
  }
}

template <typename MultiplexerType>
MultiplexChannel<MultiplexerType>::MultiplexChannel(TProcessor* processor)
    : processor_(processor) {}

template <typename MultiplexerType>
MultiplexChannel<MultiplexerType>::MultiplexChannel(
    MultiplexChannel&& other) noexcept
    : processor_(std::exchange(other.processor_, nullptr)) {}

template <typename MultiplexerType>
MultiplexChannel<MultiplexerType>& MultiplexChannel<MultiplexerType>::operator=(
    MultiplexChannel&& other) noexcept {
  if (&other == this) {
    return *this;
  }
  if (processor_) {
    std::move(*this).close();
  }
  processor_ = std::exchange(other.processor_, nullptr);
  return *this;
}

template <typename MultiplexerType>
MultiplexChannel<MultiplexerType>::~MultiplexChannel() {
  if (processor_ != nullptr) {
    std::move(*this).close(exception_wrapper());
  }
}

template <typename MultiplexerType>
MultiplexChannel<MultiplexerType>::operator bool() const {
  return processor_;
}

template <typename MultiplexerType>
Receiver<typename MultiplexChannel<MultiplexerType>::OutputValueType>
MultiplexChannel<MultiplexerType>::subscribe(
    KeyType key, SubscriptionArgType subscriptionArg) {
  return processor_->subscribe(std::move(key), std::move(subscriptionArg));
}

template <typename MultiplexerType>
folly::coro::Task<std::vector<std::pair<
    typename MultiplexChannel<MultiplexerType>::KeyType,
    typename MultiplexChannel<MultiplexerType>::KeyContextType>>>
MultiplexChannel<MultiplexerType>::clearUnusedSubscriptions() {
  co_return co_await processor_->clearUnusedSubscriptions();
}

template <typename MultiplexerType>
bool MultiplexChannel<MultiplexerType>::anySubscribers() const {
  return processor_->anySubscribers();
}

template <typename MultiplexerType>
void MultiplexChannel<MultiplexerType>::close(exception_wrapper ex) && {
  processor_->destroyHandle(
      ex ? detail::CloseResult(std::move(ex)) : detail::CloseResult());
  processor_ = nullptr;
}

namespace detail {

/**
 * This object fans out values from the input receiver to all output receivers.
 * The lifetime of this object is described by the following state machine.
 *
 * The input receiver can be in one of three conceptual states: Active,
 * CancellationTriggered, or CancellationProcessed (removed). When the input
 * receiver reaches the CancellationProcessed state AND the user's
 * MultiplexChannel object is deleted, this object is deleted.
 *
 * When an input receiver receives a value indicating that the channel has
 * been closed, the state of the input receiver transitions from Active directly
 * to CancellationProcessed (and this object will be deleted once the user
 * destroys their MultiplexChannel object).
 *
 * When the user destroys their MultiplexChannel object, the state of the input
 * receiver transitions from Active to CancellationTriggered. This object will
 * then be deleted once the input receiver transitions to the
 * CancellationProcessed state.
 */
template <typename MultiplexerType>
class MultiplexChannelProcessor : public IChannelCallback {
 private:
  using KeyType = typename detail::MultiplexerTraits<MultiplexerType>::KeyType;
  using KeyContextType =
      typename detail::MultiplexerTraits<MultiplexerType>::KeyContextType;
  using SubscriptionArgType =
      typename detail::MultiplexerTraits<MultiplexerType>::SubscriptionArgType;
  using InputValueType =
      typename detail::MultiplexerTraits<MultiplexerType>::InputValueType;
  using OutputValueType =
      typename detail::MultiplexerTraits<MultiplexerType>::OutputValueType;

 public:
  explicit MultiplexChannelProcessor(MultiplexerType multiplexer)
      : multiplexer_(std::move(multiplexer)),
        totalSubscriptions_(0),
        pendingAsyncCalls_(0) {}

  /**
   * Starts multiplexing values from the input receiver to to one or more keyed
   * subscriptions.
   */
  void start(Receiver<InputValueType> inputReceiver) {
    executeWithMutexWhenReady(
        [this,
         inputReceiver =
             std::move(inputReceiver)]() mutable -> folly::coro::Task<void> {
          co_await processStart(std::move(inputReceiver));
        });
  }

  Receiver<OutputValueType> subscribe(
      KeyType key, SubscriptionArgType subscriptionArg) {
    auto [receiver, sender] = Channel<OutputValueType>::create();
    totalSubscriptions_.fetch_add(1);
    executeWithMutexWhenReady(
        [this,
         key = std::move(key),
         subscriptionArg = std::move(subscriptionArg),
         sender_2 = std::move(sender)]() mutable -> folly::coro::Task<void> {
          co_await processNewSubscription(
              std::move(key), std::move(subscriptionArg), std::move(sender_2));
        });
    return std::move(receiver);
  }

  folly::coro::Task<std::vector<std::pair<KeyType, KeyContextType>>>
  clearUnusedSubscriptions() {
    auto [promise, future] = folly::coro::makePromiseContract<
        std::vector<std::pair<KeyType, KeyContextType>>>();
    executeWithMutexWhenReady(
        [this,
         promise_2 = std::move(promise)]() mutable -> folly::coro::Task<void> {
          co_await processClearUnusedSubscriptions(std::move(promise_2));
        });
    return folly::coro::toTask(std::move(future));
  }

  bool anySubscribers() { return totalSubscriptions_.load() > 0; }

  /**
   * This is called when the user's MultiplexChannel object has been destroyed.
   */
  void destroyHandle(CloseResult closeResult) {
    executeWithMutexWhenReady(
        [this,
         closeResult =
             std::move(closeResult)]() mutable -> folly::coro::Task<void> {
          co_await processHandleDestroyed(std::move(closeResult));
        });
  }

 private:
  /**
   * Called when the input receiver has an update.
   */
  void consume(ChannelBridgeBase*) override {
    executeWithMutexWhenReady([this]() -> folly::coro::Task<void> {
      co_await processAllAvailableValues();
    });
  }

  /**
   * Called after we cancelled this input receiver, due to the destruction of
   * the handle.
   */
  void canceled(ChannelBridgeBase*) override {
    executeWithMutexWhenReady([this]() -> folly::coro::Task<void> {
      auto closeResult = CloseResult(); // Declaring first due to GCC bug
      co_await processReceiverCancelled(std::move(closeResult));
    });
  }

  folly::coro::Task<void> processStart(Receiver<InputValueType> inputReceiver) {
    auto [unbufferedInputReceiver, buffer] =
        detail::receiverUnbuffer(std::move(inputReceiver));
    receiver_ = std::move(unbufferedInputReceiver);

    // Start processing new values that come in from the input receiver.
    co_await processAllAvailableValues(std::move(buffer));
  }

  /**
   * Processes all available values from the input receiver (starting from the
   * provided buffer, if present).
   *
   * If an value was received indicating that the input channel has been closed
   * (or if the transform function indicated that channel should be closed), we
   * will process cancellation for the input receiver.
   */
  folly::coro::Task<void> processAllAvailableValues(
      std::optional<ReceiverQueue<InputValueType>> buffer = std::nullopt) {
    CHECK_NE(getReceiverState(), ChannelState::CancellationProcessed);
    auto closeResult = receiver_->isReceiverCancelled()
        ? CloseResult()
        : (buffer.has_value()
               ? co_await processValues(std::move(buffer.value()))
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
      closeResult = co_await processValues(std::move(values));
    }
    if (closeResult.has_value()) {
      // The receiver received a value indicating channel closure.
      receiver_->receiverCancel();
      co_await processReceiverCancelled(std::move(closeResult.value()));
    }
  }

  /**
   * Processes the given set of values for the input receiver. Returns a
   * CloseResult if channel was closed, so the caller can stop attempting to
   * process values from it.
   */
  folly::coro::Task<std::optional<CloseResult>> processValues(
      ReceiverQueue<InputValueType> values) {
    while (!values.empty()) {
      auto inputResult = std::move(values.front());
      values.pop();
      bool inputClosed = !inputResult.hasValue();
      auto subscriptions =
          MultiplexedSubscriptions<MultiplexerType>(subscriptions_);
      if (inputClosed && !inputResult.hasException()) {
        // The input channel was closed. We will send an OnClosedException to
        // onInputValue.
        inputResult = Try<InputValueType>(
            folly::make_exception_wrapper<OnClosedException>());
      }

      // Process the input value by calling onInputValue on the user's
      // multiplexer.
      auto onInputValueResult = co_await folly::coro::co_awaitTry(
          multiplexer_.onInputValue(std::move(inputResult), subscriptions));

      // If the user closed any subscriptions, erase them from the subscriptions
      // map.
      for (const auto& key : subscriptions.closedSubscriptionKeys_) {
        subscriptions_.erase(key);
      }
      if (!subscriptions.closedSubscriptionKeys_.empty()) {
        totalSubscriptions_.fetch_sub(
            subscriptions.closedSubscriptionKeys_.size());
        subscriptions.closedSubscriptionKeys_.clear();
      }

      if (inputClosed && onInputValueResult.hasValue()) {
        // The input channel was closed, but the onInputValue function did not
        // throw. We need to close all output receivers.
        onInputValueResult =
            Try<void>(folly::make_exception_wrapper<OnClosedException>());
      }
      if (!onInputValueResult.hasValue()) {
        co_return onInputValueResult.template hasException<OnClosedException>()
            ? CloseResult()
            : CloseResult(std::move(onInputValueResult.exception()));
      }
    }
    co_return std::nullopt;
  }

  /**
   * Processes the cancellation of the input receiver. We will close all
   * senders with the exception received from the input receiver (if any).
   */
  folly::coro::Task<void> processReceiverCancelled(CloseResult closeResult) {
    CHECK_EQ(getReceiverState(), ChannelState::CancellationTriggered);
    receiver_ = nullptr;
    closeAllSubscriptions(std::move(closeResult));
    co_return;
  }

  folly::coro::Task<void> processNewSubscription(
      KeyType key,
      SubscriptionArgType subscriptionArg,
      Sender<OutputValueType> newSender) {
    if (subscriptions_.contains(key)) {
      // We already had a subscription for this key.
      totalSubscriptions_.fetch_sub(1);
    }
    auto& [sender, context] = subscriptions_[key];
    auto initialValues =
        co_await folly::coro::co_awaitTry(multiplexer_.onNewSubscription(
            key, context, std::move(subscriptionArg)));
    if (initialValues.hasException()) {
      std::move(newSender).close(initialValues.exception());
      co_return;
    }
    for (auto& initialValue : initialValues.value()) {
      newSender.write(std::move(initialValue));
    }
    sender.subscribe(std::move(newSender));
  }

  folly::coro::Task<void> processClearUnusedSubscriptions(
      folly::coro::Promise<std::vector<std::pair<KeyType, KeyContextType>>>
          promise) {
    auto clearedSubscriptions =
        std::vector<std::pair<KeyType, KeyContextType>>();
    size_t subscriptionsToRemove = 0;
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
      auto& sender = std::get<FanoutSender<OutputValueType>>(it->second);
      if (!sender.anySubscribers()) {
        clearedSubscriptions.push_back(std::make_pair(
            it->first, std::move(std::get<KeyContextType>(it->second))));
        it = subscriptions_.erase(it);
        subscriptionsToRemove++;
      } else {
        ++it;
      }
    }

    totalSubscriptions_.fetch_sub(subscriptionsToRemove);
    promise.setValue(std::move(clearedSubscriptions));
    co_return;
  }

  /**
   * Processes the destruction of the user's MultiplexChannel object.  We will
   * cancel the receiver and trigger cancellation for all senders not already
   * cancelled.
   */
  folly::coro::Task<void> processHandleDestroyed(CloseResult closeResult) {
    handleDeleted_ = true;
    if (getReceiverState() == ChannelState::Active) {
      receiver_->receiverCancel();
    }
    closeAllSubscriptions(std::move(closeResult));
    co_return;
  }

  /**
   * Deletes this object if we have already processed cancellation for the
   * receiver and all senders, and if the user's MultiplexChannel object was
   * destroyed.
   */
  void maybeDelete(std::unique_lock<folly::coro::Mutex>& lock) {
    if (getReceiverState() == ChannelState::CancellationProcessed &&
        handleDeleted_ && pendingAsyncCalls_ == 0) {
      lock.unlock();
      delete this;
    }
  }

  void executeWithMutexWhenReady(
      folly::Function<folly::coro::Task<void>()> func) {
    pendingAsyncCalls_++;
    auto rateLimiter = multiplexer_.getRateLimiter();
    if (rateLimiter != nullptr) {
      rateLimiter->executeWhenReady(
          [this, func = std::move(func), executor = multiplexer_.getExecutor()](
              std::unique_ptr<RateLimiter::Token> token) mutable {
            folly::coro::co_invoke(
                [this,
                 token = std::move(token),
                 func = std::move(func)]() mutable -> folly::coro::Task<void> {
                  auto lock = co_await mutex_.co_scoped_lock();
                  co_await func();
                  pendingAsyncCalls_--;
                  maybeDelete(lock);
                })
                .scheduleOn(executor)
                .start();
          },
          multiplexer_.getExecutor());
    } else {
      folly::coro::co_invoke(
          [this, func = std::move(func)]() mutable -> folly::coro::Task<void> {
            auto lock = co_await mutex_.co_scoped_lock();
            co_await func();
            pendingAsyncCalls_--;
            maybeDelete(lock);
          })
          .scheduleOn(multiplexer_.getExecutor())
          .start();
    }
  }

  ChannelState getReceiverState() {
    return detail::getReceiverState(receiver_.get());
  }

  void closeAllSubscriptions(CloseResult closeResult) {
    for (auto& [key, subscription] : subscriptions_) {
      auto& sender = std::get<FanoutSender<OutputValueType>>(subscription);
      std::move(sender).close(
          closeResult.exception.has_value() ? closeResult.exception.value()
                                            : exception_wrapper());
    }
    totalSubscriptions_.fetch_sub(subscriptions_.size());
    subscriptions_.clear();
  }

  using SubscriptionMap = folly::F14FastMap<
      KeyType,
      std::tuple<FanoutSender<OutputValueType>, KeyContextType>>;

  coro::Mutex mutex_;

  // The above coro mutex must be acquired before accessing this state.
  ChannelBridgePtr<InputValueType> receiver_;
  SubscriptionMap subscriptions_;
  bool handleDeleted_{false};

  // The above coro mutex does not need to be acquired before accessing this
  // state.
  MultiplexerType multiplexer_;
  std::atomic<uint64_t> totalSubscriptions_; // Includes pending subscriptions
  std::atomic<uint64_t> pendingAsyncCalls_;
};
} // namespace detail

template <typename MultiplexerType, typename InputReceiverType>
MultiplexChannel<MultiplexerType> createMultiplexChannel(
    MultiplexerType multiplexer, InputReceiverType inputReceiver) {
  auto* processor = new detail::MultiplexChannelProcessor<MultiplexerType>(
      std::move(multiplexer));
  processor->start(std::move(inputReceiver));
  return MultiplexChannel<MultiplexerType>(processor);
}
} // namespace channels
} // namespace folly
