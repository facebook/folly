/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/Try.h>
#include <folly/Unit.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/detail/Traits.h>

#include <experimental/coroutine>
#include <functional>
#include <tuple>
#include <type_traits>

namespace folly {
namespace coro {
namespace detail {

template <typename SemiAwaitable>
using collect_all_try_component_t = folly::Try<decay_rvalue_reference_t<
    lift_lvalue_reference_t<semi_await_result_t<SemiAwaitable>>>>;

template <typename SemiAwaitable>
using collect_all_component_t =
    decay_rvalue_reference_t<lift_unit_t<semi_await_result_t<SemiAwaitable>>>;

} // namespace detail

///////////////////////////////////////////////////////////////////////////
// collectAll(SemiAwaitable<Ts>...) -> SemiAwaitable<std::tuple<Ts...>>
//
// The collectAll() function can be used to concurrently co_await on multiple
// SemiAwaitable objects and continue once they are all complete.
//
// collectAll() accepts an arbitrary number of SemiAwaitable objects and returns
// a SemiAwaitable object that will complete with a std::tuple of the results.
//
// When the returned SemiAwaitable object is co_awaited it will launch
// a new coroutine for awaiting each input awaitable in-turn.
//
// Note that coroutines for awaiting the input awaitables of later arguments
// will not be launched until the prior coroutine reaches its first suspend
// point. This means that awaiting multiple sub-tasks that all complete
// synchronously will still execute them sequentially on the current thread.
//
// If any of the input operations complete with an exception then the whole
// collectAll() operation will also complete with an exception once all of the
// operations have completed. Any partial results will be discarded.
// If multiple operations fail with an exception then one of the exceptions
// will be rethrown to the caller (which one is unspecified) and the other
// exceptions are discarded.
//
// If you need to know which operation failed or you want to handle partial
// failures then you can use the folly::coro::collectAllTry() instead which
// returns a tuple of Try<T> objects instead of a tuple of values.
//
// Example: Serially awaiting multiple operations (slower)
//   folly::coro::Task<Foo> doSomething();
//   folly::coro::Task<Bar> doSomethingElse();
//
//   Foo result1 = co_await doSomething();
//   Bar result2 = co_await doSomethingElse();
//
// Example: Concurrently awaiting multiple operations (faster) C++17-only.
//   auto [result1, result2] =
//       co_await folly::coro::collectAll(doSomething(), doSomethingElse());
//
template <typename... SemiAwaitables>
auto collectAll(SemiAwaitables&&... awaitables) -> folly::coro::Task<std::tuple<
    detail::collect_all_component_t<remove_cvref_t<SemiAwaitables>>...>>;

///////////////////////////////////////////////////////////////////////////
// collectAllTry(SemiAwaitable<Ts>...)
//    -> SemiAwaitable<std::tuple<Try<Ts>...>>
//
// Like the collectAll() function, the collectAllTry() function can be used to
// concurrently await multiple input SemiAwaitable objects.
//
// The collectAllTry() function differs from collectAll() in that it produces a
// tuple of Try<T> objects rather than a tuple of the values.
// This allows the caller to inspect the success/failure of individual
// operations and handle partial failures but has a less-convenient interface
// than collectAll().
//
// Example: Handling partial failure with collectAllTry()
//    folly::coro::Task<Foo> doSomething();
//    folly::coro::Task<Bar> doSomethingElse();
//
//    auto [result1, result2] = co_await folly::coro::collectAllTry(
//        doSomething(), doSomethingElse());
//
//    if (result1.hasValue()) {
//      Foo& foo = result1.value();
//      process(foo);
//    } else {
//      logError("doSomething() failed", result1.exception());
//    }
//
//    if (result2.hasValue()) {
//      Bar& bar = result2.value();
//      process(bar);
//    } else {
//      logError("doSomethingElse() failed", result2.exception());
//    }
//
template <typename... SemiAwaitables>
auto collectAllTry(SemiAwaitables&&... awaitables)
    -> folly::coro::Task<std::tuple<detail::collect_all_try_component_t<
        remove_cvref_t<SemiAwaitables>>...>>;

} // namespace coro
} // namespace folly

#include <folly/experimental/coro/Collect-inl.h>
