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

#include <folly/Try.h>
#include <folly/Unit.h>
#include <folly/container/Access.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/AsyncScope.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/detail/Traits.h>

#include <range/v3/view/move.hpp>

#include <functional>
#include <iterator>
#include <tuple>
#include <type_traits>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

template <typename SemiAwaitable>
using collect_all_try_component_t = folly::Try<decay_rvalue_reference_t<
    lift_lvalue_reference_t<semi_await_result_t<SemiAwaitable>>>>;

template <typename SemiAwaitable>
using collect_all_component_t =
    decay_rvalue_reference_t<lift_unit_t<semi_await_result_t<SemiAwaitable>>>;

template <typename SemiAwaitable>
using collect_all_range_component_t = decay_rvalue_reference_t<
    lift_lvalue_reference_t<semi_await_result_t<SemiAwaitable>>>;

template <typename SemiAwaitable>
using collect_all_try_range_component_t =
    collect_all_try_component_t<SemiAwaitable>;

template <typename Range>
using range_iterator_t = decltype(access::begin(std::declval<Range&>()));

template <typename Iterator>
using iterator_reference_t = typename std::iterator_traits<Iterator>::reference;

template <typename Range>
using range_reference_t = iterator_reference_t<range_iterator_t<Range>>;

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
// If any of the input operations complete with an exception then it will
// request cancellation of any outstanding tasks and the whole collectAll()
// operation will complete with an exception once all of the operations
// have completed.  Any partial results will be discarded. If multiple
// operations fail with an exception then the exception from the first task
// to fail will be rethrown and subsequent errors are discarded.
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
// It also differs in that failure of one subtask does _not_ request
// cancellation of the other subtasks.
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

////////////////////////////////////////////////////////////////////////
// collectAllRange(RangeOf<SemiAwaitable<T>>&&)
//   -> SemiAwaitable<std::vector<T>>
//
// The collectAllRange() function can be used to concurrently await a collection
// of SemiAwaitable objects, returning a std::vector of the individual results
// once all operations have completed.
//
// If any of the operations fail with an exception then requests cancellation of
// any outstanding operations and the entire operation fails with an exception,
// discarding any partial results. If more than one operation fails with an
// exception then the exception from task that failed first (in time) is
// rethrown. Other results and exceptions are discarded.
//
// If you need to be able to distinguish which operation failed or handle
// partial failures then use collectAllTryRange() instead.
//
// Note that the expression `*it` must be SemiAwaitable.
// This typically means that containers of Task<T> must be adapted to produce
// moved-elements by applying the ranges::views::move transform.
// e.g.
//
//   std::vector<Task<T>> tasks = ...;
//   std::vector<T> vals = co_await collectAllRange(tasks |
//   ranges::views::move);
//
template <
    typename InputRange,
    std::enable_if_t<
        !std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int> = 0>
auto collectAllRange(InputRange awaitables)
    -> folly::coro::Task<std::vector<detail::collect_all_range_component_t<
        detail::range_reference_t<InputRange>>>>;
template <
    typename InputRange,
    std::enable_if_t<
        std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int> = 0>
auto collectAllRange(InputRange awaitables) -> folly::coro::Task<void>;

////////////////////////////////////////////////////////////////////////////
// collectAllTryRange(RangeOf<SemiAwaitable<T>>&&)
//    -> SemiAwaitable<std::vector<folly::Try<T>>>
//
// The collectAllTryRange() function can be used to concurrently await a
// collection of SemiAwaitable objects and produces a std::vector of
// Try<T> objects once all of the input operations have completed.
//
// The element of the returned vector contains the result of the corresponding
// input operation in the same order that they appeared in the 'awaitables'
// sequence.
//
// The success/failure of individual results can be inspected by calling
// .hasValue() or .hasException() on the elements of the returned vector.
template <typename InputRange>
auto collectAllTryRange(InputRange awaitables)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>>;

// collectAllRange()/collectAllTryRange() overloads that simplifies the
// common-case where an rvalue std::vector<SemiAwaitable> is passed.
//
// This avoids the caller needing to pipe the input through ranges::views::move
// transform to force the elements to be rvalue-references since the
// std::vector<T>::reference type is T& rather than T&& and some awaitables,
// such as Task<U>, are not lvalue awaitable.
template <typename SemiAwaitable>
auto collectAllRange(std::vector<SemiAwaitable> awaitables)
    -> decltype(collectAllRange(awaitables | ranges::views::move)) {
  co_return co_await collectAllRange(awaitables | ranges::views::move);
}

template <typename SemiAwaitable>
auto collectAllTryRange(std::vector<SemiAwaitable> awaitables)
    -> decltype(collectAllTryRange(awaitables | ranges::views::move)) {
  co_return co_await collectAllTryRange(awaitables | ranges::views::move);
}

namespace detail {
template <typename InputRange, bool IsTry>
using async_generator_from_awaitable_range_item_t = conditional_t<
    IsTry,
    collect_all_try_range_component_t<range_reference_t<InputRange>>,
    collect_all_range_component_t<range_reference_t<InputRange>>>;
}

////////////////////////////////////////////////////////////////////////////
// makeUnorderedAsyncGeneratorFromAwaitableRange(AsyncScope&,
// RangeOf<SemiAwaitable<T>>&&) -> AsyncGenerator<T&&>
// makeUnorderedAsyncGeneratorFromAwaitableTryRange(AsyncScope&,
// RangeOf<SemiAwaitable<T>>&&) -> AsyncGenerator<Try<T>&&>

