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

#include <mutex>
#include <folly/synchronization/detail/Sleeper.h>

namespace folly {
namespace fibers {

namespace detail {

template <class BatonType>
MutexWaiter<BatonType>::~MutexWaiter() {
  if (posted_.load(std::memory_order_acquire)) {
    return;
  }
  // If baton supported retrying wait() after wait()/try_wait_until(), like
  // SaturatingSemaphore, this could simply be replaced with baton_.wait(), and
  // posted_ could be removed.
  folly::detail::Sleeper sleeper;
  while (!posted_.load(std::memory_order_acquire)) {
    sleeper.wait();
  }
}

template <class BatonType>
void MutexWaiter<BatonType>::wait() {
  baton_.wait();
}

template <class BatonType>
template <class Deadline>
bool MutexWaiter<BatonType>::try_wait_until(Deadline deadline) {
  return baton_.try_wait_until(deadline);
}

template <class BatonType>
void MutexWaiter<BatonType>::wake() {
  baton_.post();
  posted_.store(true, std::memory_order_release);
}

} // namespace detail

//
// TimedMutex implementation
//

template <typename WaitFunc>
TimedMutex::LockResult TimedMutex::lockHelper(WaitFunc&& waitFunc) {
  std::unique_lock ulock(lock_);
  if (!locked_) {
    locked_ = true;
    return LockResult::SUCCESS;
  }

  const auto isOnFiber = options_.stealing_ && onFiber();

  if (!isOnFiber && notifiedFiber_ != nullptr) {
    // lock() was called on a thread and while some other fiber was already
    // notified, it hasn't be run yet. We steal the lock from that fiber then
    // to avoid potential deadlock.
    DCHECK(threadWaiters_.empty());
    notifiedFiber_ = nullptr;
    return LockResult::SUCCESS;
  }

  // Delay constructing the waiter until it is actually required.
  // This makes a huge difference, at least in the benchmarks,
  // when the mutex isn't locked.
  MutexWaiter waiter;
  MutexWaiterList& waiters = isOnFiber ? fiberWaiters_ : threadWaiters_;
  waiters.push_back(waiter);

  ulock.unlock();

  if (!waitFunc(waiters, waiter)) {
    return LockResult::TIMEOUT;
  }

  if (isOnFiber) {
    auto lockStolen = [&] {
      std::lock_guard lg(lock_);

      auto stolen = notifiedFiber_ != &waiter;
      if (!stolen) {
        notifiedFiber_ = nullptr;
      }
      return stolen;
    }();

    if (lockStolen) {
      return LockResult::STOLEN;
    }
  }

  return LockResult::SUCCESS;
}

inline void TimedMutex::lock() {
  LockResult result;
  do {
    result = lockHelper([](MutexWaiterList&, MutexWaiter& waiter) {
      waiter.wait();
      return true;
    });

    DCHECK(result != LockResult::TIMEOUT);
  } while (result != LockResult::SUCCESS);
}

template <typename Rep, typename Period>
bool TimedMutex::try_lock_for(
    const std::chrono::duration<Rep, Period>& timeout) {
  return try_lock_until(std::chrono::steady_clock::now() + timeout);
}

template <typename Clock, typename Duration>
bool TimedMutex::try_lock_until(
    const std::chrono::time_point<Clock, Duration>& deadline) {
  auto result = lockHelper([&](MutexWaiterList& waiters, MutexWaiter& waiter) {
    if (!waiter.try_wait_until(deadline)) {
      // We timed out. Two cases:
      // 1. We're still in the waiter list and we truly timed out
      // 2. We're not in the waiter list anymore. This could happen if the baton
      //    times out but the mutex is unlocked before we reach this code. In
      //    this
      //    case we'll pretend we got the lock on time.
      std::unique_lock lg(lock_);
      if (waiter.hook.is_linked()) {
        waiters.erase(waiters.iterator_to(waiter));
        lg.unlock();
        waiter.wake(); // Ensure that destruction doesn't block.
        return false;
      }
    }
    return true;
  });

  switch (result) {
    case LockResult::SUCCESS:
      return true;
    case LockResult::TIMEOUT:
      return false;
    case LockResult::STOLEN:
      // We don't respect the duration if lock was stolen
      lock();
      return true;
  }
  assume_unreachable();
}

inline bool TimedMutex::try_lock() {
  std::lock_guard lg(lock_);
  if (locked_) {
    return false;
  }
  locked_ = true;
  return true;
}

inline void TimedMutex::unlock() {
  std::unique_lock lg(lock_);
  if (!threadWaiters_.empty()) {
    auto& to_wake = threadWaiters_.front();
    threadWaiters_.pop_front();
    lg.unlock();
    to_wake.wake();
    return;
  }

  if (!fiberWaiters_.empty()) {
    auto& to_wake = fiberWaiters_.front();
    fiberWaiters_.pop_front();
    notifiedFiber_ = &to_wake;
    lg.unlock();
    to_wake.wake();
    return;
  }

  locked_ = false;
}

//
// TimedRWMutexImpl implementation
//

template <bool ReaderPriority, typename BatonType>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::shouldReadersWait() const {
  return state_ == State::WRITE_LOCKED ||
      (!ReaderPriority && !write_waiters_.empty());
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::lock_shared() {
  std::unique_lock ulock{lock_};
  if (shouldReadersWait()) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    ulock.unlock();
    waiter.wait();
    if (folly::kIsDebug) {
      std::unique_lock assertLock{lock_};
      assert(state_ == State::READ_LOCKED);
    }
    return;
  }
  assert(
      (state_ == State::UNLOCKED && readers_ == 0) ||
      (state_ == State::READ_LOCKED && readers_ > 0));
  assert(read_waiters_.empty());
  state_ = State::READ_LOCKED;
  readers_ += 1;
}

template <bool ReaderPriority, typename BatonType>
template <typename Rep, typename Period>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock_shared_for(
    const std::chrono::duration<Rep, Period>& timeout) {
  return try_lock_shared_until(std::chrono::steady_clock::now() + timeout);
}

template <bool ReaderPriority, typename BatonType>
template <typename Clock, typename Duration>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock_shared_until(
    const std::chrono::time_point<Clock, Duration>& deadline) {
  std::unique_lock ulock{lock_};
  if (shouldReadersWait()) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    ulock.unlock();

    if (!waiter.try_wait_until(deadline)) {
      // We timed out. Two cases:
      // 1. We're still in the waiter list and we truly timed out
      // 2. We're not in the waiter list anymore. This could happen if the baton
      //    times out but the mutex is unlocked before we reach this code. In
      //    this case we'll pretend we got the lock on time.
      std::unique_lock guard{lock_};
      if (waiter.hook.is_linked()) {
        read_waiters_.erase(read_waiters_.iterator_to(waiter));
        guard.unlock();
        waiter.wake(); // Ensure that destruction doesn't block.
        return false;
      }
    }
    return true;
  }
  assert(
      (state_ == State::UNLOCKED && readers_ == 0) ||
      (state_ == State::READ_LOCKED && readers_ > 0));
  assert(read_waiters_.empty());
  state_ = State::READ_LOCKED;
  readers_ += 1;
  return true;
}

template <bool ReaderPriority, typename BatonType>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock_shared() {
  std::unique_lock guard{lock_};
  if (!shouldReadersWait()) {
    assert(
        (state_ == State::UNLOCKED && readers_ == 0) ||
        (state_ == State::READ_LOCKED && readers_ > 0));
    assert(read_waiters_.empty());
    state_ = State::READ_LOCKED;
    readers_ += 1;
    return true;
  }
  return false;
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock_shared() {
  if (kIsDebug) {
    std::unique_lock ulock{lock_};
    assert(state_ == State::READ_LOCKED);
  }
  unlock_();
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::lock() {
  std::unique_lock ulock{lock_};
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    return;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  ulock.unlock();
  waiter.wait();
}

template <bool ReaderPriority, typename BatonType>
template <typename Rep, typename Period>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock_for(
    const std::chrono::duration<Rep, Period>& timeout) {
  return try_lock_until(std::chrono::steady_clock::now() + timeout);
}

template <bool ReaderPriority, typename BatonType>
template <typename Clock, typename Duration>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock_until(
    const std::chrono::time_point<Clock, Duration>& deadline) {
  std::unique_lock ulock{lock_};
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    return true;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  ulock.unlock();

  if (!waiter.try_wait_until(deadline)) {
    // We timed out. Two cases:
    // 1. We're still in the waiter list and we truly timed out
    // 2. We're not in the waiter list anymore. This could happen if the baton
    //    times out but the mutex is unlocked before we reach this code. In
    //    this case we'll pretend we got the lock on time.
    std::unique_lock guard{lock_};
    if (waiter.hook.is_linked()) {
      write_waiters_.erase(write_waiters_.iterator_to(waiter));
      guard.unlock();
      waiter.wake(); // Ensure that destruction doesn't block.
      return false;
    }
  }
  assert(state_ == State::WRITE_LOCKED);
  return true;
}

template <bool ReaderPriority, typename BatonType>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock() {
  std::lock_guard guard{lock_};
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    return true;
  }
  return false;
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock() {
  if (kIsDebug) {
    std::unique_lock ulock{lock_};
    assert(state_ == State::WRITE_LOCKED);
  }
  unlock_();
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock_() {
  std::unique_lock guard{lock_};
  assert(state_ != State::UNLOCKED);
  assert(
      (state_ == State::READ_LOCKED && readers_ > 0) ||
      (state_ == State::WRITE_LOCKED && readers_ == 0));
  if (state_ == State::READ_LOCKED) {
    readers_ -= 1;
  }

  if (!read_waiters_.empty() && (ReaderPriority || write_waiters_.empty())) {
    assert(
        state_ == State::WRITE_LOCKED && readers_ == 0 &&
        "read waiters can only accumulate while write locked");
    state_ = State::READ_LOCKED;
    readers_ = read_waiters_.size();

    MutexWaiterList waiters_to_wake = std::move(read_waiters_);
    guard.unlock();

    while (!waiters_to_wake.empty()) {
      MutexWaiter& to_wake = waiters_to_wake.front();
      waiters_to_wake.pop_front();
      to_wake.wake();
    }

    return;
  }

  if (readers_ == 0) {
    if (!write_waiters_.empty()) {
      assert(read_waiters_.empty() || !ReaderPriority);
      state_ = State::WRITE_LOCKED;

      // Wake a single writer (after releasing the spin lock)
      MutexWaiter& to_wake = write_waiters_.front();
      write_waiters_.pop_front();
      guard.unlock();
      to_wake.wake();
      return;
    }

    verify_unlocked_properties();
    state_ = State::UNLOCKED;
    return;
  }

  assert(state_ == State::READ_LOCKED);
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock_and_lock_shared() {
  std::unique_lock guard{lock_};
  assert(state_ == State::WRITE_LOCKED && readers_ == 0);
  state_ = State::READ_LOCKED;
  readers_ += 1;

  if (!read_waiters_.empty()) {
    readers_ += read_waiters_.size();

    MutexWaiterList waiters_to_wake = std::move(read_waiters_);
    guard.unlock();

    while (!waiters_to_wake.empty()) {
      MutexWaiter& to_wake = waiters_to_wake.front();
      waiters_to_wake.pop_front();
      to_wake.wake();
    }
  }
}
} // namespace fibers
} // namespace folly
