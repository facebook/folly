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
template <typename ValueType>
class ProxyChannelProcessor;
}

/**
 * A proxy allows one to create a channel whose input is proxied from the output
 * of another channel, which can change over time. This is more memory-efficient
 * than using a MergeChannel with one input receiver.
 *
 * Example:
 *
 *  // Example function that returns a receiver for a given entity:
 *  Receiver<int> subscribe(std::string entity);
 *
 *  // Example function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  auto [outputReceiver, proxyChannel]
 *      = createProxyChannel<int>(getExecutor());
 *  proxyChannel.setInputReceiver(subscribe("abc"));
 *  proxyChannel.setInputReceiver(subscribe("def"));
 *  proxyChannel.removeInputReceiver();
 *  proxyChannel.setInputReceiver(subscribe("ghi"));
 *  std::move(proxyChannel).close();
 */
template <typename ValueType>
class ProxyChannel {
  using TProcessor = detail::ProxyChannelProcessor<ValueType>;

 public:
  explicit ProxyChannel(detail::ProxyChannelProcessor<ValueType>* processor);
  ProxyChannel(ProxyChannel&& other) noexcept;
  ProxyChannel& operator=(ProxyChannel&& other) noexcept;
  ~ProxyChannel();

  /**
   * Returns whether this ProxyChannel is a valid object.
   */
  explicit operator bool() const;

  /**
   * Sets a new input receiver. As soon as this function returns, values from
   * the old input receiver (if any) will no longer be sent to the output
   * receiver. Values from the new input receiver will start being sent to the
   * output receiver, unless a previous input receiver was closed.
   */
  void setInputReceiver(Receiver<ValueType> receiver);

  /**
   * Removes the current input receiver (if any). As soon as this function
   * returns, values from the old input receiver (if any) will no longer be sent
   * to the output receiver.
   */
  void removeInputReceiver();

  /**
   * Closes the proxy channel.
   */
  void close(folly::exception_wrapper&& ex = {}) &&;

 private:
  TProcessor* processor_;
};

/**
 * Creates a new proxy channel.
 *
 * @param executor: The SequencedExecutor to use for proxying values.
 */
template <typename ValueType>
std::pair<Receiver<ValueType>, ProxyChannel<ValueType>> createProxyChannel(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor);
} // namespace channels
} // namespace folly

#include <folly/channels/ProxyChannel-inl.h>
