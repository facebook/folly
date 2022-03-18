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

#include <range/v3/view/map.hpp>
#include <folly/container/F14Map.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/FanoutSender.h>
#include <folly/experimental/channels/OnClosedException.h>
#include <folly/experimental/channels/detail/MultiplexerTraits.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {

template <typename MultiplexerType>
class MultiplexChannel;

/**
 * Creates a new multiplex channel that multiplexes updates from a single input
 * receiver to one or more keyed subscriptions.
 *
 * The creator of a multiplex channel must pass a Multiplexer class that
 * implements the following functions:
 *
 *   folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *   std::shared_ptr<RateLimiter> getRateLimiter(); // Can return nullptr
 *
 *   // This function is called for each call to subscribe on the multiplex
 *   // channel object. It returns a vector of output values that should be sent
 *   // directly to the new output receiver for that subscription.
 *   folly::coro::Task<std::vector<OutputValueType>> onNewSubscription(
 *       KeyType key,
 *       KeyContextType& keyContext,
 *       SubscriptionArgType subscriptionArg);
 *
 *   // This function is called with each value fromm the given input receiver.
 *   // This function sends any corresponding values to the relevant output
 *   // receivers, using the subscriptions parameter.
 *   folly::coro::Task<void> onInputValue(
 *       folly::Try<InputValueType> inputValue,
 *       MultiplexedSubscriptions<MultiplexerType>& subscriptions);
 *
 * Example:
 *
 *   struct InputValue {
 *     std::string key;
 *     int64_t value;
 *   };
 *
 *   struct NoContext {}
 *   struct NoSubscriptionArg {};
 *
 *   class Multiplexer {
 *    public:
 *     explicit Multiplexer(
 *         folly::Executor::KeepAlive<folly::SequencedExecutor> executor)
 *         : executor_(std::move(executor)) {}
 *
 *    folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor() {
 *      return executor_;
 *    }
 *
 *    std::shared_ptr<RateLimiter> getRateLimiter() {
 *      return nullptr; // No rate limiting
 *    }
 *
 *    folly::coro::Task<std::vector<OutputValueType>> onNewSubscription(
 *        std::string key,
 *        NoContext&,
 *        NoSubscriptionArg&) {
 *      co_return std::vector<int64_t>(); // No initial values
 *    }
 *
 *    folly::coro::Task<void> onInputValue(
 *        folly::Try<InputValue> inputValue,
 *        MultiplexedSubscriptions<Multiplexer>& subscriptions) {
 *      if (subscriptions.hasSubscription(inputValue->key)) {
 *        subscriptions.write(inputValue->key, inputValue->value);
 *      }
 *      co_return;
 *    }
 *
 *    private:
 *     folly::Executor::KeepAlive<folly::SequencedExecutor> executor_;
 *   }
 *
 *   // Function that returns a receiver:
 *   Receiver<InputValue> getInputReceiver();
 *
 *   // Function that returns an executor
 *   folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *   auto multiplexChannel = createMultiplexChannel(
 *       Multiplexer(getExecutor()),
 *       getInputReceiver());
 *
 *   auto receiver1a = multiplexChannel.subscribe("one");
 *   auto receiver1b = multiplexChannel.subscribe("one");
 *   auto receiver2a = multiplexChannel.subscribe("two");
 */
template <typename MultiplexerType, typename InputReceiverType>
MultiplexChannel<MultiplexerType> createMultiplexChannel(
    MultiplexerType multiplexer, InputReceiverType inputReceiver);

namespace detail {
template <typename MultiplexerType>
class MultiplexChannelProcessor;
} // namespace detail

/**
 * A multiplex channel allows multiplexing updates from a single input receiver
 * to one or more keyed subscriptions.
 */
template <typename MultiplexerType>
class MultiplexChannel {
  using TProcessor = detail::MultiplexChannelProcessor<MultiplexerType>;

  using KeyType = typename detail::MultiplexerTraits<MultiplexerType>::KeyType;
  using KeyContextType =
      typename detail::MultiplexerTraits<MultiplexerType>::KeyContextType;
  using SubscriptionArgType =
      typename detail::MultiplexerTraits<MultiplexerType>::SubscriptionArgType;
  using OutputValueType =
      typename detail::MultiplexerTraits<MultiplexerType>::OutputValueType;

 public:
  MultiplexChannel(MultiplexChannel&& other) noexcept;
  MultiplexChannel& operator=(MultiplexChannel&& other) noexcept;
  ~MultiplexChannel();

  /**
   * Returns whether this MultiplexChannel is a valid object. This will return
   * false if the object was moved from.
   */
  explicit operator bool() const;

  /**
   * Returns a new output receiver for the given key.
   */
  Receiver<OutputValueType> subscribe(
      KeyType key, SubscriptionArgType subscriptionArg);

  /**
   * Removes keys with no subscribers, and returns their contexts.
   */
  folly::coro::Task<std::vector<std::pair<KeyType, KeyContextType>>>
  clearUnusedSubscriptions();

  /**
   * Returns whether this multiplex channel has any subscribers for any keys.
   * Note that if any output receivers returned from subscribe have been
   * destroyed, such subscriptions will still be considered to exist until the
   * clearUnusedSubscriptions function is called.
   */
  bool anySubscribers() const;

  /**
   * Closes the multiplex channel.
   */
  void close(folly::exception_wrapper ex = folly::exception_wrapper()) &&;

 private:
  template <typename Multiplexer, typename InputValueType>
  friend MultiplexChannel<Multiplexer> createMultiplexChannel(
      Multiplexer, InputValueType);

  explicit MultiplexChannel(TProcessor* processor);

  TProcessor* processor_;
};

/**
 * A class that allows one to see which keys are subscribed, and to write
 * values for particular subscriptions. This is passed to the onInputValue
 * function of the user-provided Multiplexer class.
 */
template <typename MultiplexerType>
class MultiplexedSubscriptions {
 public:
  using KeyType = typename detail::MultiplexerTraits<MultiplexerType>::KeyType;
  using KeyContextType =
      typename detail::MultiplexerTraits<MultiplexerType>::KeyContextType;
  using OutputValueType =
      typename detail::MultiplexerTraits<MultiplexerType>::OutputValueType;

  friend class detail::MultiplexChannelProcessor<MultiplexerType>;

  /**
   * Returns whether or not a subscription exists for the given key.
   */
  bool hasSubscription(const KeyType& key);

  /**
   * Returns a reference to the context object for the given key.
   */
  KeyContextType& getKeyContext(const KeyType& key);

  /**
   * Sends a value to all subscribers of a given key.
   */
  template <typename U = OutputValueType>
  void write(const KeyType& key, U&& value);

  /**
   * Closes all subscribers for the given key.
   */
  void close(const KeyType& key, folly::exception_wrapper ex);

  /**
   * Returns a view containing a list of subscribed keys.
   */
  auto getAllSubscriptionKeys() { return subscriptions_ | ranges::views::keys; }

 private:
  using SubscriptionMap = folly::F14FastMap<
      KeyType,
      std::tuple<FanoutSender<OutputValueType>, KeyContextType>>&;

  explicit MultiplexedSubscriptions(SubscriptionMap& subscriptions);

  void ensureKeyExists(const KeyType& key);

  SubscriptionMap& subscriptions_;
  folly::F14FastSet<KeyType> closedSubscriptionKeys_;
};
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/MultiplexChannel-inl.h>
