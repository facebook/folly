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

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <system_error>

#include <folly/Portability.h>
#include <folly/lang/Exception.h>

namespace folly {

//  upgrade_lock
//
//  A lock-holder type which holds upgrade locks, usable with any shared mutex
//  type which supports the upgrade state. Similar to std::unique_lock vis-a-vis
//  all mutex types or to std::shared_lock vis-a-vis all shared mutex types.
//
//  As long as the associated mutex type exposes members in the style:
//  - lock_upgrade void()
//  - try_lock_upgrade bool()
//  - try_lock_upgrade_for bool(std::chrono::duration<...> const&)
//  - try_lock_upgrade_until bool(std::chrono::time_point<...> const&)
//  - unlock_upgrade
//
//  Upgrade locks are not useful by themselves; they are primarily useful since
//  upgrade locks may be transitioned atomically to exclusive locks. This lock-
//  holder type works with the transition_to_... functions below to facilitate
//  atomic transition from ugprade lock to exclusive lock.
template <typename Mutex>
class upgrade_lock {
 public:
  using mutex_type = Mutex;

  upgrade_lock() noexcept = default;
  upgrade_lock(upgrade_lock&& that) noexcept
      : mutex_{std::exchange(that.mutex_, nullptr)},
        owns_{std::exchange(that.owns_, false)} {}
  upgrade_lock(mutex_type& mutex) : mutex_{&mutex}, owns_{true} {
    mutex.lock_upgrade();
  }
  upgrade_lock(mutex_type& mutex, std::defer_lock_t) noexcept
      : mutex_{&mutex} {}
  upgrade_lock(mutex_type& mutex, std::try_to_lock_t)
      : mutex_{&mutex}, owns_{mutex.try_lock_upgrade()} {}
  upgrade_lock(mutex_type& mutex, std::adopt_lock_t)
      : mutex_{&mutex}, owns_{true} {}
  template <typename Rep, typename Period>
  upgrade_lock(
      mutex_type& mutex, std::chrono::duration<Rep, Period> const& timeout)
      : mutex_{&mutex}, owns_{mutex.try_lock_upgrade_for(timeout)} {}
  template <typename Clock, typename Duration>
  upgrade_lock(
      mutex_type& mutex,
      std::chrono::time_point<Clock, Duration> const& deadline)
      : mutex_{&mutex}, owns_{mutex.try_lock_upgrade_until(deadline)} {}

  ~upgrade_lock() {
    if (owns_) {
      mutex_->unlock_upgrade();
    }
  }

  upgrade_lock& operator=(upgrade_lock&& that) noexcept {
    if (owns_lock()) {
      unlock();
    }
    mutex_ = std::exchange(that.mutex_, nullptr);
    owns_ = std::exchange(that.owns_, false);
    return *this;
  }

  void lock() {
    check<false>();
    mutex_->lock_upgrade();
    owns_ = true;
  }

  bool try_lock() {
    check<false>();
    return owns_ = mutex_->try_lock_upgrade();
  }

  template <typename Rep, typename Period>
  bool try_lock_for(std::chrono::duration<Rep, Period> const& timeout) {
    check<false>();
    return owns_ = mutex_->try_lock_upgrade_for(timeout);
  }

  template <typename Clock, typename Duration>
  bool try_lock_until(
      std::chrono::time_point<Clock, Duration> const& deadline) {
    check<false>();
    return owns_ = mutex_->try_lock_upgrade_until(deadline);
  }

  void unlock() {
    check<true>();
    mutex_->unlock_upgrade();
    owns_ = false;
  }

  void swap(upgrade_lock& that) noexcept {
    std::swap(mutex_, that.mutex_);
    std::swap(owns_, that.owns_);
  }

  friend void swap(upgrade_lock& a, upgrade_lock& b) noexcept { a.swap(b); }

  mutex_type* release() noexcept {
    owns_ = false;
    return std::exchange(mutex_, nullptr);
  }

  mutex_type* mutex() const noexcept { return mutex_; }

  bool owns_lock() const noexcept { return owns_; }

  explicit operator bool() const noexcept { return owns_; }

 private:
  template <bool Owns>
  void check() {
    if (!mutex_ || owns_ != Owns) {
      check_<Owns>();
    }
  }

  template <bool Owns>
  [[noreturn]] FOLLY_NOINLINE void check_() {
    throw_exception<std::system_error>(std::make_error_code(
        !mutex_ || !owns_ ? std::errc::operation_not_permitted
                          : std::errc::resource_deadlock_would_occur));
  }

  mutex_type* mutex_{nullptr};
  bool owns_{false};
};

namespace detail {

template <
    template <typename>
    class To,
    template <typename>
    class From,
    typename Mutex,
    typename Transition>
To<Mutex> try_transition_lock_(From<Mutex>& lock, Transition transition) {
  auto owns = lock.owns_lock();
  if (!lock.mutex()) {
    return To<Mutex>{};
  }
  if (!owns) {
    return To<Mutex>{*lock.release(), std::defer_lock};
  }
  if (!transition(*lock.mutex())) {
    return To<Mutex>{};
  }
  return To<Mutex>{*lock.release(), std::adopt_lock};
}

template <
    template <typename>
    class To,
    template <typename>
    class From,
    typename Mutex,
    typename Transition>
To<Mutex> transition_lock_(From<Mutex>& lock, Transition transition) {
  return try_transition_lock_<To>(lock, [&](auto& l) { //
    return transition(l), true;
  });
}

} // namespace detail

//  transition_to_shared_lock(unique_lock)
//
//  Wraps mutex member function unlock_and_lock_shared.
//
//  Represents an immediate atomic downgrade transition from exclusive lock to
//  to shared lock.
template <typename Mutex>
std::shared_lock<Mutex> transition_to_shared_lock(
    std::unique_lock<Mutex>& lock) {
  return detail::transition_lock_<std::shared_lock>(lock, [](auto& _) { //
    _.unlock_and_lock_shared();
  });
}

//  transition_to_shared_lock(upgrade_lock)
//
//  Wraps mutex member function unlock_upgrade_and_lock_shared.
//
//  Represents an immediate atomic downgrade transition from upgrade lock to
//  shared lock.
template <typename Mutex>
std::shared_lock<Mutex> transition_to_shared_lock( //
    upgrade_lock<Mutex>& lock) {
  return detail::transition_lock_<std::shared_lock>(lock, [](auto& _) { //
    _.unlock_upgrade_and_lock_shared();
  });
}

//  transition_to_upgrade_lock(unique_lock)
//
//  Wraps mutex member function unlock_and_lock_upgrade.
//
//  Represents an immediate atomic downgrade transition from unique lock to
//  upgrade lock.
template <typename Mutex>
upgrade_lock<Mutex> transition_to_upgrade_lock( //
    std::unique_lock<Mutex>& lock) {
  return detail::transition_lock_<upgrade_lock>(lock, [](auto& _) { //
    _.unlock_and_lock_upgrade();
  });
}

//  transition_to_unique_lock(upgrade_lock)
//
//  Wraps mutex member function unlock_upgrade_and_lock.
//
//  Represents an eventual atomic upgrade transition from upgrade lock to unique
//  lock.
template <typename Mutex>
std::unique_lock<Mutex> transition_to_unique_lock( //
    upgrade_lock<Mutex>& lock) {
  return detail::transition_lock_<std::unique_lock>(lock, [](auto& _) { //
    _.unlock_upgrade_and_lock();
  });
}

//  try_transition_to_unique_lock(upgrade_lock)
//
//  Wraps mutex member function try_unlock_upgrade_and_lock.
//
//  Represents an immediate attempted atomic upgrade transition from upgrade
//  lock to unique lock.
template <typename Mutex>
std::unique_lock<Mutex> try_transition_to_unique_lock(
    upgrade_lock<Mutex>& lock) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [](auto& _) { //
    return _.try_unlock_upgrade_and_lock();
  });
}

//  try_transition_to_unique_lock_for(upgrade_lock)
//
//  Wraps mutex member function try_unlock_upgrade_and_lock_for.
//
//  Represents an eventual attempted atomic upgrade transition from upgrade
//  lock to unique lock.
template <typename Mutex, typename Rep, typename Period>
std::unique_lock<Mutex> try_transition_to_unique_lock_for(
    upgrade_lock<Mutex>& lock,
    std::chrono::duration<Rep, Period> const& timeout) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [&](auto& _) {
    return _.try_unlock_upgrade_and_lock_for(timeout);
  });
}

//  try_transition_to_unique_lock_until(upgrade_lock)
//
//  Wraps mutex member function try_unlock_upgrade_and_lock_until.
//
//  Represents an eventual attemped atomic upgrade transition from upgrade
//  lock to unique lock.
template <typename Mutex, typename Clock, typename Duration>
std::unique_lock<Mutex> try_transition_to_unique_lock_until(
    upgrade_lock<Mutex>& lock,
    std::chrono::time_point<Clock, Duration> const& deadline) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [&](auto& _) {
    return _.try_unlock_upgrade_and_lock_until(deadline);
  });
}

} // namespace folly
