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
#include <utility>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

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
class FOLLY_NODISCARD SharedLock {
 public:
  SharedLock() noexcept : mutex_(nullptr), locked_(false) {}

  explicit SharedLock(Mutex& mutex, std::defer_lock_t) noexcept
      : mutex_(std::addressof(mutex)), locked_(false) {}

  explicit SharedLock(Mutex& mutex, std::adopt_lock_t) noexcept
      : mutex_(std::addressof(mutex)), locked_(true) {}

  explicit SharedLock(Mutex& mutex, std::try_to_lock_t) noexcept(
      noexcept(mutex.try_lock_shared()))
      : mutex_(std::addressof(mutex)), locked_(mutex.try_lock_shared()) {}

  SharedLock(SharedLock&& other) noexcept
      : mutex_(std::exchange(other.mutex_, nullptr)),
        locked_(std::exchange(other.locked_, false)) {}

  SharedLock(const SharedLock&) = delete;
  SharedLock& operator=(const SharedLock&) = delete;

  ~SharedLock() {
    if (locked_) {
      mutex_->unlock_shared();
    }
  }

  SharedLock& operator=(SharedLock&& other) noexcept {
    SharedLock temp(std::move(other));
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

  bool try_lock() noexcept(noexcept(mutex_->try_lock_shared())) {
    DCHECK(!locked_);
    DCHECK(mutex_ != nullptr);
    locked_ = mutex_->try_lock_shared();
    return locked_;
  }

  void unlock() noexcept(noexcept(mutex_->unlock_shared())) {
    DCHECK(locked_);
    locked_ = false;
    mutex_->unlock_shared();
  }

  void swap(SharedLock& other) noexcept {
    std::swap(mutex_, other.mutex_);
    std::swap(locked_, other.locked_);
  }

 private:
  Mutex* mutex_;
  bool locked_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
