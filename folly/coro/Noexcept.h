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
#include <folly/coro/ViaIfAsync.h>

#if FOLLY_HAS_COROUTINES

/// ## When to use this
///
/// Use `AsNoexcept`<>` only with APIs that only take coroutines that MUST NOT
/// throw when awaited -- like `co_cleanup()` or async scopes.  If your code
/// compiles without `AsNoexcept<>`, you do not need it!
///
/// ## This is probably not the utility you are looking for!
///
///   - This is not related to `co_nothrow`, which is a perf optimization for
///     coroutines with hot exceptions.  `co_await co_nothrow(foo())` means
///     "when `foo()` throws, exit the current coro and pass the exception to
///     whatever is awaiting me".  This saves the ~usec overhead of rethrow.
///
///   - If your project likes to avoid exceptions, that is not a great reason
///     to reflexively make all your coros `AsNoexcept<>`, for these reasons:
///       * Since `AsNoexcept<>` is implemented as a wrapper, it may reduce
///         your build speed.
///       * Coro frame allocation & construction can still throw (unless you
///         also mark the coro function `noexcept`).
///       * Emitting the `noexcept` -> `std::terminate` offramp can sometimes
///         be a pessimization compared to normal exception propagation.
///
///   - We do not provide a helper like `co_await co_fatalOnThrow(...)`, since
///     most callsites should either handle the exception (possibly with
///     `co_awaitTry`), or let it fly.
///
/// ## Why does this even exist, what's wrong with `noexcept`?
///
/// Some `folly::coro` libraries require `AsNoexcept<>` is to clearly signal a
/// **firm contract** between the API and the user-supplied coroutine.  This is
/// only appropriate in situations similar to sync destructors, where the API
/// has no good recourse in case of a thrown exception.
///
/// We need a special wrapper type because marking a coroutine function
/// `noexcept` says nothing about whether awaiting the resulting coroutine can
/// throw.  Rather, it makes fatal any exception thrown during the construction
/// of the coroutine object itself (i.e.  a `bad_alloc` for the frame, or
/// errors copying/moving the args).
///
/// ## How exactly does `AsNoexcept<>` work?
///
/// `Noexcept.h` lets you mark coroutine types as `noexcept_awaitable_v`:
///
///   []() -> AsNoexcept<Task<...>> { co_return ...; }
///
/// This function creates a coroutine whose awaitable is that of the inner
/// task, but automatically wrapped with `detail::NoexceptAwaitable`.
//
/// The latter is an awaitable-wrapper similar to `co_awaitTry()`, except that
/// it terminates the program if `someAwaitable()` resumes with a thrown
/// exception.  So, both of these will never throw, but the former returns a
/// `Try` while latter returns an unwrapped value:
///
///   co_await co_awaitTry(intAwaitable())  // returns `Try<int>`
///   co_await detail::NoexceptAwaitable{intAwaitable()} // returns `int`
///
/// Both the coroutine `AsNoexcept<Task<...>>` and the preceding 2 awaitables
/// return `true` for `noexcept_awaitable_v`.
///
/// `AsNoexcept<>` / `NoexceptAwaitable` compose properly with other coroutine-
/// and awaitable-wrappers.  But, not all combinations make sense -- see the
/// test, and/or extend it if needed.  For example, the wrapper is useless in
/// `NoexceptAwaitable(co_awaitTry(...))`, since exceptions are put in a `Try`.

namespace folly::coro {

namespace detail {
template <typename T>
class NoexceptAwaitable;
} // namespace detail

namespace detail {

template <typename Awaitable>
class NoexceptAwaiter {
 private:
  using Awaiter = awaiter_type_t<Awaitable>;
  Awaiter awaiter_;

 public:
  explicit NoexceptAwaiter(Awaitable&& awaiter)
      : awaiter_(get_awaiter(static_cast<Awaitable&&>(awaiter))) {}

  auto await_ready() noexcept -> decltype(awaiter_.await_ready()) {
    // As of this writing, all `await_ready` in `folly::coro` are `noexcept`.
    // If this is legitimately triggered, then we can decide the right policy.
    static_assert(noexcept(awaiter_.await_ready()));
    return awaiter_.await_ready();
  }

  // `noexcept` forces any rethrown exceptions to `std::terminate`
  auto await_resume() noexcept -> decltype(awaiter_.await_resume()) {
    return awaiter_.await_resume();
  }

