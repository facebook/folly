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

#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/Task.h>
#include <folly/synchronization/Lock.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

namespace detail {

template <typename Mutex, typename Policy>
class FOLLY_NODISCARD LockBase {
 public:
  static_assert(std::is_same_v<
                bool,
                invoke_result_t<typename Policy::try_lock_fn, Mutex&>>);

  LockBase() noexcept : mutex_(nullptr), locked_(false) {}

  explicit LockBase(Mutex& mutex, std::defer_lock_t) noexcept
      : mutex_(std::addressof(mutex)), locked_(false) {}

  explicit LockBase(Mutex& mutex, std::adopt_lock_t) noexcept
      : mutex_(std::addressof(mutex)), locked_(true) {}

  explicit LockBase(Mutex& mutex, std::try_to_lock_t) noexcept(
      noexcept(typename Policy::try_lock_fn{}(mutex)))
      : mutex_(std::addressof(mutex)),
        locked_(typename Policy::try_lock_fn{}(mutex)) {}

  LockBase(LockBase&& other) noexcept
      : mutex_(std::exchange(other.mutex_, nullptr)),
        locked_(std::exchange(other.locked_, false)) {}

  LockBase(const LockBase&) = delete;
  LockBase& operator=(const LockBase&) = delete;

  ~LockBase() {
    if (locked_) {
      typename Policy::unlock_fn{}(*mutex_);
    }
  }

  LockBase& operator=(LockBase&& other) noexcept {
    LockBase temp(std::move(other));
    swap(temp);
    return *this;
  }

  Mutex* mutex() const noexcept { return mutex_; }

  Mutex* release() noexcept {
    locked_ = false;
    return std::exchange(mutex_, nullptr);
  }

  bool owns_lock() const noexcept { return locked_; }

  explicit operator bool() const noexcept { return owns_lock(); }

  bool try_lock() noexcept(noexcept(typename Policy::try_lock_fn{}(*mutex_))) {
    DCHECK(!locked_);
    DCHECK(mutex_ != nullptr);
    locked_ = typename Policy::try_lock{}(*mutex_);
    return locked_;
  }

  void unlock() noexcept(noexcept(typename Policy::unlock_fn{}(*mutex_))) {
    DCHECK(locked_);
    locked_ = false;
    typename Policy::unlock_fn{}(*mutex_);
  }

  void swap(LockBase& other) noexcept {
    std::swap(mutex_, other.mutex_);
    std::swap(locked_, other.locked_);
  }

 protected:
  Mutex* mutex_;
  bool locked_;
};

struct lock_policy_shared {
  using try_lock_fn = access::try_lock_shared_fn;
  using unlock_fn = access::unlock_shared_fn;
};
struct lock_policy_upgrade {
  using try_lock_fn = access::try_lock_upgrade_fn;
  using unlock_fn = access::unlock_upgrade_fn;
};
} // namespace detail

/// This type mirrors the interface of std::shared_lock as much as possible.
///
/// The main difference between this type and std::shared_lock is that this
/// type is designed to be used with asynchronous shared-mutex types where
/// the lock acquisition is an asynchronous operation.
///
/// TODO: Actually implement the .co_lock() method on this class.
///
/// Workaround for now is to use:
///   SharedLock<SharedMutex> lock{mutex, std::defer_lock};
///   ...
///   lock = co_await lock.mutex()->co_scoped_lock_shared();
template <typename Mutex>
class SharedLock : public detail::LockBase<Mutex, detail::lock_policy_shared> {
 public:
  using detail::LockBase<Mutex, detail::lock_policy_shared>::LockBase;
};

template <typename Mutex, typename... A>
explicit SharedLock(Mutex&, A const&...) -> SharedLock<Mutex>;

template <typename Mutex>
class UpgradeLock
    : public detail::LockBase<Mutex, detail::lock_policy_upgrade> {
 public:
  using detail::LockBase<Mutex, detail::lock_policy_upgrade>::LockBase;
};

template <typename Mutex, typename... A>
explicit UpgradeLock(Mutex&, A const&...) -> UpgradeLock<Mutex>;

/// Async version of the folly::transition_lock
/// TODO: add more transition policies beyond just from upgrade to exclusive
template <typename Mutex>
folly::coro::Task<std::unique_lock<Mutex>> co_transition_lock(
    UpgradeLock<Mutex>& lock) {
  if (lock.owns_lock()) {
    co_return co_await lock.release()->co_scoped_unlock_upgrade_and_lock();
  } else {
    co_return std::unique_lock<Mutex>{*lock.release(), std::defer_lock};
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
