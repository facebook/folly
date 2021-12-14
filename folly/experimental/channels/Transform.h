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

/**
 * Returns an output receiver that applies a given transformation function to
 * each value from an input receiver.
 *
 * The TransformValue function takes a Try<TInputValue>, and returns a
 * folly::coro::AsyncGenerator<TOutputValue>.
 *
 *   - If the TransformValue function yields one or more output values, those
 *      output values are sent to the output receiver.
 *
 *   - If the TransformValue function throws an OnClosedException, the output
 *      receiver is closed (without an exception).
 *
 *   - If the TransformValue function throws any other type of exception, the
 *      output receiver is closed with that exception.
 *
 * If the input receiver was closed, the TransformValue function is called with
 * a Try containing an exception (either OnClosedException if the input receiver
 * was closed without an exception, or the closure exception if the input
 * receiver was closed with an exception). In this case, regardless of what the
 * TransformValue function returns, the output receiver will be closed
 * (potentially after receiving the last output values the TransformValue
 * function returned, if any).
 *
 * @param inputReceiver: The input receiver.
 *
 * @param executor: A folly::SequencedExecutor used to transform the values.
 *
 * @param transformValue: A function as described above.
 *
 * Example:
 *
 *  // Function that returns a receiver
 *  Receiver<int> getInputReceiver();
 *
 *  // Function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  Receiver<std::string> outputReceiver = transform(
 *      getInputReceiver(),
 *      getExecutor(),
 *      [](folly::Try<int> try) -> folly::coro::AsyncGenerator<std::string&&> {
 *          co_yield folly::to<std::string>(try.value());
 *      });
 */
template <
    typename TReceiver,
    typename TransformValueFunc,
    typename TInputValue = typename TReceiver::ValueType,
    typename TOutputValue = typename folly::invoke_result_t<
        TransformValueFunc,
        folly::Try<TInputValue>>::value_type>
Receiver<TOutputValue> transform(
    TReceiver inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    TransformValueFunc transformValue);

/**
 * This function is similar to the above transform function. However, instead of
 * taking a single input receiver, it takes an initialization function that
 * returns a std::pair<std::vector<TOutputValue>, Receiver<TInputValue>>.
 *
 *  - If the InitializeTransform function returns successfully, the vector's
 *      output values will be immediately sent to the output receiver. The input
 *      receiver is then processed as described in the transform function's
 *      documentation, until it is closed (without an exception). At that point,
 *      the InitializationTransform is re-run, and the transform begins anew.
 *
 *  - If the InitializeTransform function throws an OnClosedException, the
 *      output receiver is closed (with no exception).
 *
 *  - If the InitializeTransform function throws any other type of exception,
 *      the output receiver is closed with that exception.
 *
 *  - If the TransformValue function throws any exception other than
 *      OnClosedException, the output receiver is closed with that exception.
 *
 * @param executor: A folly::SequencedExecutor used to transform the values.
 *
 * @param initializeTransform: The InitializeTransform function as described
 *  above.
 *
 * @param transformValue: A function as described above.
 *
 * Example:
 *
 *  // Function that returns a receiver
 *  Receiver<int> getInputReceiver();
 *
 *  // Function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  Receiver<std::string> outputReceiver = transform(
 *      getExecutor(),
 *      []() -> folly::coro::Task<
 *                  std::pair<std::vector<std::string>, Receiver<int>> {
 *          co_return std::make_pair(
 *              std::vector<std::string>({"Initialized"}),
 *              getInputReceiver());
 *      },
 *      [](folly::Try<int> try) -> folly::coro::AsyncGenerator<std::string&&> {
 *          co_yield folly::to<std::string>(try.value());
 *      });
 *
 */
template <
    typename InitializeTransformFunc,
    typename TransformValueFunc,
    typename TReceiver = typename folly::invoke_result_t<
        InitializeTransformFunc>::StorageType::second_type,
    typename TInputValue = typename TReceiver::ValueType,
    typename TOutputValue = typename folly::invoke_result_t<
        TransformValueFunc,
        folly::Try<TInputValue>>::value_type>
Receiver<TOutputValue> resumableTransform(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    InitializeTransformFunc initializeTransform,
    TransformValueFunc transformValue);

/**
 * An OnClosedException passed to a transform callback indicates that the input
 * channel was closed. An OnClosedException can also be thrown by a transform
 * callback, which will close the output channel.
 */
struct OnClosedException : public std::exception {
  const char* what() const noexcept override {
    return "A transform has closed the channel.";
  }
};
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/Transform-inl.h>
