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

#include <algorithm>
#include <vector>

#include <folly/Traits.h>
#include <folly/Try.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/WhenN.h>
#include <folly/fibers/async/Async.h>
#include <folly/fibers/async/FiberManager.h>
#include <folly/fibers/async/Future.h>
#include <folly/functional/Invoke.h>

namespace folly {
namespace fibers {
namespace async {

/**
 * Schedules several async annotated functors and blocks until all of these are
 * completed. If any of the functors throws an exception, this exception will be
 * re-thrown, but only when all the tasks are complete. If several throw
 * exceptions one of them will be re-thrown.
 *
 * Returns a vector of the results of the functors.
 */
template <
    class InputIterator,
    typename FuncType =
        typename std::iterator_traits<InputIterator>::value_type,
    typename ResultType = invoke_result_t<FuncType>>
Async<std::vector<typename std::enable_if<
    !std::is_same<ResultType, Async<void>>::value,
    async_inner_type_t<ResultType>>::type>>
collectAll(InputIterator first, InputIterator last);

/**
 * collectAll specialization for functions returning void
 */
template <
    class InputIterator,
    typename FuncType =
        typename std::iterator_traits<InputIterator>::value_type,
    typename ResultType = invoke_result_t<FuncType>>
typename std::
    enable_if<std::is_same<ResultType, Async<void>>::value, Async<void>>::
        type inline collectAll(InputIterator first, InputIterator last);

/**
 * collectAll version that takes a container instead of iterators for
 * convenience
 */
template <class Collection>
auto collectAll(Collection&& c) -> decltype(collectAll(c.begin(), c.end())) {
  return collectAll(c.begin(), c.end());
}

/**
 * collectAll version that takes a varying number of functors instead of a
 * container or iterators
 */
template <typename... Ts>
Async<std::tuple<lift_unit_t<async_invocable_inner_type_t<Ts>>...>> collectAll(
    Ts&&... tasks) {
  auto future = folly::collectAllUnsafe(addFiberFuture(
      std::forward<Ts>(tasks), FiberManager::getFiberManager())...);
  auto tuple = await_async(futureWait(std::move(future)));
  return Async(folly::unwrapTryTuple(std::move(tuple)));
}

template <typename F>
Async<Try<async_invocable_inner_type_t<F>>> awaitTry(F&& func) {
  return makeTryWithNoUnwrap([&]() { return await(func()); });
}

/*
 * Run an async-annotated functor on a new fiber, blocking the current fiber.
 *
 * Should be used sparingly to reset the fiber stack usage and avoid fiber stack
 * overflows
 */
template <typename F>
Async<async_invocable_inner_type_t<F>> executeOnNewFiber(F&& func) {
  DCHECK(detail::onFiber());
  return futureWait(
      addFiberFuture(std::forward<F>(func), FiberManager::getFiberManager()));
}

/*
 * Run an async-annotated functor on a new fiber on remote thread,
 * blocking the current fiber.
 */
template <typename F>
Async<async_invocable_inner_type_t<F>> executeOnRemoteFiber(
    F&& func, FiberManager& fm) {
  DCHECK(detail::onFiber());
  return futureWait(addFiberRemoteFuture(std::forward<F>(func), fm));
}

} // namespace async
} // namespace fibers
} // namespace folly

#include <folly/fibers/async/Collect-inl.h>
