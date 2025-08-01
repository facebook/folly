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

#include <folly/coro/AwaitImmediately.h>
#include <folly/coro/Task.h>

/// `TaskWrapper.h` provides base classes for wrapping `folly::coro::Task` with
/// custom functionality.  These work by composition, which avoids the pitfalls
/// of inheritance -- your custom wrapper will not be "is-a-Task", and will not
/// implicitly "object slice" to a `Task`.
///
/// The point of this header is to uniformly forward the large API surface of
/// `Task`, `TaskWithExecutor`, and `TaskPromise`, leaving just the "new logic"
/// in each wrapper's implementation.
///
///   - `TaskWrapperCrtp` makes your type (1)  a coroutine (`promise_type`)
///     that can `co_await` other `folly::coro` objects.  (2) semi-awaitable by
///     other `folly::coro` coroutines.  It has the following features:
///        * `co_await`ability (using `co_viaIfAsync`)
///        * Interoperates with `folly::coro` awaitable wrappers like
///          `co_awaitTry` and `co_nothrow`.
///        * `co_withCancellation` to add a cancellation token
///        * `co_withExecutor` to add a cancellation token
///        * Basic reflection via `folly/coro/Traits.h`
///        * Empty base optimization for zero runtime overhead
///
///   - `TaskWithExecutorWrapperCrtp` is awaitable, but not a coroutine.  It
///     has the same features, except for `co_withExecutor`.
///
/// ### WARNING: Do not blindly forward more APIs in `TaskWrapper.h`!
///
/// Several existing wrappers are immediately-awaitable (`AwaitImmediately.h`).
/// For those tasks (e.g. `now_task`), API forwarding is risky:
///   - Do NOT forward `semi()`, `start*()`, `unwrap()`, or other methods, or
///     CPOs that take the awaitable by-reference.  All of those make it
///     trivial to accidentally break the immediately-awaitable invariant, and
///     cause lifetime bugs.
///   - When forwarding an API, use either a static method or CPO.  Then,
///     either ONLY take the awaitable by-value, or bifurcate the API on
///     `must_await_immediately_v<Awaitable>`, grep for examples.
///
/// If you **have** to forward an unsafe API, here are some suggestions:
///   - Only add them in your wrapper.
///   - Add them via `UnsafeTaskWrapperCrtp` deriving from `TaskWrapperCrtp`.
///   - Add boolean flags to the configuration struct, and gate the methods via
///     `enable_if`.  NB: You probably cannot gate these on `Derived` **not**
///     being `MustAwaitImmediately`, since CRTP bases see an incomplete type.
///
/// ### WARNING: Beware of object slicing in "unwrapping" APIs
///
/// Start by reading "A note on object slicing" in `AwaitImmediately.h`.
///
/// If your wrapper is adding new members, or customizing object lifecycle
/// (dtor / copy / move / assignment), then you must:
///   - Write a custom `getUnsafeMover()`.
///   - Overload the protected `unsafeTask()` and `unsafeTaskWithExecutor()` to
///     reduce slicing risk.
///   - Take care not to slice down to the `Crtp` bases.
///
/// ### How to implement a wrapper
///
/// First, read the WARNINGs above. Then, follow one of the "Tiny" examples
/// in `TaskWrapperTest.cpp`. The important things are:
///   - Actually read the "object slicing" warning above!
///   - In most cases, you'll need to both implement a task, and customize its
///     `TaskWithExecutorT`.  If you leave that as `coro::TaskWithExecutor`,
///     some users will accidentally avoid your wrapper's effects.
///   - Tag `YourTaskWithExecutor` with `FOLLY_NODISCARD`.
///   - Tag `YourTask` with the `FOLLY_CORO_TASK_ATTRS` attribute.  Caveat:
///     This assumes that the coro's caller will outlive it.  That is true for
///     `Task`, and almost certainly true of all sensible wrapper types.
///   - Mark your wrappers `final` to discourage inheritance and object-slicing
///     bugs.  They can still be wrapped recursively.
///
/// Future: Once this has a benchmark, see if `FOLLY_ALWAYS_INLINE` makes
/// any difference on the wrapped functions (it shouldn't).

#if FOLLY_HAS_IMMOVABLE_COROUTINES