// Returns an AsyncGenerator that yields results of passed-in awaitables in
// order of completion.
// Destroying or cancelling the AsyncGenerator cancels the remaining awaitables.
//
// makeUnorderedAsyncGeneratorFromAwaitableRange cancels all remaining
// awaitables when any of them fail with an exception. Any results obtained
// before the failure are still returned via the generator, then the first
// exception in time. makeUnorderedAsyncGeneratorFromAwaitableTryRange does not
// cancel awaitables when one fails, and yields all results even when cancelled.
//
// Awaitables are attached to the passed-in AsyncScope.

template <typename InputRange>
auto makeUnorderedAsyncGeneratorFromAwaitableRange(
    AsyncScope& scope, InputRange awaitables)
    -> AsyncGenerator<detail::async_generator_from_awaitable_range_item_t<
        InputRange,
        false>&&>;
template <typename InputRange>
auto makeUnorderedAsyncGeneratorFromAwaitableTryRange(
    AsyncScope& scope, InputRange awaitables)
    -> AsyncGenerator<detail::async_generator_from_awaitable_range_item_t<
        InputRange,
        true>&&>;

template <typename SemiAwaitable>
auto makeUnorderedAsyncGeneratorFromAwaitableRange(
    AsyncScope& scope, std::vector<SemiAwaitable> awaitables)
    -> decltype(makeUnorderedAsyncGeneratorFromAwaitableRange(
        scope, awaitables | ranges::views::move)) {
  co_return co_await makeUnorderedAsyncGeneratorFromAwaitableRange(
      scope, awaitables | ranges::views::move);
}
template <typename SemiAwaitable>
auto makeUnorderedAsyncGeneratorFromAwaitableTryRange(
    AsyncScope& scope, std::vector<SemiAwaitable> awaitables)
    -> decltype(makeUnorderedAsyncGeneratorFromAwaitableTryRange(
        scope, awaitables | ranges::views::move)) {
  co_return co_await makeUnorderedAsyncGeneratorFromAwaitableTryRange(
      scope, awaitables | ranges::views::move);
}

///////////////////////////////////////////////////////////////////////////////
// collectAllWindowed(RangeOf<SemiAwaitable<T>>&&, size_t maxConcurrency)
//   -> SemiAwaitable<std::vector<T>>
//
// collectAllWindowed(RangeOf<SemiAwaitable<void>>&&, size_t maxConcurrency)
//   -> SemiAwaitable<void>
//
// Await each of the input awaitables in the range, allowing at most
// 'maxConcurrency' of these input awaitables to be concurrently awaited
// at any one point in time.
//
// If any of the input awaitables fail with an exception then requests
// cancellation of any incomplete operations and fails the whole
// operation with an exception. If multiple input awaitables fail with
// an exception then the exeception from the first task to fail (in time)
// will be rethrown and the rest of the results will be discarded.
//
// If there is an exception thrown while iterating over the input-range then
// it will still guarantee that any prior awaitables in the input-range will
// run to completion before completing the collectAllWindowed() operation with
// the exception thrown during iteration.
//
// The resulting std::vector will contain the results in the corresponding
// order of their respective awaitables in the input range.
template <
    typename InputRange,
    std::enable_if_t<
        std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int> = 0>
auto collectAllWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<void>;
template <
    typename InputRange,
    std::enable_if_t<
        !std::is_void_v<
            semi_await_result_t<detail::range_reference_t<InputRange>>>,
        int> = 0>
auto collectAllWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<std::vector<detail::collect_all_range_component_t<
        detail::range_reference_t<InputRange>>>>;

///////////////////////////////////////////////////////////////////////////////
// collectAllTryWindowed(RangeOf<SemiAwaitable<T>>&, size_t maxConcurrency)
//   -> SemiAwaitable<std::vector<folly::Try<T>>>
//
// Concurrently awaits a collection of awaitable with bounded concurrency,
// producing a vector of Try values containing each of the results.
//
// The resulting std::vector will contain the results in the corresponding
// order of their respective awaitables in the input range.
//
// Note that the whole operation may still complete with an exception if
// iterating over the awaitables fails with an exception (eg. if you pass
// a Generator<Task<T>&&> and the generator throws an exception).
template <typename InputRange>
auto collectAllTryWindowed(InputRange awaitables, std::size_t maxConcurrency)
    -> folly::coro::Task<std::vector<detail::collect_all_try_range_component_t<
        detail::range_reference_t<InputRange>>>>;

// collectAllWindowed()/collectAllTryWindowed() overloads that simplify the
// use of these functions with std::vector<SemiAwaitable>.
template <typename SemiAwaitable>
auto collectAllWindowed(
    std::vector<SemiAwaitable> awaitables, std::size_t maxConcurrency)
    -> decltype(collectAllWindowed(
        awaitables | ranges::views::move, maxConcurrency)) {
  co_return co_await collectAllWindowed(
      awaitables | ranges::views::move, maxConcurrency);
}

template <typename SemiAwaitable>
auto collectAllTryWindowed(
    std::vector<SemiAwaitable> awaitables, std::size_t maxConcurrency)
    -> decltype(collectAllTryWindowed(
        awaitables | ranges::views::move, maxConcurrency)) {
  co_return co_await collectAllTryWindowed(
      awaitables | ranges::views::move, maxConcurrency);
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Collect-inl.h>
