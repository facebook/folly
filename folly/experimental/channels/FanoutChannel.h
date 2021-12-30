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

#include <folly/executors/SequencedExecutor.h>
#include <folly/experimental/channels/Channel.h>

namespace folly {
namespace channels {

namespace detail {
template <typename ValueType, typename ContextType>
class IFanoutChannelProcessor;
}

template <typename TValue>
struct NoContext {
  void update(const TValue&) {}
};

/**
 * A fanout channel allows fanning out updates from a single input receiver
 * to multiple output receivers.
 *
 * When a new output receiver is added, an optional function will be run that
 * computes a set of initial values. These initial values will only be sent to
 * the new receiver.
 *
 * FanoutChannel allows specifying an optional context object. If specified, the
 * context object must have a void update function:
 *
 *   void update(const ValueType&);
 *
 * This update function will be called on every value from the input receiver.
 * The context will be passed to the getInitialUpdates argument to subscribe,
 * allowing for initial updates to depend on the context. This facilitates the
 * common pattern of letting new subscribers know where they are starting from.
 *
 * Example without context:
 *
 *   // Function that returns a receiver:
 *   Receiver<int> getInputReceiver();
 *
 *   // Function that returns an executor
 *   folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *   auto fanoutChannel = createFanoutChannel(getReceiver(), getExecutor());
 *   auto receiver1 = fanoutChannel.subscribe();
 *   auto receiver2 = fanoutChannel.subscribe();
 *   auto receiver3 = fanoutChannel.subscribe([]{ return {1, 2, 3}; });
 *
 * Example with context:
 *
 *   struct Context {
 *     int lastValue{-1};
 *
 *     void update(const int& value) {
 *       lastValue = value;
 *     }
 *   };
 *
 *   auto fanoutChannel =
 *       createFanoutChannel(getReceiver(), getExecutor(), Context());
 *   auto receiver1 = fanoutChannel.subscribe(
 *       [](const Context& context) { return {context.latestValue}; });
 *   auto receiver2 = fanoutChannel.subscribe(
 *       [](const Context& context) { return {context.latestValue}; });
 *   std::move(fanoutChannel).close();
 */
template <typename ValueType, typename ContextType = NoContext<ValueType>>
class FanoutChannel {
  using TProcessor = detail::IFanoutChannelProcessor<ValueType, ContextType>;

 public:
  explicit FanoutChannel(TProcessor* processor);
  FanoutChannel(FanoutChannel&& other) noexcept;
  FanoutChannel& operator=(FanoutChannel&& other) noexcept;
  ~FanoutChannel();

  /**
   * Returns whether this FanoutChannel is a valid object.
   */
  explicit operator bool() const;

  /**
   * Returns a new output receiver that will receive all values from the input
   * receiver.
   *
   * If a getInitialValues parameter is provided, it will be executed
   * to determine the set of initial values that will (only) go to the new input
   * receiver. Other functions on this class should not be called from within
   * getInitialValues, or a deadlock will occur.
   */
  Receiver<ValueType> subscribe(
      folly::Function<std::vector<ValueType>(const ContextType&)>
          getInitialValues = {});

  /**
   * Returns whether this fanout channel has any subscribers.
   */
  bool anySubscribers() const;

  /**
   * Closes all subscribers, without closing the fanout channel. New subscribers
   * can be added after this call.
   */
  void closeSubscribers(
      folly::exception_wrapper ex = folly::exception_wrapper());

  /**
   * Closes the fanout channel.
   */
  void close(folly::exception_wrapper ex = folly::exception_wrapper()) &&;

 private:
  TProcessor* processor_;
};

/**
 * Creates a new fanout channel that fans out updates from an input receiver.
 */
template <
    typename ReceiverType,
    typename ValueType = typename ReceiverType::ValueType,
    typename ContextType = NoContext<typename ReceiverType::ValueType>>
FanoutChannel<ValueType, ContextType> createFanoutChannel(
    ReceiverType inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    ContextType context = ContextType());
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/FanoutChannel-inl.h>