namespace folly::coro {

namespace detail {

template <typename Wrapper>
using task_wrapper_inner_semiawaitable_t =
    typename Wrapper::folly_private_task_wrapper_inner_t;

template <typename SemiAwaitable, typename T>
inline constexpr bool is_task_or_wrapper_v =
    (!std::is_same_v<nonesuch, SemiAwaitable> && // Does not wrap Task
     (std::is_same_v<SemiAwaitable, Task<T>> || // Wraps Task
      is_task_or_wrapper_v<
          detected_t<task_wrapper_inner_semiawaitable_t, SemiAwaitable>,
          T>));

template <typename Wrapper>
using task_wrapper_inner_promise_t = typename Wrapper::TaskWrapperInnerPromise;

template <typename Promise, typename T>
inline constexpr bool is_task_promise_or_wrapper_v =
    (!std::is_same_v<nonesuch, Promise> && // Does not wrap TaskPromise
     (std::is_same_v<Promise, TaskPromise<T>> || // Wraps TaskPromise
      is_task_promise_or_wrapper_v<
          detected_t<task_wrapper_inner_promise_t, Promise>,
          T>));

template <typename T, typename WrapperTask, typename Promise>
class TaskPromiseWrapperBase {
 protected:
  static_assert(
      is_task_or_wrapper_v<WrapperTask, T>,
      "SemiAwaitable must be a sequence of wrappers ending in Task<T>");
  static_assert(
      is_task_promise_or_wrapper_v<Promise, T>,
      "Promise must be a sequence of wrappers ending in TaskPromise<T>");

  Promise promise_;

  TaskPromiseWrapperBase() noexcept = default;
  ~TaskPromiseWrapperBase() = default;

 public:
  using TaskWrapperInnerPromise = Promise;

  WrapperTask get_return_object() noexcept {
    return WrapperTask{promise_.get_return_object()};
  }

  static void* operator new(std::size_t size) {
    return ::folly_coro_async_malloc(size);
  }
  static void operator delete(void* ptr, std::size_t size) {
    ::folly_coro_async_free(ptr, size);
  }

  auto initial_suspend() noexcept { return promise_.initial_suspend(); }
  auto final_suspend() noexcept { return promise_.final_suspend(); }

  template <
      typename Awaitable,
      std::enable_if_t<!must_await_immediately_v<Awaitable>, int> = 0>
  auto await_transform(Awaitable&& what) {
    return promise_.await_transform(std::forward<Awaitable>(what));
  }
  template <
      typename Awaitable,
      std::enable_if_t<must_await_immediately_v<Awaitable>, int> = 0>
  auto await_transform(Awaitable what) {
    return promise_.await_transform(
        mustAwaitImmediatelyUnsafeMover(std::move(what))());
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

template <typename T, typename WrapperTask, typename Promise>
class TaskPromiseWrapper
    : public TaskPromiseWrapperBase<T, WrapperTask, Promise> {
 protected:
  TaskPromiseWrapper() noexcept = default;
  ~TaskPromiseWrapper() = default;

 public:
  template <typename U = T> // see "`co_return` with implicit ctor" test
  auto return_value(U&& value) {
    return this->promise_.return_value(std::forward<U>(value));
  }
};

template <typename WrapperTask, typename Promise>
class TaskPromiseWrapper<void, WrapperTask, Promise>
    : public TaskPromiseWrapperBase<void, WrapperTask, Promise> {
 protected:
  TaskPromiseWrapper() noexcept = default;
  ~TaskPromiseWrapper() = default;

 public:
  void return_void() noexcept { this->promise_.return_void(); }
};

// Mixin for TaskWrapper.h configs for `Task` & `TaskWithExecutor` types
struct DoesNotWrapAwaitable {
  template <typename Awaitable>
  static inline constexpr Awaitable&& wrapAwaitable(Awaitable&& awaitable) {
    return static_cast<Awaitable&&>(awaitable);
  }
};

} // namespace detail

// IMPORTANT: Read "Do not blindly forward more APIs" in the file docblock.  In
// a nutshell, adding methods, or by-ref CPOs, can compromise the safety of
// immediately-awaitable wrappers, so DON'T DO THAT.
template <typename Derived, typename Cfg>
class TaskWrapperCrtp {
 public:
  using promise_type = typename Cfg::PromiseT;

  // Pass `tw` by-value, since `&&` would break immediately-awaitable types
  friend typename Cfg::TaskWithExecutorT co_withExecutor(
      Executor::KeepAlive<> executor, Derived tw) noexcept {
    return typename Cfg::TaskWithExecutorT{
        co_withExecutor(std::move(executor), std::move(tw).unwrapTask())};
  }

