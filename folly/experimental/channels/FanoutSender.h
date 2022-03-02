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
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/detail/PointerVariant.h>

namespace folly {
namespace channels {

/**
 * A FanoutSender allows fanning out updates to multiple output receivers.
 * Values can be written as with a normal Sender. When there is only one output
 * receiver, the memory used by a FanoutSender (and the corresponding output
 * receiver) is the same as the memory used by a normal channel.
 *
 * When a new output receiver is added, an optional vector of initial values
 * can be provided. These initial values will only be sent to the new receiver.
 *
 * Memory used by closed receivers is reclaimed lazily (when iterating over
 * receivers).
 *
 * Example:
 *
 *  FanoutSender<int> fanoutSender;
 *  auto receiver1 = fanoutSender.getNewReceiver();
 *  auto receiver2 = fanoutSender.getNewReceiver();
 *  auto receiver3 = fanoutSender.getNewReceiver({1, 2, 3});
 *  std::move(fanoutSender).close();
 */
template <typename ValueType>
class FanoutSender {
 public:
  FanoutSender();
  FanoutSender(FanoutSender&& other) noexcept;
  FanoutSender& operator=(FanoutSender&& other) noexcept;
  ~FanoutSender();

  /**
   * Returns a new output receiver that will receive all values written to the
   * FanoutSender. If the initialValues parameter is provided, the given values
   * will (only) go to the new output receiver.
   */
  Receiver<ValueType> subscribe(std::vector<ValueType> initialValues = {});

  /**
   * Subscribes with an already-created sender.
   */
  void subscribe(Sender<ValueType> sender);

  /**
   * Returns whether this fanout sender has any active output receivers.
   */
  bool anySubscribers();

  /**
   * Returns the number of output receivers for this fanout sender.
   */
  std::uint64_t numSubscribers() const;

  /**
   * Sends the given value to all corresponding receivers.
   */
  template <typename U = ValueType>
  void write(U&& element);

  /**
   * Closes the fanout sender.
   */
  void close(folly::exception_wrapper ex = folly::exception_wrapper()) &&;

 private:
  bool hasSenderSet();

  detail::ChannelBridge<ValueType>* getSingleSender();

  folly::F14FastSet<detail::ChannelBridgePtr<ValueType>>* getSenderSet();

  void clearSendersWithClosedReceivers();

  detail::PointerVariant<
      detail::ChannelBridge<ValueType>,
      folly::F14FastSet<detail::ChannelBridgePtr<ValueType>>>
      senders_;
};
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/FanoutSender-inl.h>
