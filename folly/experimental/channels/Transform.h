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
#include <folly/experimental/channels/OnClosedException.h>
#include <folly/experimental/channels/RateLimiter.h>

namespace folly {
namespace channels {

/**
 * Returns an output receiver that applies a given transformation function to
 * each value from an input receiver.
 *
 * The TransformValue function takes a Try<InputValueType>, and returns a
 * folly::coro::AsyncGenerator<OutputValueType>.
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
 * @param rateLimiter: An optional rate limiter. If specified, the given rate
 *     limiter will limit the number of transformation functions that are
 *     simultaneously running.
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
    typename ReceiverType,
    typename TransformValueFunc,
    typename InputValueType = typename ReceiverType::ValueType,
    typename OutputValueType = typename folly::invoke_result_t<
        TransformValueFunc,
        folly::Try<InputValueType>>::value_type>
Receiver<OutputValueType> transform(
    ReceiverType inputReceiver,
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    TransformValueFunc transformValue,
    std::shared_ptr<RateLimiter> rateLimiter = nullptr);

/**
 * This overload accepts arguments in the form of a transformer object. The
 * transformer object must have the following functions:
 *
 * folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 * folly::coro::AsyncGenerator<OutputValueType&&> transformValue(
 *     folly::Try<InputValueType> inputValue);
 *
 * std::shared_ptr<RateLimiter> getRateLimiter(); // Can return nullptr
 */
template <
    typename ReceiverType,
    typename TransformerType,
    typename InputValueType = typename ReceiverType::ValueType,
    typename OutputValueType =
        typename decltype(std::declval<TransformerType>().transformValue(
            std::declval<folly::Try<InputValueType>>()))::value_type>
Receiver<OutputValueType> transform(
    ReceiverType inputReceiver, TransformerType transformer);

/**
 * This function is similar to the above transform function. However, instead of
 * taking a single input receiver, it takes an initialization function that
 * accepts a value of type InitializeArg, and returns a
 * std::pair<std::vector<OutputValueType>, Receiver<InputValueType>>.
 *
 *  - If the InitializeTransform function returns successfully, the vector's
 *      output values will be immediately sent to the output receiver. The input
 *      receiver is then processed as described in the transform function's
 *      documentation, unless and until it throws a ReinitializeException. At
 *      that point, the InitializationTransform is re-run with the InitializeArg
 *      specified in the ReinitializeException, and the transform begins anew.
 *
 *  - If the InitializeTransform function or the TransformValue function throws
 *      an OnClosedException, the output receiver is closed (with no exception).
 *
 *  - If the InitializeTransform function or the TransformValue function throws
 *      any other type of exception, the output receiver is closed with that
 *      exception.
 *
 * @param executor: A folly::SequencedExecutor used to transform the values.
 *
 * @param initializeArg: The initial argument passed to the InitializeTransform
 *  function.
 *
 * @param initializeTransform: The InitializeTransform function as described
 *  above.
 *
 * @param transformValue: The TransformValue function as described above.
 *
 * @param rateLimiter: An optional rate limiter. If specified, the given rate
 *     limiter will limit the number of transformation functions that are
 *     simultaneously running.
 *
 * Example:
 *
 *  struct InitializeArg {
 *    std::string param;
 *  }
 *
 *  // Function that returns a receiver
 *  Receiver<int> getInputReceiver(InitializeArg initializeArg);
 *
 *  // Function that returns an executor
 *  folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 *  Receiver<std::string> outputReceiver = transform(
 *      getExecutor(),
 *      InitializeArg{"param"},
 *      [](InitializeArg initializeArg) -> folly::coro::Task<
 *                  std::pair<std::vector<std::string>, Receiver<int>> {
 *          co_return std::make_pair(
 *              std::vector<std::string>({"Initialized"}),
 *              getInputReceiver(initializeArg));
 *      },
 *      [](folly::Try<int> try) -> folly::coro::AsyncGenerator<std::string&&> {
 *          try {
 *            co_yield folly::to<std::string>(try.value());
 *          } catch (const SomeApplicationException& ex) {
 *            throw ReinitializeException(InitializeArg{ex.getParam()});
 *          }
 *      });
 *
 */
template <
    typename InitializeArg,
    typename InitializeTransformFunc,
    typename TransformValueFunc,
    typename ReceiverType = typename folly::invoke_result_t<
        InitializeTransformFunc,
        InitializeArg>::StorageType::second_type,
    typename InputValueType = typename ReceiverType::ValueType,
    typename OutputValueType = typename folly::invoke_result_t<
        TransformValueFunc,
        folly::Try<InputValueType>>::value_type>
Receiver<OutputValueType> resumableTransform(
    folly::Executor::KeepAlive<folly::SequencedExecutor> executor,
    InitializeArg initializeArg,
    InitializeTransformFunc initializeTransform,
    TransformValueFunc transformValue,
    std::shared_ptr<RateLimiter> rateLimiter = nullptr);

/**
 * This overload accepts arguments in the form of a transformer object. The
 * transformer object must have the following functions:
 *
 * folly::Executor::KeepAlive<folly::SequencedExecutor> getExecutor();
 *
 * std::pair<std::vector<OutputValueType>, Receiver<InputValueType>>
 * initializeTransform(InitializeArg initializeArg);
 *
 * folly::coro::AsyncGenerator<OutputValueType&&> transformValue(
 *     folly::Try<InputValueType> inputValue);
 *
 * std::shared_ptr<RateLimiter> getRateLimiter(); // Can return nullptr
 */
template <
    typename InitializeArg,
    typename TransformerType,
    typename ReceiverType =
        typename decltype(std::declval<TransformerType>().initializeTransform(
            std::declval<InitializeArg>()))::StorageType::second_type,
    typename InputValueType = typename ReceiverType::ValueType,
    typename OutputValueType =
        typename decltype(std::declval<TransformerType>().transformValue(
            std::declval<folly::Try<InputValueType>>()))::value_type>
Receiver<OutputValueType> resumableTransform(
    InitializeArg initializeArg, TransformerType transformer);

/**
 * A ReinitializeException thrown by a transform callback indicates that the
 * resumable transform needs to be re-initialized.
 */
template <typename InitializeArg>
struct ReinitializeException : public std::exception {
  explicit ReinitializeException(InitializeArg _initializeArg)
      : initializeArg(std::move(_initializeArg)) {}

  const char* what() const noexcept override {
    return "This resumable transform should be re-initialized.";
  }

  InitializeArg initializeArg;
};
} // namespace channels
} // namespace folly

#include <folly/experimental/channels/Transform-inl.h>
