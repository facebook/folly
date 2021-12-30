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

namespace folly {
namespace channels {

namespace detail {
template <typename KeyType, typename ValueType>
class IMergeChannelProcessor;
}

struct MergeChannelReceiverAdded {};
struct MergeChannelReceiverRemoved {};
struct MergeChannelReceiverClosed {
  folly::exception_wrapper exception;
};

template <typename KeyType, typename ValueType>
struct MergeChannelEvent {
  using EventType = std::variant<
      ValueType,
      MergeChannelReceiverAdded,
      MergeChannelReceiverRemoved,
      MergeChannelReceiverClosed>;

  KeyType key;
  EventType event;
};

/**
 * A merge channel allows one to merge multiple receivers into a single
 * output receiver. The set of receivers being merged can be changed at
 * runtime. Each receiver is added with a key that can be used to remove
 * the receiver at a later point.
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
 *      = createMergeChannel<std::string, int>(getExecutor());
 *  mergeChannel.addNewReceiver("abc", subscribe("abc"));
 *  mergeChannel.addNewReceiver("def", subscribe("def"));
 *  mergeChannel.removeReceiver("abc");
 *  std::move(mergeChannel).close();
 */
template <typename KeyType, typename ValueType>
class MergeChannel {
  using TProcessor = detail::IMergeChannelProcessor<KeyType, ValueType>;

 public:
  explicit MergeChannel(
      detail::IMergeChannelProcessor<KeyType, ValueType>* processor);
  MergeChannel(MergeChannel&& other) noexcept;
  MergeChannel& operator=(MergeChannel&& other) noexcept;
  ~MergeChannel();

  /**
   * Returns whether this MergeChannel is a valid object.
   */
  explicit operator bool() const;

  /**
   * Adds a new receiver to be merged, along with a given key. If the key
   * matches the key of an existing receiver, that existing receiver is replaced
   * with the new one (and updates from the old receiver will no longer be
   * merged). An added receiver can later be removed by passing the same key to
   * removeReceiver.
   */
  template <typename TReceiver>
  void addNewReceiver(KeyType key, TReceiver receiver);

  /**
   * Removes the receiver added with the given key. The receiver will be
   * asynchronously removed, so the consumer may still receive some values from
   * this receiver after this call.
   */
  void removeReceiver(KeyType key);

  /**
   * Returns a set of keys for receivers that are merged into this MergeChannel.
   */
  folly::F14FastSet<KeyType> getReceiverKeys();

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
template <typename KeyType, typename ValueType>
std::pair<
    Receiver<MergeChannelEvent<KeyType, ValueType>>,
    MergeChannel<KeyType, ValueType>>
createMergeChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/MergeChannel-inl.h>
