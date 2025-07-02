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

#include <folly/coro/TaskWrapper.h>
#include <folly/coro/safe/SafeAlias.h>

#if FOLLY_HAS_IMMOVABLE_COROUTINES

/// `NowTask<T>` quacks like `Task<T>` but is immovable, and must be
/// `co_await`ed in the same expression that created it.
///
/// Using `NowTask` by default brings considerable safety benefits.  With
/// `Task`, the following would be anti-patterns that cause dangling reference
/// bugs, but with `NowTask`, C++ lifetime extension rules ensure that they
/// simply work.
///   - Pass-by-reference into coroutines.
///   - Ephemeral coro lambdas with captures.
///   - Coro lambdas with capture-by-reference.
///
/// Notes:
///   - (subject to change) Unlike `SafeTask`, `NowTask` does NOT check
///     `safe_alias_of` for the return type `T`.  `NowTask` is essentially an
///     immediate async function -- it satisfies the structured concurrency
///     maxim of "lexical scope drives both control flow & lifetime".  That
///     lowers the odds that returned pointers/references are unexpectedly
///     invalid.  The one failure mode I can think of is that the
///     pointed-to-data gets invalidated by a concurrent thread of execution,
///     but in that case the program almost certainly has a data race --
///     regardless of the lifetime bug -- and that requires runtime
///     instrumentation (like TSAN) to detect in present-day C++.

namespace folly::coro {

template <safe_alias, typename>
class BackgroundTask;

template <typename T = void>
class NowTask;

template <typename T = void>
class NowTaskWithExecutor;

namespace detail {
template <typename T>
struct NowTaskWithExecutorCfg : DoesNotWrapAwaitable {
  using InnerTaskWithExecutorT = TaskWithExecutor<T>;
  using WrapperTaskT = NowTask<T>;
};
template <typename T>
using NowTaskWithExecutorBase =
    AddMustAwaitImmediately<TaskWithExecutorWrapperCrtp<
        NowTaskWithExecutor<T>,
        detail::NowTaskWithExecutorCfg<T>>>;
} // namespace detail

template <typename T>
class FOLLY_NODISCARD NowTaskWithExecutor final
    : public detail::NowTaskWithExecutorBase<T> {
 protected:
  using detail::NowTaskWithExecutorBase<T>::NowTaskWithExecutorBase;

  template <safe_alias, typename>
  friend class BackgroundTask; // for `unwrapTaskWithExecutor`, remove later
};

namespace detail {
template <typename T>
class NowTaskPromise final
    : public TaskPromiseWrapper<T, NowTask<T>, TaskPromise<T>> {};
template <typename T>
struct NowTaskCfg : DoesNotWrapAwaitable {
  using ValueT = T;
  using InnerTaskT = Task<T>;
  using TaskWithExecutorT = NowTaskWithExecutor<T>;
  using PromiseT = NowTaskPromise<T>;
};
template <typename T>
using NowTaskBase =
    AddMustAwaitImmediately<TaskWrapperCrtp<NowTask<T>, detail::NowTaskCfg<T>>>;
} // namespace detail

template <safe_alias, typename>
class SafeTask;

template <safe_alias S, typename U>
auto toNowTask(SafeTask<S, U>);

template <typename T>
class FOLLY_CORO_TASK_ATTRS NowTask final : public detail::NowTaskBase<T> {
 protected:
  using detail::NowTaskBase<T>::NowTaskBase;

  template <typename U> // can construct
  friend auto toNowTask(Task<U>);
  template <safe_alias S, typename U> // can construct
  friend auto toNowTask(SafeTask<S, U>);
  template <typename U> // can construct & `unwrapTask`
  friend auto toNowTask(NowTask<U>);
};

// NB: `toNowTask(SafeTask)` is in `SafeTask.h` to avoid circular deps.
template <typename T>
auto toNowTask(Task<T> t) {
  return NowTask<T>{std::move(t)};
}
template <typename T>
auto toNowTask(NowTask<T> t) {
  return NowTask<T>{std::move(t).unwrapTask()};
}

// Apparently, Clang 15 has a bug in prvalue semantics support, so it cannot
// return immovable coroutines.
#if !defined(__clang__) || __clang_major__ > 15

/// Make a `NowTask` that trivially returns a value.
template <class T>
NowTask<T> makeNowTask(T t) {
  co_return t;
}

/// Make a `NowTask` that trivially returns no value
inline NowTask<> makeNowTask() {
  co_return;
}
/// Same as makeNowTask(). See Unit
inline NowTask<> makeNowTask(Unit) {
  co_return;
}

/// Make a `NowTask` that will trivially yield an exception.
template <class T>
NowTask<T> makeErrorNowTask(exception_wrapper ew) {
  co_yield co_error(std::move(ew));
}

#endif // no `makeNowTask` on old/buggy clang

} // namespace folly::coro

template <typename T>
struct folly::safe_alias_of<::folly::coro::NowTask<T>>
    : safe_alias_constant<safe_alias::unsafe> {};

#endif

#if FOLLY_HAS_COROUTINES

namespace folly::coro::detail {

// `best_fit_task_wrapper<void, ...>` defaults to `NowTask`
template <typename Void, typename... SemiAwaitables>
struct best_fit_task_wrapper
#if FOLLY_HAS_IMMOVABLE_COROUTINES
{
  template <typename T>
  using task_type = NowTask<T>;
}
#endif
;

// If all `best_fit_task_wrapper<void, ...>` inputs are movable, return `Task`.
template <typename... SemiAwaitables>
struct best_fit_task_wrapper<
    std::enable_if_t<(!must_await_immediately_v<SemiAwaitables> && ...), void>,
    SemiAwaitables...> {
  template <typename T>
  using task_type = Task<T>;
};
} // namespace folly::coro::detail

#endif // FOLLY_HAS_COROUTINES
