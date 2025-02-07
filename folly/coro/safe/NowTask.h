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
class FOLLY_NODISCARD NowTaskWithExecutor final
    : private NonCopyableNonMovable {
 protected:
  template <safe_alias, typename>
  friend class BackgroundTask; // see `CanUnwrapNowTask` below
  TaskWithExecutor<T> unwrapTaskWithExecutor() && { return std::move(inner_); }

  template <typename>
  friend class NowTask; // can construct

  explicit NowTaskWithExecutor(TaskWithExecutor<T> t) : inner_(std::move(t)) {}

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
    : public TaskWrapperCrtp<NowTask<T>, T, Task<T>>,
      private NonCopyableNonMovable {
 public:
  using promise_type = detail::NowTaskPromise<T>;

  // If `makeNowTask().scheduleOn()` is movable, it defeats our purpose.
  FOLLY_NODISCARD
  NowTaskWithExecutor<T> scheduleOn(Executor::KeepAlive<> exec) && noexcept {
    return NowTaskWithExecutor<T>{
        std::move(*this).unwrap().scheduleOn(std::move(exec))};
  }

  explicit NowTask(Task<T> t)
      : TaskWrapperCrtp<NowTask<T>, T, Task<T>>(std::move(t)) {}

 protected:
  // These 3 `friend`s (+ 1 above) are for `unwrap()`.  If this list grows,
  // introduce a `CanUnwrapNowTask` passkey type.
  template <typename U>
  friend auto toNowTask(NowTask<U> t);
  // `async_now_closure` wraps `NowTask`s into `NowTask`s
  template <auto>
  friend auto detail::bind_captures_to_closure(auto&&, auto);
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
