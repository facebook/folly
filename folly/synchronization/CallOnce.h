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

#include <atomic>
#include <utility>

#include <folly/Likely.h>
#include <folly/MicroLock.h>
#include <folly/Portability.h>
#include <folly/SharedMutex.h>
#include <folly/functional/Invoke.h>

namespace folly {

//  basic_once_flag
//
//  The flag template to be used with call_once. Parameterizable by the mutex
//  type and atomic template. The mutex type is required to mimic std::mutex and
//  the atomic type is required to mimic std::atomic.
template <typename Mutex, template <typename> class Atom = std::atomic>
class basic_once_flag;

//  compact_once_flag
//
//  An alternative flag template that can be used with call_once that uses only
//  1 byte. Internally, compact_once_flag uses folly::MicroLock and its data
//  storage API.
class compact_once_flag;

//  call_once
//
//  Drop-in replacement for std::call_once.
//
//  The libstdc++ implementation has two flaws:
//  * it lacks a fast path, and
//  * it deadlocks (in explicit violation of the standard) when invoked twice
//    with a given flag, and the callable passed to the first invocation throws.
//
//  This implementation corrects both flaws.
//
//  The tradeoff is a slightly larger once_flag struct at 8 bytes, vs 4 bytes
//  with libstdc++ on Linux/x64. However, you may use folly::compact_once_flag
//  which is 1 byte.
//
//  Does not work with std::once_flag.
//
//  mimic: std::call_once
template <typename OnceFlag, typename F, typename... Args>
FOLLY_ALWAYS_INLINE void call_once(OnceFlag& flag, F&& f, Args&&... args) {
  if (LIKELY(flag.test_once())) {
    return;
  }
  flag.call_once_slow(std::forward<F>(f), std::forward<Args>(args)...);
}

//  try_call_once
//
//  Like call_once, but using a boolean return type to signal pass/fail rather
//  than throwing exceptions.
//
//  Returns true if any previous call to try_call_once with the same once_flag
//  has returned true or if any previous call to call_once with the same
//  once_flag has completed without throwing an exception or if the function
//  passed as an argument returns true; otherwise returns false.
//
//  Note: This has no parallel in the std::once_flag interface.
template <typename OnceFlag, typename F, typename... Args>
FOLLY_NODISCARD FOLLY_ALWAYS_INLINE bool try_call_once(
    OnceFlag& flag, F&& f, Args&&... args) noexcept {
  static_assert(is_nothrow_invocable_v<F, Args...>, "must be noexcept");
  if (LIKELY(flag.test_once())) {
    return true;
  }
  return flag.try_call_once_slow(
      std::forward<F>(f), std::forward<Args>(args)...);
}

//  test_once
//
//  Tests whether any invocation to call_once with the given flag has succeeded.
//
//  May help with space usage in certain esoteric scenarios compared with caller
//  code tracking a separate and possibly-padded bool.
//
//  Note: This has no parallel in the std::once_flag interface.
template <typename OnceFlag>
FOLLY_ALWAYS_INLINE bool test_once(OnceFlag const& flag) noexcept {
  return flag.test_once();
}

//  reset_once
//
//  Resets a flag.
//
//  Warning: unsafe to call concurrently with any other flag operations.
template <typename OnceFlag>
FOLLY_ALWAYS_INLINE void reset_once(OnceFlag& flag) noexcept {
  return flag.reset_once();
}

template <typename Mutex, template <typename> class Atom>
class basic_once_flag {
 public:
  constexpr basic_once_flag() noexcept = default;
  basic_once_flag(const basic_once_flag&) = delete;
  basic_once_flag& operator=(const basic_once_flag&) = delete;

 private:
  template <typename OnceFlag, typename F, typename... Args>
  friend void call_once(OnceFlag&, F&&, Args&&...);

  template <typename OnceFlag>
  friend bool test_once(OnceFlag const& flag) noexcept;

  template <typename OnceFlag>
  friend void reset_once(OnceFlag&) noexcept;

  template <typename F, typename... Args>
  FOLLY_NOINLINE void call_once_slow(F&& f, Args&&... args) {
    std::lock_guard<Mutex> lock(mutex_);
    if (called_.load(std::memory_order_relaxed)) {
      return;
    }
    invoke(std::forward<F>(f), std::forward<Args>(args)...);
    called_.store(true, std::memory_order_release);
  }

  template <typename OnceFlag, typename F, typename... Args>
  friend bool try_call_once(OnceFlag&, F&&, Args&&...) noexcept;

  template <typename F, typename... Args>
  FOLLY_NOINLINE bool try_call_once_slow(F&& f, Args&&... args) noexcept {
    std::lock_guard<Mutex> lock(mutex_);
    if (called_.load(std::memory_order_relaxed)) {
      return true;
    }
    auto const pass = invoke(std::forward<F>(f), std::forward<Args>(args)...);
    called_.store(pass, std::memory_order_release);
    return pass;
  }

  FOLLY_ALWAYS_INLINE bool test_once() const noexcept {
    return called_.load(std::memory_order_acquire);
  }

  FOLLY_ALWAYS_INLINE void reset_once() noexcept {
    called_.store(false, std::memory_order_relaxed);
  }

  Atom<bool> called_{false};
  Mutex mutex_;
};

class compact_once_flag {
 public:
  compact_once_flag() = default;
  compact_once_flag(const compact_once_flag&) = delete;
  compact_once_flag& operator=(const compact_once_flag&) = delete;

 private:
  template <typename OnceFlag, typename F, typename... Args>
  friend void call_once(OnceFlag&, F&&, Args&&...);

  template <typename OnceFlag>
  friend bool test_once(OnceFlag const& flag) noexcept;

  template <typename OnceFlag>
  friend void reset_once(OnceFlag&) noexcept;

  template <typename F, typename... Args>
  FOLLY_NOINLINE void call_once_slow(F&& f, Args&&... args) {
    folly::MicroLock::LockGuardWithData guard(mutex_);
    if (guard.loadedValue() != 0) {
      return;
    }
    invoke(std::forward<F>(f), std::forward<Args>(args)...);
    guard.storeValue(1);
  }

  template <typename OnceFlag, typename F, typename... Args>
  friend bool try_call_once(OnceFlag&, F&&, Args&&...) noexcept;

  template <typename F, typename... Args>
  FOLLY_NOINLINE bool try_call_once_slow(F&& f, Args&&... args) noexcept {
    folly::MicroLock::LockGuardWithData guard(mutex_);
    if (guard.loadedValue() != 0) {
      return true;
    }
    const auto pass = static_cast<bool>(
        invoke(std::forward<F>(f), std::forward<Args>(args)...));
    guard.storeValue(pass ? 1 : 0);
    return pass;
  }

  FOLLY_ALWAYS_INLINE bool test_once() const noexcept {
    return mutex_.load(std::memory_order_acquire) != 0;
  }

  FOLLY_ALWAYS_INLINE void reset_once() noexcept {
    folly::MicroLock::LockGuardWithData guard(mutex_);
    guard.storeValue(0);
  }

  folly::MicroLock mutex_;
};

static_assert(
    sizeof(compact_once_flag) == 1, "compact_once_flag should be 1 byte");

//  once_flag
//
//  The flag type to be used with call_once. An instance of basic_once_flag.
//
//  Does not work with std::call_once.
//
//  mimic: std::once_flag
using once_flag = basic_once_flag<SharedMutex>;

} // namespace folly
