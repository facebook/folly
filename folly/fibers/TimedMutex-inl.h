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

#include <mutex>

namespace folly {
namespace fibers {

//
// TimedMutex implementation
//

template <typename WaitFunc>
TimedMutex::LockResult TimedMutex::lockHelper(WaitFunc&& waitFunc) {
  std::unique_lock<folly::SpinLock> ulock(lock_);
  if (!locked_) {
    locked_ = true;
    return LockResult::SUCCESS;
  }

  const auto isOnFiber = onFiber();

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
  if (isOnFiber) {
    fiberWaiters_.push_back(waiter);
  } else {
    threadWaiters_.push_back(waiter);
  }

  ulock.unlock();

  if (!waitFunc(waiter)) {
    return LockResult::TIMEOUT;
  }

  if (isOnFiber) {
    auto lockStolen = [&] {
      std::lock_guard<folly::SpinLock> lg(lock_);

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
    result = lockHelper([](MutexWaiter& waiter) {
      waiter.baton.wait();
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
  auto result = lockHelper([&](MutexWaiter& waiter) {
    if (!waiter.baton.try_wait_until(deadline)) {
      // We timed out. Two cases:
      // 1. We're still in the waiter list and we truly timed out
      // 2. We're not in the waiter list anymore. This could happen if the baton
      //    times out but the mutex is unlocked before we reach this code. In
      //    this
      //    case we'll pretend we got the lock on time.
      std::lock_guard<folly::SpinLock> lg(lock_);
      if (waiter.hook.is_linked()) {
        waiter.hook.unlink();
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
  std::lock_guard<folly::SpinLock> lg(lock_);
  if (locked_) {
    return false;
  }
  locked_ = true;
  return true;
}

inline void TimedMutex::unlock() {
  std::lock_guard<folly::SpinLock> lg(lock_);
  if (!threadWaiters_.empty()) {
    auto& to_wake = threadWaiters_.front();
    threadWaiters_.pop_front();
    to_wake.baton.post();
  } else if (!fiberWaiters_.empty()) {
    auto& to_wake = fiberWaiters_.front();
    fiberWaiters_.pop_front();
    notifiedFiber_ = &to_wake;
    to_wake.baton.post();
  } else {
    locked_ = false;
  }
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
  std::unique_lock<folly::SpinLock> ulock{lock_};
  if (shouldReadersWait()) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    ulock.unlock();
    waiter.baton.wait();
    if (folly::kIsDebug) {
      std::unique_lock<folly::SpinLock> assertLock{lock_};
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
  std::unique_lock<folly::SpinLock> ulock{lock_};
  if (shouldReadersWait()) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    ulock.unlock();

    if (!waiter.baton.try_wait_until(deadline)) {
      // We timed out. Two cases:
      // 1. We're still in the waiter list and we truly timed out
      // 2. We're not in the waiter list anymore. This could happen if the baton
      //    times out but the mutex is unlocked before we reach this code. In
      //    this case we'll pretend we got the lock on time.
      std::lock_guard<SpinLock> guard{lock_};
      if (waiter.hook.is_linked()) {
        read_waiters_.erase(read_waiters_.iterator_to(waiter));
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
  std::lock_guard<SpinLock> guard{lock_};
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
    std::unique_lock<folly::SpinLock> ulock{lock_};
    assert(state_ == State::READ_LOCKED);
  }
  unlock_();
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::lock() {
  std::unique_lock<folly::SpinLock> ulock{lock_};
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    return;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  ulock.unlock();
  waiter.baton.wait();
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
  std::unique_lock<folly::SpinLock> ulock{lock_};
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    return true;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  ulock.unlock();

  if (!waiter.baton.try_wait_until(deadline)) {
    // We timed out. Two cases:
    // 1. We're still in the waiter list and we truly timed out
    // 2. We're not in the waiter list anymore. This could happen if the baton
    //    times out but the mutex is unlocked before we reach this code. In
    //    this case we'll pretend we got the lock on time.
    std::lock_guard<SpinLock> guard{lock_};
    if (waiter.hook.is_linked()) {
      write_waiters_.erase(write_waiters_.iterator_to(waiter));
      return false;
    }
  }
  assert(state_ == State::WRITE_LOCKED);
  return true;
}

template <bool ReaderPriority, typename BatonType>
bool TimedRWMutexImpl<ReaderPriority, BatonType>::try_lock() {
  std::lock_guard<SpinLock> guard{lock_};
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
    std::unique_lock<folly::SpinLock> ulock{lock_};
    assert(state_ == State::WRITE_LOCKED);
  }
  unlock_();
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock_() {
  std::lock_guard<SpinLock> guard{lock_};
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

    while (!read_waiters_.empty()) {
      MutexWaiter& to_wake = read_waiters_.front();
      read_waiters_.pop_front();
      to_wake.baton.post();
    }
  } else if (readers_ == 0) {
    if (!write_waiters_.empty()) {
      assert(read_waiters_.empty() || !ReaderPriority);
      state_ = State::WRITE_LOCKED;

      // Wake a single writer (after releasing the spin lock)
      MutexWaiter& to_wake = write_waiters_.front();
      write_waiters_.pop_front();
      to_wake.baton.post();
    } else {
      verify_unlocked_properties();
      state_ = State::UNLOCKED;
    }
  } else {
    assert(state_ == State::READ_LOCKED);
  }
}

template <bool ReaderPriority, typename BatonType>
void TimedRWMutexImpl<ReaderPriority, BatonType>::unlock_and_lock_shared() {
  std::lock_guard<SpinLock> guard{lock_};
  assert(state_ == State::WRITE_LOCKED && readers_ == 0);
  state_ = State::READ_LOCKED;
  readers_ += 1;

  if (!read_waiters_.empty()) {
    readers_ += read_waiters_.size();

    while (!read_waiters_.empty()) {
      MutexWaiter& to_wake = read_waiters_.front();
      read_waiters_.pop_front();
      to_wake.baton.post();
    }
  }
}
} // namespace fibers
} // namespace folly
