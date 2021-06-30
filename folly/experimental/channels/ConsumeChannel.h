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

#include <folly/Executor.h>
#include <folly/IntrusiveList.h>
#include <folly/experimental/channels/Channel.h>
#include <folly/experimental/channels/ChannelCallbackHandle.h>
#include <folly/experimental/coro/Task.h>

namespace folly {
namespace channels {

/**
 * This function takes a Receiver, and consumes updates from that receiver with
 * a callback.
 *
 * This function returns a ChannelCallbackHandle. On destruction of this handle,
 * the callback will receive a try containing an exception of type
 * folly::OperationCancelled. If an active callback is running at the time the
 * cancellation request is received, cancellation will be requested on the
 * ambient cancellation token of the callback.
 *
 * The callback is run for each received value on the given executor. A try
 * is passed to the callback with the result:
 *
 *    - If a value is sent, the folly::Try will contain the value.
 *    - If the channel is closed by the sender with no exception, the try will
 *          be empty (with no value or exception).
 *    - If the channel is closed by the sender with an exception, the try will
 *          contain the exception.
 *    - If the channel was cancelled (by the destruction of the returned
 *          handle), the try will contain an exception of type
 *          folly::OperationCancelled.
 *
 * If the callback returns false or throws a folly::OperationCancelled
 * exception, the channel will be cancelled and no further values will be
 * received.
 */
template <
    typename TReceiver,
    typename OnNextFunc,
    typename TValue = typename TReceiver::ValueType,
    std::enable_if_t<
        std::is_constructible_v<
            folly::Function<folly::coro::Task<bool>(folly::Try<TValue>)>,
            OnNextFunc>,
        int> = 0>
ChannelCallbackHandle consumeChannelWithCallback(
    TReceiver receiver,
    folly::Executor::KeepAlive<> executor,
    OnNextFunc onNext);

/**
 * This overload is similar to the previous overload. However, unlike the
 * previous overload (which returns a handle that allows cancellation of that
 * specific consumption operation), this overload accepts a list of handles and
 * returns void. This overload will immediately add a handle to the list, and
 * will remove itself from the list if the channel is closed or cancelled.
 *
 * When the passed-in handle list is destroyed by the caller, all handles
 * remaining in the list will trigger cancellation on their corresponding
 * consumption operations.
 *
 * Note that ChannelCallbackHandleList is not thread safe. This means that all
 * operations using a particular list (including start and cancellation) need to
 * run on the same serial executor passed to this function.
 */
template <
    typename TReceiver,
    typename OnNextFunc,
    typename TValue = typename TReceiver::ValueType,
    std::enable_if_t<
        std::is_constructible_v<
            folly::Function<folly::coro::Task<bool>(folly::Try<TValue>)>,
            OnNextFunc>,
        int> = 0>
void consumeChannelWithCallback(
    TReceiver receiver,
    folly::Executor::KeepAlive<> executor,
    OnNextFunc onNext,
    ChannelCallbackHandleList& callbackHandles);
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/ConsumeChannel-inl.h>
