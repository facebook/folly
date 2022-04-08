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

#include <folly/experimental/channels/detail/ChannelBridge.h>

#include <optional>

namespace folly {
namespace channels {

template <typename TValue>
class Receiver;

template <typename TValue>
class Sender;

namespace detail {
template <typename TValue>
ChannelBridgePtr<TValue>& senderGetBridge(Sender<TValue>& sender);

template <typename TValue>
bool receiverWait(
    Receiver<TValue>& receiver, detail::IChannelCallback* receiverCallback);

template <typename TValue>
detail::IChannelCallback* cancelReceiverWait(Receiver<TValue>& receiver);

template <typename TValue>
std::optional<folly::Try<TValue>> receiverGetValue(Receiver<TValue>& receiver);

template <typename TValue>
std::pair<detail::ChannelBridgePtr<TValue>, detail::ReceiverQueue<TValue>>
receiverUnbuffer(Receiver<TValue>&& receiver);
} // namespace detail
} // namespace channels
} // namespace folly
