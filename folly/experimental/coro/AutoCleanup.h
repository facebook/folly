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

#include <folly/experimental/coro/AutoCleanup-fwd.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Task.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

namespace detail {
template <typename T>
struct ScopeExitArg {
  explicit ScopeExitArg(T&) {}

  folly::coro::Task<> cleanup() && { co_return; }

  void update(T&) {}
};
} // namespace detail

/// The user can use AutoCleanup to wrap arguments passed to a
/// CleanableAsyncGenerator. When the coroutine promise of
/// CleanableAsyncGenerator is created it will automatically attach
/// co_scope_exit task that performs async cleanup for all the arguments wrapped
/// in AutoCleanup. This allows to ensure cleanup of the arguments even when
/// next() of the CleanableAsyncGenerator is never co_awaited.
///
/// Example usage:
///  folly::coro::CleanableAsyncGenerator<std::pair<int, int>> zip(
///      folly::coro::AutoCleanup<folly::coro::CleanableAsyncGenerator<int>> a,
///      folly::coro::AutoCleanup<folly::coro::CleanableAsyncGenerator<int>> b
///  ) {
///    while (true) {
///      auto x = co_await a->next();
///      if (!x) {
///        break;
///      }
///      auto y = co_await b->next();
///      if (!y) {
///        break;
///      }
///      co_yield std::make_pair(*x, *y);
///    }
///  }
///
/// In the example above, a and b will be cleaned up automatically when
/// cleanup() of the zip genertor is co_awaited, even if next() of the zip
/// generator have been never co_awaited.

template <typename T, typename CleanupFn>
class AutoCleanup : MoveOnly {
 public:
  using type = T;
  using cleanup_fn = CleanupFn;

  explicit AutoCleanup(T&& object, CleanupFn cleanupFn = CleanupFn{}) noexcept
      : ptr_{std::addressof(object)}, cleanupFn_{std::move(cleanupFn)} {}

  AutoCleanup(const AutoCleanup&) = delete;

  AutoCleanup(AutoCleanup&& other) noexcept
      : ptr_{std::exchange(other.ptr_, nullptr)},
        cleanupFn_{std::move(other.cleanupFn_)} {}

  ~AutoCleanup() { DCHECK(!kIsDebug || ptr_ == nullptr || scheduled_); }

  AutoCleanup& operator=(const AutoCleanup&) = delete;

  AutoCleanup& operator=(AutoCleanup&&) = delete;

  std::add_lvalue_reference_t<T> operator*() const
      noexcept(noexcept(*std::declval<T*>())) {
    return *get();
  }

  T* operator->() const noexcept { return get(); }

  T* get() const noexcept {
    DCHECK(!kIsDebug || scheduled_);
    return ptr_;
  }

 private:
  T* ptr_;
  CleanupFn cleanupFn_;
  FOLLY_ATTR_NO_UNIQUE_ADDRESS conditional_t<kIsDebug, bool, std::false_type>
      scheduled_{};

  friend struct detail::ScopeExitArg<AutoCleanup<T, CleanupFn>>;
};

namespace detail {

template <typename T, typename CleanupFn>
struct ScopeExitArg<AutoCleanup<T, CleanupFn>> {
  T object;
  CleanupFn cleanupFn;

  explicit ScopeExitArg(AutoCleanup<T, CleanupFn>& autoCleanup)
      : object{std::move(*autoCleanup.ptr_)},
        cleanupFn{autoCleanup.cleanupFn_} {}

  auto cleanup() && { return cleanupFn(std::move(object)); }

  void update(AutoCleanup<T, CleanupFn>& autoCleanup) {
    autoCleanup.ptr_ = std::addressof(object);
    if constexpr (kIsDebug) {
      DCHECK(!autoCleanup.scheduled_);
      autoCleanup.scheduled_ = true;
    }
  }
};

template <typename Promise, typename Action, typename... Args>
auto attachScopeExit(
    coroutine_handle<Promise> coro, Action&& action, Args&&... args) {
  auto scopeExitAwaiter = co_viaIfAsync(
      nullptr,
      co_scope_exit(
          static_cast<Action&&>(action), static_cast<Args&&>(args)...));
  auto ready = scopeExitAwaiter.await_ready();
  DCHECK(!ready);
  auto suspend = scopeExitAwaiter.await_suspend(coro);
  DCHECK(!suspend);
  return scopeExitAwaiter.await_resume();
}

template <typename Promise, typename... Args>
void scheduleAutoCleanup(coroutine_handle<Promise> coro, Args&... args) {
  auto result = attachScopeExit(
      coro,
      [](auto&&... scopeExitArgs) -> Task<> {
        co_await collectAll(std::move(scopeExitArgs).cleanup()...);
      },
      detail::ScopeExitArg<Args>(args)...);
  std::apply([&](auto&... objs) { ((objs.update(args)), ...); }, result);
}

} // namespace detail

} // namespace folly::coro

#endif
