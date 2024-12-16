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

#include <folly/coro/Task.h>

/// The header provides base classes for wrapping `folly::coro::Task` with
/// custom functionality.  These work by composition, which avoids the
/// pitfalls of inheritance -- your custom wrapper will not be "is-a-Task",
/// and will not implicitly "object slice" to a `Task`.
///
/// Keep in mind that some destructive APIs, like `.semi()`, effectively
/// unwrap the `Task`.  If this is important for your use-case, you may need
/// to add features (e.g.  `TaskWithExecutorWrapper`, on-unwrap callbacks).
///
/// The point of this header is to uniformly forward the large API surface
/// of `TaskPromise` & `Task`, leaving just the "new logic" in each wrapper.
/// As `Task.h` evolves, a central `TaskWrapper.h` is easier to maintain.
///
/// You'll derive from `TaskWrapperPromise` -- which must reference a
/// derived class of `TaskWrapperCrtp` that is your new user-facing coro.
///
/// Please use `FOLLY_CORO_TASK_ATTRS` on your `Task` wrapper class.
/// Caveat: this assumes that the coro's caller will outlive it.  That is
/// true for `Task`, and almost certainly true of all sensible wrapper
/// types.  If you have an analog of `TaskWithExecutor`, also mark that
/// `FOLLY_NODISCARD`.
///
/// To discourage inheritance and object-slicing bugs, mark your derived
/// wrappers `final` -- they can be wrapped recursively.
///
/// Read `TaskWrapperTest.cpp` for examples of a minimal & recursive wrapper.
///
/// Future: Once this has a benchmark, see if `FOLLY_ALWAYS_INLINE` makes
/// any difference on the wrapped functions (it shouldn't).

namespace folly::coro {

namespace detail {
template <typename, typename, typename>
class TaskPromiseWrapperBase;
} // namespace detail

template <typename, typename, typename>
class TaskWrapperCrtp;

namespace detail {

template <typename Wrapper>
using task_wrapper_underlying_semiawaitable_t =
    typename Wrapper::TaskWrapperUnderlyingSemiAwaitable;

template <typename SemiAwaitable, typename T>
inline constexpr bool is_task_or_wrapper_v =
    (!std::is_same_v<nonesuch, SemiAwaitable> && // Does not wrap Task
     (std::is_same_v<SemiAwaitable, Task<T>> || // Wraps Task
      is_task_or_wrapper_v<
          detected_t<task_wrapper_underlying_semiawaitable_t, SemiAwaitable>,
          T>));

template <typename Wrapper>
using task_wrapper_underlying_promise_t =
    typename Wrapper::TaskWrapperUnderlyingPromise;

template <typename Promise, typename T>
inline constexpr bool is_task_promise_or_wrapper_v =
    (!std::is_same_v<nonesuch, Promise> && // Does not wrap TaskPromise
     (std::is_same_v<Promise, TaskPromise<T>> || // Wraps TaskPromise
      is_task_promise_or_wrapper_v<
          detected_t<task_wrapper_underlying_promise_t, Promise>,
          T>));

template <typename T, typename WrappedSemiAwaitable, typename Promise>
class TaskPromiseWrapperBase {
 protected:
  static_assert(
      is_task_or_wrapper_v<WrappedSemiAwaitable, T>,
      "SemiAwaitable must be a sequence of wrappers ending in Task<T>");
  static_assert(
      is_task_promise_or_wrapper_v<Promise, T>,
      "Promise must be a sequence of wrappers ending in TaskPromise<T>");

  Promise promise_;

  TaskPromiseWrapperBase() noexcept = default;
  ~TaskPromiseWrapperBase() = default;

 public:
  using TaskWrapperUnderlyingPromise = Promise;

  WrappedSemiAwaitable get_return_object() noexcept {
    return WrappedSemiAwaitable{promise_.get_return_object()};
  }

  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }
  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  auto initial_suspend() noexcept { return promise_.initial_suspend(); }
  auto final_suspend() noexcept { return promise_.final_suspend(); }

  auto await_transform(auto&& what) {
    return promise_.await_transform(std::forward<decltype(what)>(what));
  }

  auto yield_value(auto&& v)
    requires requires { promise_.yield_value(std::forward<decltype(v)>(v)); }
  {
    return promise_.yield_value(std::forward<decltype(v)>(v));
  }