  // Pass `tw` by-value, since `&&` would break immediately-awaitable types
  friend Derived co_withCancellation(
      CancellationToken cancelToken, Derived tw) noexcept {
    return Derived{co_withCancellation(
        std::move(cancelToken), std::move(tw).unwrapTask())};
  }

  // Pass `tw` by-value, since `&&` would break immediately-awaitable types
  // Has copy-pasta below in `TaskWithExecutorWrapperCrtp`.
  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor, Derived tw) noexcept {
    return Cfg::wrapAwaitable(co_viaIfAsync(
        std::move(executor),
        mustAwaitImmediatelyUnsafeMover(std::move(tw).unwrapTask())()));
  }

  // No `cpo_t<co_withAsyncStack>` since a "Task" is not an awaitable.

  auto getUnsafeMover(ForMustAwaitImmediately p) && noexcept {
    // See "A note on object slicing" above `mustAwaitImmediatelyUnsafeMover`
    static_assert(sizeof(Derived) == sizeof(typename Cfg::InnerTaskT));
    static_assert( // More `noexcept` tests in `MustAwaitImmediatelyUnsafeMover`
        noexcept(std::move(*this).unwrapTask().getUnsafeMover(p)));
    return MustAwaitImmediatelyUnsafeMover{
        (Derived*)nullptr, std::move(*this).unwrapTask().getUnsafeMover(p)};
  }

  using folly_private_task_wrapper_inner_t = typename Cfg::InnerTaskT;
  using folly_private_task_wrapper_crtp_base = TaskWrapperCrtp;

  // Wrappers can override these as-needed
  using folly_private_must_await_immediately_t =
      must_await_immediately_t<typename Cfg::InnerTaskT>;
  using folly_private_noexcept_awaitable_t =
      noexcept_awaitable_t<typename Cfg::InnerTaskT>;
  template <safe_alias Default>
  using folly_private_safe_alias_t =
      safe_alias_of<folly_private_task_wrapper_inner_t, Default>;

 private:
  using Inner = folly_private_task_wrapper_inner_t;
  static_assert(
      detail::is_task_or_wrapper_v<Inner, typename Cfg::ValueT>,
      "*TaskWrapper must wrap a sequence of wrappers ending in Task<T>");

  Inner task_;

 protected:
  template <typename, typename, typename> // can construct
  friend class ::folly::coro::detail::TaskPromiseWrapperBase;
  friend class MustAwaitImmediatelyUnsafeMover< // can construct
      Derived,
      detail::unsafe_mover_for_must_await_immediately_t<Inner>>;

  explicit TaskWrapperCrtp(Inner t)
      // `mustAwaitImmediatelyUnsafeMover` has more `noexcept` assertions.
      noexcept(noexcept(Inner{FOLLY_DECLVAL(Inner)}))
      : task_(mustAwaitImmediatelyUnsafeMover(std::move(t))()) {
    static_assert(
        must_await_immediately_v<Derived> ||
            !must_await_immediately_v<typename Cfg::TaskWithExecutorT>,
        "`TaskWithExecutorT` must `AddMustAwaitImmediately` because the inner "
        "task did");
  }

  // See "A note on object slicing" above `mustAwaitImmediatelyUnsafeMover`
  Inner unwrapTask() && noexcept {
    static_assert(sizeof(Inner) == sizeof(Derived));
    return mustAwaitImmediatelyUnsafeMover(std::move(task_))();
  }
};

// IMPORTANT: Read "Do not blindly forward more APIs" in the file docblock.  In
// a nutshell, adding methods, or by-ref CPOs, can compromise the safety of
// immediately-awaitable wrappers, so DON'T DO THAT.
template <typename Derived, typename Cfg>
class TaskWithExecutorWrapperCrtp {
 private:
  using Inner = typename Cfg::InnerTaskWithExecutorT;
  Inner inner_;

 protected:
  friend class MustAwaitImmediatelyUnsafeMover< // can construct
      Derived,
      detail::unsafe_mover_for_must_await_immediately_t<Inner>>;

  // See "A note on object slicing" above `mustAwaitImmediatelyUnsafeMover`
  Inner unwrapTaskWithExecutor() && noexcept {
    static_assert(sizeof(Inner) == sizeof(Derived));
    return mustAwaitImmediatelyUnsafeMover(std::move(inner_))();
  }

  // Our task can construct us, and that logic lives in the CRTP base
  friend typename Cfg::WrapperTaskT::folly_private_task_wrapper_crtp_base;