  // `noexcept` here as well, because the underlying awaitable might
  // have a throwing `await_suspend`, and those exceptions propagate
  // to the parent coro promise, bypassing `await_resume`.
  // Demo: https://godbolt.org/z/Edfj8P8be
  template <typename Promise>
  auto await_suspend(coroutine_handle<Promise> coro) noexcept
      -> decltype(awaiter_.await_suspend(coro)) {
    return awaiter_.await_suspend(coro);
  }
};

template <typename T>
class [[FOLLY_ATTR_CLANG_CORO_AWAIT_ELIDABLE]] NoexceptAwaitable
    : public CommutativeWrapperAwaitable<NoexceptAwaitable, T> {
 public:
  using CommutativeWrapperAwaitable<NoexceptAwaitable, T>::
      CommutativeWrapperAwaitable;

  template <typename T2 = T, std::enable_if_t<is_awaitable_v<T2>, int> = 0>
  NoexceptAwaiter<T> operator co_await() && {
    return NoexceptAwaiter<T>{std::move(this->inner_)};
  }

  using folly_private_noexcept_awaitable_t = std::true_type;
};

} // namespace detail

#if FOLLY_HAS_IMMOVABLE_COROUTINES

template <typename Inner>
class AsNoexcept;
// NB: While it'd be prettier to have `AsNoexcept` branch on whether the inner
// task has an executor, a separate template is much simpler.
template <typename Inner>
class AsNoexceptWithExecutor;

namespace detail {
template <typename Inner>
struct AsNoexceptWithExecutorCfg {
  using InnerTaskWithExecutorT = Inner;
  using WrapperTaskT =
      AsNoexcept<typename Inner::folly_private_task_without_executor_t>;
  template <typename Awaitable> // library-internal, meant to be by-rref
  static inline auto wrapAwaitable(Awaitable&& awaitable) noexcept {
    // Assert can be removed, I was concerned if we accidentally double-wrap
    static_assert(!noexcept_awaitable_v<Awaitable>);
    return detail::NoexceptAwaitable<Awaitable>{
        mustAwaitImmediatelyUnsafeMover(static_cast<Awaitable&&>(awaitable))()};
  }
};
template <typename Inner>
using AsNoexceptWithExecutorBase = TaskWithExecutorWrapperCrtp<
    AsNoexceptWithExecutor<Inner>,
    AsNoexceptWithExecutorCfg<Inner>>;
} // namespace detail

template <typename Inner>
class FOLLY_NODISCARD AsNoexceptWithExecutor final
    : public detail::AsNoexceptWithExecutorBase<Inner> {
 protected:
  using detail::AsNoexceptWithExecutorBase<Inner>::AsNoexceptWithExecutorBase;

 public:
  using folly_private_noexcept_awaitable_t = std::true_type;
};

namespace detail {

template <typename... BaseArgs>
class AsNoexceptTaskPromiseWrapper final
    : public TaskPromiseWrapper<BaseArgs...> {};

template <typename Inner>
struct AsNoexceptCfg {
  using ValueT = semi_await_result_t<Inner>;
  using InnerTaskT = Inner;
  using TaskWithExecutorT = AsNoexceptWithExecutor<decltype(co_withExecutor(
      FOLLY_DECLVAL(Executor::KeepAlive<>), FOLLY_DECLVAL(Inner)))>;
  using PromiseT = AsNoexceptTaskPromiseWrapper<
      ValueT,
      AsNoexcept<Inner>,
      typename Inner::promise_type>;
  template <typename Awaitable> // library-internal, meant to be by-rref
  static inline auto wrapAwaitable(Awaitable&& awaitable) noexcept {
    // Assert can be removed, I was concerned if we accidentally double-wrap
    static_assert(!noexcept_awaitable_v<Awaitable>);
    return detail::NoexceptAwaitable<Awaitable>{
        static_cast<Awaitable&&>(awaitable)};
  }
};

template <typename Inner>
using AsNoexceptBase = TaskWrapperCrtp<AsNoexcept<Inner>, AsNoexceptCfg<Inner>>;

} // namespace detail

template <typename Inner>
class FOLLY_CORO_TASK_ATTRS AsNoexcept final
    : public detail::AsNoexceptBase<Inner> {
 protected:
  using detail::AsNoexceptBase<Inner>::AsNoexceptBase;

 public:
  using folly_private_noexcept_awaitable_t = std::true_type;
};

#endif // FOLLY_HAS_IMMOVABLE_COROUTINES

} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES
