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

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <system_error>

#include <folly/Portability.h>
#include <folly/lang/Exception.h>

namespace folly {

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
      mutex_type& mutex,
      std::chrono::duration<Rep, Period> const& timeout)
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

  friend void swap(upgrade_lock& a, upgrade_lock& b) noexcept {
    a.swap(b);
  }

  mutex_type* release() noexcept {
    owns_ = false;
    return std::exchange(mutex_, nullptr);
  }

  mutex_type* mutex() const noexcept {
    return mutex_;
  }

  bool owns_lock() const noexcept {
    return owns_;
  }

  explicit operator bool() const noexcept {
    return owns_;
  }

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
    template <typename> class To,
    template <typename> class From,
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
    template <typename> class To,
    template <typename> class From,
    typename Mutex,
    typename Transition>
To<Mutex> transition_lock_(From<Mutex>& lock, Transition transition) {
  return try_transition_lock_<To>(lock, [&](auto& l) { //
    return transition(l), true;
  });
}

} // namespace detail

template <typename Mutex>
std::shared_lock<Mutex> transition_to_shared_lock(
    std::unique_lock<Mutex>& lock) {
  return detail::transition_lock_<std::shared_lock>(lock, [](auto& _) { //
    _.unlock_and_lock_shared();
  });
}

template <typename Mutex>
std::shared_lock<Mutex> transition_to_shared_lock( //
    upgrade_lock<Mutex>& lock) {
  return detail::transition_lock_<std::shared_lock>(lock, [](auto& _) { //
    _.unlock_upgrade_and_lock_shared();
  });
}

template <typename Mutex>
upgrade_lock<Mutex> transition_to_upgrade_lock( //
    std::unique_lock<Mutex>& lock) {
  return detail::transition_lock_<upgrade_lock>(lock, [](auto& _) { //
    _.unlock_and_lock_upgrade();
  });
}

template <typename Mutex>
std::unique_lock<Mutex> transition_to_unique_lock( //
    upgrade_lock<Mutex>& lock) {
  return detail::transition_lock_<std::unique_lock>(lock, [](auto& _) { //
    _.unlock_upgrade_and_lock();
  });
}

template <typename Mutex>
std::unique_lock<Mutex> try_transition_to_unique_lock(
    upgrade_lock<Mutex>& lock) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [](auto& _) { //
    return _.try_unlock_upgrade_and_lock();
  });
}

template <typename Mutex, typename Rep, typename Period>
std::unique_lock<Mutex> try_transition_to_unique_lock_for(
    upgrade_lock<Mutex>& lock,
    std::chrono::duration<Rep, Period> const& timeout) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [&](auto& _) {
    return _.try_unlock_upgrade_and_lock_for(timeout);
  });
}

template <typename Mutex, typename Clock, typename Duration>
std::unique_lock<Mutex> try_transition_to_unique_lock_until(
    upgrade_lock<Mutex>& lock,
    std::chrono::time_point<Clock, Duration> const& deadline) {
  return detail::try_transition_lock_<std::unique_lock>(lock, [&](auto& _) {
    return _.try_unlock_upgrade_and_lock_until(deadline);
  });
}

} // namespace folly
