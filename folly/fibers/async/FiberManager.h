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

#include <folly/fibers/FiberManager.h>
#include <folly/fibers/async/Async.h>

#pragma once

namespace folly {
namespace fibers {
namespace async {

// These functions wrap the corresponding FiberManager members.
// The difference is that these functions accept callables which
// return Async results.
//
// Implementation notes:
// These functions don't respect some design principles of the library:
// - Strict separation of functions running on main context and fiber context
// - Avoid using the Awaitable of the other async frameworks in the public
//   interface, under long-term goal of decoupling the frameworks
// - Keep FiberManager as an implementation detail, in favor of Executor in
//   the public interface
//
// These functions should ultimately live in detail::, but are public to
// enable early adoption of the library.

/**
 * Schedule an async-annotated functor to run on a fiber manager.
 */
template <typename F>
void addFiber(F&& func, FiberManager& fm) {
  fm.addTask(
      [func = std::forward<F>(func)]() mutable { return init_await(func()); });
}

/**
 * Schedule an async-annotated functor to run on a remote thread's
 * fiber manager.
 */
template <typename F>
void addFiberRemote(F&& func, FiberManager& fm) {
  fm.addTaskRemote(
      [func = std::forward<F>(func)]() mutable { return init_await(func()); });
}

/**
 * Schedule an async-annotated functor to run on a fiber manager.
 * Returns a Future for the result.
 *
 * In most cases, prefer those options instead:
 * - executeOnNewFiber: reset fiber stack usage from fiber context
 * - executeOnFiberAndWait: initialize fiber context from main context
 *   then block thread
 */
template <typename F>
Future<lift_unit_t<async_invocable_inner_type_t<F>>> addFiberFuture(
    F&& func,
    FiberManager& fm) {
  return fm.addTaskFuture(
      [func = std::forward<F>(func)]() mutable { return init_await(func()); });
}

/**
 * Schedule an async-annotated functor to run on a remote thread's
 * fiber manager.
 * Returns a future for the result.
 *
 * In most cases, prefer those options instead:
 * - executeOnRemoteFiber: wait on remote fiber from (local) fiber context
 * - executeOnRemoteFiberAndWait: wait on remote fiber from (local) main context
 */
template <typename F>
Future<lift_unit_t<async_invocable_inner_type_t<F>>> addFiberRemoteFuture(
    F&& func,
    FiberManager& fm) {
  return fm.addTaskRemoteFuture(
      [func = std::forward<F>(func)]() mutable { return init_await(func()); });
}

} // namespace async
} // namespace fibers
} // namespace folly
