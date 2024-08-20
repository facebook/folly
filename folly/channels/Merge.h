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

#include <folly/channels/Channel.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
namespace channels {

/**
 * Merge takes a list of receivers, and returns a new receiver that receives
 * all updates from all input receivers. If any input receiver closes with
 * an exception, the exception is forwarded and the channel is closed. If any
 * input receiver closes without an exception, the channel continues to merge
 * values from the other input receivers until all input receivers are closed.
 *
 * @param inputReceivers: The collection of input receivers to merge.
 *
 * @param executor: A SequencedExecutor used to merge input values.
 *
 * @param waitForAllInputsToClose: When true, if any input receiver closes
 * without an exception, the channel continues to merge values from the other
 * input receivers until all input receivers are closed. If false, the channel
 * closes as soon as any input receiver has closed.
 *
 * Example:
 *
 *  // Example function that returns a list of receivers
 *  std::vector<Receiver<int>> getReceivers();
 *
 *  // Example function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  Receiver<int> mergedReceiver = merge(getReceivers(), getExecutor());
 */
template <typename TReceiver, typename TValue = typename TReceiver::ValueType>
Receiver<TValue> merge(
    std::vector<TReceiver> inputReceivers,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    bool waitForAllInputsToClose = true);
} // namespace channels
} // namespace folly

#include <folly/channels/Merge-inl.h>