  void unhandled_exception() noexcept { promise_.unhandled_exception(); }

  // These getters are all interposed for `TaskPromiseBase::FinalAwaiter`
  decltype(auto) result() { return promise_.result(); }
  decltype(auto) getAsyncFrame() { return promise_.getAsyncFrame(); }
  auto& scopeExitRef(TaskPromisePrivate tag) {
    return promise_.scopeExitRef(tag);
  }
  auto& continuationRef(TaskPromisePrivate tag) {
    return promise_.continuationRef(tag);
  }
  auto& executorRef(TaskPromisePrivate tag) {
    return promise_.executorRef(tag);
  }
};

template <typename T, typename SemiAwaitable, typename Promise>
class TaskPromiseWrapper
    : public TaskPromiseWrapperBase<T, SemiAwaitable, Promise> {
 protected:
  TaskPromiseWrapper() noexcept = default;
  ~TaskPromiseWrapper() = default;

 public:
  template <typename U = T> // see `returnImplicitCtor` test
  auto return_value(U&& value) {
    return this->promise_.return_value(std::forward<U>(value));
  }
};

template <typename SemiAwaitable, typename Promise>
class TaskPromiseWrapper<void, SemiAwaitable, Promise>
    : public TaskPromiseWrapperBase<void, SemiAwaitable, Promise> {
 protected:
  TaskPromiseWrapper() noexcept = default;
  ~TaskPromiseWrapper() = default;

 public:
  void return_void() noexcept { this->promise_.return_void(); }
};

} // namespace detail

// Inherit from this instead of `TaskWrapperCrtp` if you don't want your
// wrapped task to be awaitable without being `unwrap()`ped first.
template <typename Derived, typename T, typename SemiAwaitable>
class OpaqueTaskWrapperCrtp {
 private:
  static_assert(
      detail::is_task_or_wrapper_v<SemiAwaitable, T>,
      "*TaskWrapperCrtp must wrap a sequence of wrappers ending in Task<T>");

  SemiAwaitable task_;

 protected:
  template <typename, typename, typename>
  friend class ::folly::coro::detail::TaskPromiseWrapperBase;

  explicit OpaqueTaskWrapperCrtp(SemiAwaitable t) : task_(std::move(t)) {}

  SemiAwaitable unwrap() && { return std::move(task_); }

 public:
  using TaskWrapperUnderlyingSemiAwaitable = SemiAwaitable;
};

template <typename Derived, typename T, typename SemiAwaitable>
class TaskWrapperCrtp
    : public OpaqueTaskWrapperCrtp<Derived, T, SemiAwaitable> {
 protected:
  template <typename, typename, typename>
  friend class ::folly::coro::detail::TaskPromiseWrapperBase;

  using OpaqueTaskWrapperCrtp<Derived, T, SemiAwaitable>::OpaqueTaskWrapperCrtp;

 public:
  // NB: In the future, this might ALSO produce a wrapped object.
  FOLLY_NODISCARD
  TaskWithExecutor<T> scheduleOn(Executor::KeepAlive<> executor) && noexcept {
    return std::move(*this).unwrap().scheduleOn(std::move(executor));
  }

  FOLLY_NOINLINE auto semi() && { return std::move(*this).unwrap().semi(); }

  friend Derived co_withCancellation(
      folly::CancellationToken cancelToken, Derived&& tw) noexcept {
    return Derived{
        co_withCancellation(std::move(cancelToken), std::move(tw).unwrap())};
  }

  friend auto co_viaIfAsync(
      folly::Executor::KeepAlive<> executor, Derived&& tw) noexcept {
    return co_viaIfAsync(std::move(executor), std::move(tw).unwrap());
  }
  // At least in Clang 15, the `static_assert` isn't enough to get a usable
  // error message (it is instantiated too late), but the deprecation
  // warning does show up.
  [[deprecated(
      "Error: Use `co_await std::move(lvalue)`, not `co_await lvalue`.")]]
  friend Derived co_viaIfAsync(folly::Executor::KeepAlive<>, const Derived&) {
    static_assert("Use `co_await std::move(lvalue)`, not `co_await lvalue`.");
  }
};

} // namespace folly::coro
