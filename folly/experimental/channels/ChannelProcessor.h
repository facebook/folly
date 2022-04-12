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

#include <folly/Executor.h>
#include <folly/experimental/channels/RateLimiter.h>

namespace folly {
namespace channels {

namespace detail {
template <typename KeyType>
class ChannelProcessorImpl;
}

/**
 * This object allows for memory-efficient processing of values many channels.
 *
 * A channel is added with a unique key and a callback. The callback will be
 * called for every value pushed to the receiver.
 *
 * A resumable channel can also be added. A resumable channel involves two
 * callbacks. An initialization callback is called to get the receiver, and the
 * update callback is called on every update (as for a normal channel). The
 * update callback can throw a ReinitializeException at any time, which will
 * trigger the initialize callback to re-run.
 *
 * Values for a given channel are processed until one of the following occurs:
 *     1. The channel is closed
 *     2. The channel callback throws an OnClosedException
 *     3. The channel callback throws a folly::OperationCancelled exception.
 *     4. The channel is removed with a call to removeChannel.
 *
 * If a channel is removed with removeChannel, processing will eventually stop
 * for that channel. This will not necessarily happen immediately.
 *
 * If a channel is added for an already existing key, the previous channel for
 * that key will be removed and processing will eventually stop.
 *
 * Processing for all channels will run on the user-provided executor. For any
 * particular channel, all processing will happen sequentially. For any two
 * distinct channels, processing may happen in parallel (subject to any
 * constraints of the provided executor).
 */
template <typename KeyType>
class ChannelProcessor {
 public:
  explicit ChannelProcessor(
      std::unique_ptr<detail::ChannelProcessorImpl<KeyType>> impl);

  /**
   * Returns whether this ChannelProcessor is a valid object.
   */
  explicit operator bool() const;

  /**
   * Processes a channel with a given key and callback. For a receiver of type
   * Receiver<InputValueType>, the callback must accept a single parameter of
   * type folly::Try<InputValueType>, and return a void task. If the callback
   * throws an exception of type OperationCancelled or OnClosedException, the
   * channel will be removed. Any other exception thrown by the callback will
   * terminate the process.
   *
   * If there is an existing channel with the same key, it will be removed
   * before the new channel is added. The old channel's callback can check the
   * current cancellation token to see if it was removed while processing
   * values. See removeChannel for more details.
   *
   * Example:
   *
   *   // Example function that returns a receiver for a given entity
   *   Receiver<int> subscribe(const std::string& entity);
   *
   *   // Example function that returns an executor
   *   folly::Executor::KeepAlive<> getExecutor();
   *
   *   auto channelProcessor = createChannelProcessor<std::string>(
   *       getExecutor());
   *
   *   channelProcessor.addChannel(
   *       "abc",
   *       subscribe("abc"),
   *       [](folly::Try<int> value) -> folly::coro::Task<void> {
   *         LOG(INFO) << fmt::format("Received value {}", *value);
   *         co_return;
   *       });
   */
  template <typename ReceiverType, typename OnUpdateFunc>
  void addChannel(KeyType key, ReceiverType receiver, OnUpdateFunc onUpdate);

