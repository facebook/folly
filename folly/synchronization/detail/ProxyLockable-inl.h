/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <cassert>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <folly/Optional.h>
#include <folly/Portability.h>

namespace folly {
namespace detail {
namespace proxylockable_detail {
template <typename Bool>
void throwIfAlreadyLocked(Bool&& locked) {
  if (kIsDebug && locked) {
    throw std::system_error{
        std::make_error_code(std::errc::resource_deadlock_would_occur)};
  }
}

template <typename Bool>
void throwIfNotLocked(Bool&& locked) {
  if (kIsDebug && !locked) {
    throw std::system_error{
        std::make_error_code(std::errc::operation_not_permitted)};
  }
}

template <typename Bool>
void throwIfNoMutex(Bool&& mutex) {
  if (kIsDebug && !mutex) {
    throw std::system_error{
        std::make_error_code(std::errc::operation_not_permitted)};
  }
}
} // namespace proxylockable_detail

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::~ProxyLockableUniqueLock() {
  if (owns_lock()) {
    unlock();
  }
}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(mutex_type& mutex)
    : mutex_{std::addressof(mutex)}, state_{mutex.lock()} {}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    ProxyLockableUniqueLock&& a) noexcept
    : mutex_{std::exchange(a.mutex_, nullptr)},
      state_{std::exchange(a.state_, state_type{})} {}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>& ProxyLockableUniqueLock<Mutex>::operator=(
    ProxyLockableUniqueLock&& other) noexcept {
  if (owns_lock()) {
    unlock();
  }
  mutex_ = std::exchange(other.mutex_, nullptr);
  state_ = std::exchange(other.state_, state_type{});
  return *this;
}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    mutex_type& mutex, std::adopt_lock_t, const state_type& state)
    : mutex_{std::addressof(mutex)}, state_{state} {
  proxylockable_detail::throwIfNotLocked(state_);
}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    mutex_type& mutex, std::defer_lock_t) noexcept
    : mutex_{std::addressof(mutex)} {}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    mutex_type& mutex, std::try_to_lock_t)
    : mutex_{std::addressof(mutex)}, state_{mutex.try_lock()} {}

template <typename Mutex>
template <typename Rep, typename Period>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    mutex_type& mutex, const std::chrono::duration<Rep, Period>& timeout)
    : mutex_{std::addressof(mutex)}, state_{mutex.try_lock_for(timeout)} {}

template <typename Mutex>
template <typename Clock, typename Duration>
ProxyLockableUniqueLock<Mutex>::ProxyLockableUniqueLock(
    mutex_type& mutex, const std::chrono::time_point<Clock, Duration>& deadline)
    : mutex_{std::addressof(mutex)}, state_{mutex.try_lock_until(deadline)} {}

template <typename Mutex>
void ProxyLockableUniqueLock<Mutex>::lock() {
  proxylockable_detail::throwIfAlreadyLocked(state_);
  proxylockable_detail::throwIfNoMutex(mutex_);

  state_ = mutex_->lock();
}

template <typename Mutex>
void ProxyLockableUniqueLock<Mutex>::unlock() {
  proxylockable_detail::throwIfNoMutex(mutex_);
  proxylockable_detail::throwIfNotLocked(state_);

  const auto& state = state_;
  mutex_->unlock(state);
  state_ = state_type{};
}

template <typename Mutex>
bool ProxyLockableUniqueLock<Mutex>::try_lock() {
  proxylockable_detail::throwIfNoMutex(mutex_);
  proxylockable_detail::throwIfAlreadyLocked(state_);

  state_ = mutex_->try_lock();
  return !!state_;
}

template <typename Mutex>
template <typename Rep, typename Period>
bool ProxyLockableUniqueLock<Mutex>::try_lock_for(
    const std::chrono::duration<Rep, Period>& timeout) {
  proxylockable_detail::throwIfNoMutex(mutex_);
  proxylockable_detail::throwIfAlreadyLocked(state_);

  state_ = mutex_->try_lock_for(timeout);
  return !!state_;
}

template <typename Mutex>
template <typename Clock, typename Duration>
bool ProxyLockableUniqueLock<Mutex>::try_lock_until(
    const std::chrono::time_point<Clock, Duration>& deadline) {
  proxylockable_detail::throwIfNoMutex(mutex_);
  proxylockable_detail::throwIfAlreadyLocked(state_);

  state_ = mutex_->try_lock_until(deadline);
  return !!state_;
}

template <typename Mutex>
void ProxyLockableUniqueLock<Mutex>::swap(
    ProxyLockableUniqueLock& other) noexcept {
  std::swap(mutex_, other.mutex_);
  std::swap(state_, other.state_);
}

template <typename Mutex>
typename ProxyLockableUniqueLock<Mutex>::mutex_type*
ProxyLockableUniqueLock<Mutex>::mutex() const noexcept {
  return mutex_;
}

template <typename Mutex>
typename ProxyLockableUniqueLock<Mutex>::state_type const&
ProxyLockableUniqueLock<Mutex>::state() const noexcept {
  return state_;
}

template <typename Mutex>
typename ProxyLockableUniqueLock<Mutex>::state_type&
ProxyLockableUniqueLock<Mutex>::state() noexcept {
  return state_;
}

template <typename Mutex>
bool ProxyLockableUniqueLock<Mutex>::owns_lock() const noexcept {
  return !!state_;
}

template <typename Mutex>
ProxyLockableUniqueLock<Mutex>::operator bool() const noexcept {
  return owns_lock();
}

template <typename Mutex>
ProxyLockableLockGuard<Mutex>::ProxyLockableLockGuard(mutex_type& mutex)
    : ProxyLockableUniqueLock<Mutex>{mutex} {}

} // namespace detail
} // namespace folly
