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

namespace folly {
namespace channels {

namespace detail {
template <typename TValue>
class IFanoutChannelProcessor;
}

/**
 * A fanout channel allows one to fan out updates from a single input receiver
 * to multiple output receivers.
 *
 * When a new output receiver is added, an optional function will be run that
 * computes a set of initial values. These initial values will only be sent to
 * the new receiver.
 *
 * Example:
 *
 *  // Function that returns a receiver:
 *  Receiver<int> getInputReceiver();
 *
 *  // Function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  auto fanoutChannel = createFanoutChannel(getReceiver(), getExecutor());
 *  auto receiver1 = fanoutChannel.newReceiver();
 *  auto receiver2 = fanoutChannel.newReceiver();
 *  auto receiver3 = fanoutChannel.newReceiver([]{ return {1, 2, 3}; });
 *  std::move(fanoutChannel).close();
 */
template <typename TValue>
class FanoutChannel {
  using TProcessor = detail::IFanoutChannelProcessor<TValue>;

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
   * receiver. If a getInitialValues parameter is provided, it will be executed
   * to determine the set of initial values that will (only) go to the new input
   * receiver.
   */
  Receiver<TValue> getNewReceiver(
      folly::Function<std::vector<TValue>()> getInitialValues = {});

  /**
   * Returns whether this fanout channel has any output receivers.
   */
  bool anyReceivers();

  /**
   * Closes the fanout channel.
   */
  void close(std::optional<folly::exception_wrapper> ex = std::nullopt) &&;

 private:
  TProcessor* processor_;
};

/**
 * Creates a new fanout channel that fans out updates from an input receiver.
 */
template <typename TReceiver, typename TValue = typename TReceiver::ValueType>
FanoutChannel<TValue> createFanoutChannel(
    TReceiver inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/FanoutChannel-inl.h>
