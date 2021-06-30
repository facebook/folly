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
template <typename TValue, typename TSubscriptionId>
class IMergeChannelProcessor;
}

/**
 * A merge channel allows one to merge multiple receivers into a single output
 * receiver. The set of receivers being merged can be changed at runtime. Each
 * receiver is added with a subscription ID, that can be used to remove the
 * receiver at a later point.
 *
 * Example:
 *
 *  // Example function that returns a receiver for a given entity:
 *  Receiver<int> subscribe(std::string entity);
 *
 *  // Example function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  auto [outputReceiver, mergeChannel]
 *      = createMergeChannel<int, std::string>(getExecutor());
 *  mergeChannel.addNewReceiver("abc", subscribe("abc"));
 *  mergeChannel.addNewReceiver("def", subscribe("def"));
 *  mergeChannel.removeReceiver("abc");
 *  std::move(mergeChannel).close();
 */
template <typename TValue, typename TSubscriptionId>
class MergeChannel {
  using TProcessor = detail::IMergeChannelProcessor<TValue, TSubscriptionId>;

 public:
  explicit MergeChannel(
      detail::IMergeChannelProcessor<TValue, TSubscriptionId>* processor);
  MergeChannel(MergeChannel&& other) noexcept;
  MergeChannel& operator=(MergeChannel&& other) noexcept;
  ~MergeChannel();

  /**
   * Returns whether this MergeChannel is a valid object.
   */
  explicit operator bool() const;

  /**
   * Adds a new receiver to be merged, along with a given subscription ID. If
   * the subscription ID matches the ID of an existing receiver, that existing
   * receiver is replaced with the new one (and changes to the old receiver will
   * no longer be merged). An added receiver can later be removed by passing the
   * subscription ID to removeReceiver.
   */
  template <typename TReceiver>
  void addNewReceiver(TSubscriptionId subscriptionId, TReceiver receiver);

  /**
   * Removes the receiver added with the given subscription ID. The receiver
   * will be asynchronously removed, so the consumer may still receive some
   * values from this receiver after this call.
   */
  void removeReceiver(TSubscriptionId subscriptionId);

  /**
   * Closes the merge channel.
   */
  void close(std::optional<folly::exception_wrapper> ex = std::nullopt) &&;

 private:
  TProcessor* processor_;
};

/**
 * Creates a new merge channel.
 *
 * @param executor: The SequencedExecutor to use for merging values.
 */
template <typename TValue, typename TSubscriptionId>
std::pair<Receiver<TValue>, MergeChannel<TValue, TSubscriptionId>>
createMergeChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/MergeChannel-inl.h>