  /**
   * Processing a resumable channel involves two callbacks. The initialization
   * callback accepts an initialization argument of a user-defined type, and
   * must return a folly::coro::Task<Receiver<InputValueType>>. The onUpdate
   * callback accepts a folly::Try<InputValueType>, and returns a void task. The
   * onUpdate callback can throw a ReinitializeException<InitializeArg> at any
   * time, which will trigger the initialize function to be run again. In
   * addition, if either callback throws an exception of type OperationCancelled
   * or OnClosedException, the channel will be removed. Any other exception
   * thrown by either callback will terminate the process.
   *
   * If there is an existing channel with the same key, it will be removed
   * before the new channel is added. The old channel's callbacks can check the
   * current cancellation token to see if it was removed while processing
   * values. See removeChannel for more details.
   *
   * Example:
   *
   *   struct InitializeArg {
   *     std::string param;
   *   }
   *
   *   // Example function that returns a receiver for a given entity
   *   Receiver<int> subscribe(const InitializeArg& initializeArg);
   *
   *   // Example function that returns an executor
   *   folly::Executor::KeepAlive<> getExecutor();
   *
   *   auto channelProcessor = createChannelProcessor<std::string>(
   *       getExecutor());
   *
   *   channelProcessor.addResumableChannel(
   *       "abc",
   *       InitializeArg({"param"}),
   *       [](InitializeArg initializeArg) -> folly::coro::Task<Receiver<int>> {
   *         co_return subscribe(initializeArg);
   *       },
   *       [](folly::Try<int> value) -> folly::coro::Task<void> {
   *         if (*value == -1) {
   *           throw ReinitializeException(InitializeArg({"param"}));
   *         }
   *         LOG(INFO) << fmt::format("Received value {}", *value);
   *         co_return;
   *       });
   */
  template <
      typename InitializeArg,
      typename InitializeFunc,
      typename OnUpdateFunc>
  void addResumableChannel(
      KeyType key,
      InitializeArg initializeArg,
      InitializeFunc initialize,
      OnUpdateFunc onUpdate);

  /*
   * This is similar to addResumableChannel. However, it allows a user-provided
   * state object to be stored with the channel. That state object will be
   * passed to both callbacks, and will be destructed when the channel is
   * removed or closed.
   *
   * * Example:
   *
   *   struct InitializeArg {
   *     std::string param;
   *   }
   *
   *   struct State {
   *     int prevValue{-1};
   *   }
   *
   *   // Example function that returns a receiver for a given entity
   *   Receiver<int> subscribe(const InitializeArg& initializeArg);
   *
   *   // Example function that returns an executor
   *   folly::Executor::KeepAlive<> getExecutor();
   *
   *   auto channelProcessor = createChannelProcessor<std::string>(
   *       getExecutor());
   *
   *   channelProcessor.addResumableChannelWithState(
   *       "abc",
   *       InitializeArg({"param"}),
   *       [](InitializeArg initializeArg, State& state)
   *                        -> folly::coro::Task<Receiver<int>> {
   *         co_return subscribe(initializeArg);
   *       },
   *       [](folly::Try<int> value, State& state) -> folly::coro::Task<void> {
   *         if (*value == -1) {
   *           throw ReinitializeException(InitializeArg({"param"}));
   *         }
   *         LOG(INFO) << fmt::format(
   *             "Received value {}. Previous: {}.", *value, state.prevValue);
   *         state.prevValue = *value;
   *         co_return;
   *       },
   *       State());
   */
  template <
      typename InitializeArg,
      typename InitializeFunc,
      typename OnUpdateFunc,
      typename ChannelState>
  void addResumableChannelWithState(
      KeyType key,
      InitializeArg initializeArg,
      InitializeFunc initialize,
      OnUpdateFunc onUpdate,
      ChannelState channelState);

  /**
   * Removes the channel with the given key, if such a channel exists. The
   * channel will be asynchronously removed, so the channels' callback may
   * still receive some values after this call. The callback can detect whether
   * or not the channel was removed by examining its current cancellation token.
   */
  void removeChannel(const KeyType& keyType);

  /**
   * Closes all channels being processed, causing all processing to eventually
   * stop. Calling this function will make the object invalid.
   */
  void close() &&;

 private:
  std::unique_ptr<detail::ChannelProcessorImpl<KeyType>> impl_;
};

/**
 * Creates a new channel processor.
 */
template <typename KeyType>
ChannelProcessor<KeyType> createChannelProcessor(
    folly::Executor::KeepAlive<> executor,
    std::shared_ptr<RateLimiter> rateLimiter = nullptr,
    size_t numSequencedExecutors = 1);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/ChannelProcessor-inl.h>
