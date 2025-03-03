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

#if FOLLY_HAS_IMMOVABLE_COROUTINES

namespace folly {
enum class safe_alias;
}
namespace folly::coro {

template <typename>
class NowTask;

namespace detail {

template <typename T>
class NowTaskPromise final
    : public TaskPromiseWrapper<T, NowTask<T>, TaskPromise<T>> {};

template <auto>
auto bind_captures_to_closure(auto&&, auto);

} // namespace detail

template <safe_alias, typename>
class BackgroundTask;

// IMPORTANT: This omits `start()` because that would destroy reference
// lifetime guarantees expected of `NowTask`. Use `BackgroundTask.h`.
template <typename T>
class FOLLY_NODISCARD NowTaskWithExecutor final : private MustAwaitImmediately {
 protected:
  template <safe_alias, typename>
  friend class BackgroundTask; // for `unwrapTaskWithExecutor`, remove later
  TaskWithExecutor<T> unwrapTaskWithExecutor() && { return std::move(inner_); }

  template <typename>
  friend class NowTask; // can construct

  explicit NowTaskWithExecutor(TaskWithExecutor<T> t) : inner_(std::move(t)) {}

 public:
  // Required for `await_result_t` to work.
  // NB: Even though this is rvalue-qualified, this does not wrongly allow
  // `co_await std::move(myNowTask().scheduleOn(ex));`, see `withExecutor` test
  auto operator co_await() && noexcept {
    return std::move(inner_).operator co_await();
  }

  NowTaskWithExecutor unsafeMoveMustAwaitImmediately() && {
    return NowTaskWithExecutor{std::move(inner_)};
  }

  friend NowTaskWithExecutor co_withCancellation(
      folly::CancellationToken cancelToken, NowTaskWithExecutor te) noexcept {
    return NowTaskWithExecutor{
        co_withCancellation(std::move(cancelToken), std::move(te.inner_))};
  }

  friend auto co_viaIfAsync(
      Executor::KeepAlive<> executor, NowTaskWithExecutor te) noexcept {
    return co_viaIfAsync(std::move(executor), std::move(te.inner_));
  }

 private:
  TaskWithExecutor<T> inner_;
};

/// `NowTask<T>` quacks like `Task<T>` but is nonmovable, and therefore
/// must be `co_await`ed in the same expression that created it.
///
/// Defaulting to `NowTask` brings considerable safety benefits.  With
/// `Task`, the following would be anti-patterns that cause dangling
/// reference bugs, but with `NowTask`, C++ lifetime extension rules ensure
/// that they simply work.
///   - Pass-by-reference into coroutines.
///   - Ephemeral coro lambdas with captures.
///   - Coro lambdas with capture-by-reference.
///
/// Notes:
///   - (subject to change) Unlike `SafeTask`, `NowTask` does NOT check
///     `safe_alias_of_v` for the return type `T`.  The rationale is that
///     `NowTask` is essentially an immediate async function, i.e. it
///     satisfies the structured concurrency maxim of "lexical scope drives
///     both control flow & lifetime".  That shrinks the odds that returned
///     pointers/references are unexpectedly invalid.  The one failure mode
///     I can think of is that the pointed-to-data gets invalidated by a
///     concurrent thread of execution, but in that case the program almost
///     certainly has a data race -- regardless of the lifetime bug -- and
///     that requires runtime instrumentation (like TSAN) to detect in
///     present-day C++.
template <typename T>
class FOLLY_CORO_TASK_ATTRS NowTask final
    : public OpaqueTaskWrapperCrtp<NowTask<T>, T, Task<T>>,
      private MustAwaitImmediately {
 public:
  using promise_type = detail::NowTaskPromise<T>;

  // If `makeNowTask().scheduleOn()` is movable, it defeats our purpose.
  NowTaskWithExecutor<T> scheduleOn(Executor::KeepAlive<> exec) && noexcept {
    return NowTaskWithExecutor<T>{
        std::move(*this).unwrap().scheduleOn(std::move(exec))};
  }

  explicit NowTask(Task<T> t)
      : OpaqueTaskWrapperCrtp<NowTask<T>, T, Task<T>>(std::move(t)) {}

  friend auto co_withCancellation(
      folly::CancellationToken cancelToken, NowTask tw) noexcept {
    return NowTask{
        co_withCancellation(std::move(cancelToken), std::move(tw).unwrap())};
  }

  friend auto co_viaIfAsync(
      folly::Executor::KeepAlive<> executor, NowTask tw) noexcept {
    return co_viaIfAsync(std::move(executor), std::move(tw).unwrap());
  }

  // IMPORTANT: If you add support for any more customization points here,
  // you must be sure to branch each callable on `is_must_await_immediately_v`
  // as is done for those above.  The key point is that
  // `MustAwaitImmediately` types MUST be taken by value, never by `&&`.

  auto unsafeMoveMustAwaitImmediately() && {
    return NowTask{std::move(*this).unwrap()};
  }

 protected:
  template <typename U>
  friend auto toNowTask(NowTask<U> t); // for `unwrap`
  // `async_now_closure` wraps `NowTask`s into `NowTask`s
  template <auto>
  friend auto detail::bind_captures_to_closure(auto&&, auto); // for `unwrap`
};

// NB: `toNowTask(SafeTask)` is in `SafeTask.h` to avoid circular deps.
template <typename T>
auto toNowTask(Task<T> t) {
  return NowTask<T>{std::move(t)};
}
template <typename T>
auto toNowTask(NowTask<T> t) {
  return NowTask<T>{std::move(t).unwrap()};
}

} // namespace folly::coro

#endif