  explicit TaskWithExecutorWrapperCrtp(Inner t)
      // `mustAwaitImmediatelyUnsafeMover` has more `noexcept` assertions.
      noexcept(noexcept(Inner{FOLLY_DECLVAL(Inner)}))
      : inner_(mustAwaitImmediatelyUnsafeMover(std::move(t))()) {}

 public:
  // This is a **deliberately undefined** declaration. It is provided so that
  // `await_result_t` can work, e.g. `AsyncScope` checks that for all tasks.
  //
  // We do NOT want a definition here, for two reasons:
  //   - As a destructive member function, this can easily violate the
  //     "immediately awaitable" invariant -- all you have to do is
  //     `twe.operator co_await()`.
  //   - A definition would have to handle `Cfg::wrapAwaitable`, but also avoid
  //     double-wrapping the awaitable (*if* that can occur?).  No definition
  //     means I don't have to think through this :)
  //
  // If, in the future, something requires `get_awaiter()` to handle a wrapped
  // task-with-executor in an **evaluated** context, we can then provide the
  // definition, being mindful of the above concerns.
  //
  // NB: Adding a definition should not let this naively wrong code compile --
  // that goes through `await_transform()`.  `NowTaskTest.cpp` checks this.
  //   auto t = co_withExecutor(ex, someNowTask());
  //   co_await std::move(t);
  auto operator co_await() && noexcept
      -> decltype(Cfg::wrapAwaitable(std::move(inner_)).operator co_await());

  // Pass `twe` by-value, since `&&` would break immediately-awaitable types
  friend Derived co_withCancellation(
      CancellationToken cancelToken, Derived twe) noexcept {
    return Derived{co_withCancellation(
        std::move(cancelToken),
        mustAwaitImmediatelyUnsafeMover(std::move(twe.inner_))())};
  }

  // Pass `twe` by-value, since `&&` would break immediately-awaitable types
  // Has copy-pasta above in `TaskWrapperCrtp`.
  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor, Derived twe) noexcept {
    return Cfg::wrapAwaitable(co_viaIfAsync(
        std::move(executor),
        mustAwaitImmediatelyUnsafeMover(std::move(twe.inner_))()));
  }

  // `AsyncScope` requires an awaitable with an executor already attached, and
  // thus directly calls `co_withAsyncStack` instead of `co_viaIfAsync`.  But,
  // we still need to wrap the awaitable on that code path.
  //
  // NB: Passing by-&& here looks like it could compromise the safety of
  // immediately-awaitable coros (`now_task`, `now_task_with_executor`).  With
  // by-value, `BlockingWaitTest.AwaitNowTaskWithExecutor` would not build.
  //
  // Supporting pass-by-value would require fixing a LOT of plumbing.
  //   - `WithAsyncStack.h` calls `is_tag_invocable_v`, which would fail on
  //     `now_task_with_executor` if this is by-value, since the implementation
  //     of `is_tag_invocable_v` presents all args by-&&.
  //   - `CommutativeWrapperAwaitable` and `StackAwareViaIfAsyncAwaiter`,
  //     among others, also assume that `co_withAsyncStack` takes by-ref.
  //
  // Fortunately, I'm not aware of any practical reduction in
  // immediately-awaitable safety from this issue.  `co_withAsyncStack` should
  // never be called in user code.  Internal usage in `folly/coro` looks
  // overall immediately-awaitable-safe -- and the best safeguard for any
  // particular scenario is to test, see e.g. `NowTaskTest.blockingWait`.
  friend auto tag_invoke(cpo_t<co_withAsyncStack>, Derived&& twe) noexcept(
      noexcept(co_withAsyncStack(FOLLY_DECLVAL(Inner)))) {
    return Cfg::wrapAwaitable(co_withAsyncStack(std::move(twe.inner_)));
  }

  auto getUnsafeMover(ForMustAwaitImmediately p) && noexcept {
    // See "A note on object slicing" above `mustAwaitImmediatelyUnsafeMover`
    static_assert(sizeof(Derived) == sizeof(Inner));
    static_assert( // More `noexcept` tests in `MustAwaitImmediatelyUnsafeMover`
        noexcept(std::move(inner_).getUnsafeMover(p)));
    return MustAwaitImmediatelyUnsafeMover{
        (Derived*)nullptr, std::move(inner_).getUnsafeMover(p)};
  }

  using folly_private_must_await_immediately_t =
      must_await_immediately_t<Inner>;
  using folly_private_task_without_executor_t = typename Cfg::WrapperTaskT;
  template <safe_alias Default>
  using folly_private_safe_alias_t = safe_alias_of<Inner, Default>;
};

} // namespace folly::coro

#endif
