/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

namespace folly {
namespace fibers {

//
// TimedMutex implementation
//

template <typename BatonType>
void TimedMutex<BatonType>::lock() {
  pthread_spin_lock(&lock_);
  if (!locked_) {
    locked_ = true;
    pthread_spin_unlock(&lock_);
    return;
  }

  // Delay constructing the waiter until it is actually required.
  // This makes a huge difference, at least in the benchmarks,
  // when the mutex isn't locked.
  MutexWaiter waiter;
  waiters_.push_back(waiter);
  pthread_spin_unlock(&lock_);
  waiter.baton.wait();
}

template <typename BatonType>
template <typename Rep, typename Period>
bool TimedMutex<BatonType>::timed_lock(
    const std::chrono::duration<Rep, Period>& duration) {
  pthread_spin_lock(&lock_);
  if (!locked_) {
    locked_ = true;
    pthread_spin_unlock(&lock_);
    return true;
  }

  MutexWaiter waiter;
  waiters_.push_back(waiter);
  pthread_spin_unlock(&lock_);

  if (!waiter.baton.timed_wait(duration)) {
    // We timed out. Two cases:
    // 1. We're still in the waiter list and we truly timed out
    // 2. We're not in the waiter list anymore. This could happen if the baton
    //    times out but the mutex is unlocked before we reach this code. In this
    //    case we'll pretend we got the lock on time.
    pthread_spin_lock(&lock_);
    if (waiter.hook.is_linked()) {
      waiters_.erase(waiters_.iterator_to(waiter));
      pthread_spin_unlock(&lock_);
      return false;
    }
    pthread_spin_unlock(&lock_);
  }
  return true;
}

template <typename BatonType>
bool TimedMutex<BatonType>::try_lock() {
  pthread_spin_lock(&lock_);
  if (locked_) {
    pthread_spin_unlock(&lock_);
    return false;
  }
  locked_ = true;
  pthread_spin_unlock(&lock_);
  return true;
}

template <typename BatonType>
void TimedMutex<BatonType>::unlock() {
  pthread_spin_lock(&lock_);
  if (waiters_.empty()) {
    locked_ = false;
    pthread_spin_unlock(&lock_);
    return;
  }
  MutexWaiter& to_wake = waiters_.front();
  waiters_.pop_front();
  to_wake.baton.post();
  pthread_spin_unlock(&lock_);
}

//
// TimedRWMutex implementation
//

template <typename BatonType>
void TimedRWMutex<BatonType>::read_lock() {
  pthread_spin_lock(&lock_);
  if (state_ == State::WRITE_LOCKED) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    pthread_spin_unlock(&lock_);
    waiter.baton.wait();
    assert(state_ == State::READ_LOCKED);
    return;
  }
  assert(
      (state_ == State::UNLOCKED && readers_ == 0) ||
      (state_ == State::READ_LOCKED && readers_ > 0));
  assert(read_waiters_.empty());
  state_ = State::READ_LOCKED;
  readers_ += 1;
  pthread_spin_unlock(&lock_);
}

template <typename BatonType>
template <typename Rep, typename Period>
bool TimedRWMutex<BatonType>::timed_read_lock(
    const std::chrono::duration<Rep, Period>& duration) {
  pthread_spin_lock(&lock_);
  if (state_ == State::WRITE_LOCKED) {
    MutexWaiter waiter;
    read_waiters_.push_back(waiter);
    pthread_spin_unlock(&lock_);

    if (!waiter.baton.timed_wait(duration)) {
      // We timed out. Two cases:
      // 1. We're still in the waiter list and we truly timed out
      // 2. We're not in the waiter list anymore. This could happen if the baton
      //    times out but the mutex is unlocked before we reach this code. In
      //    this case we'll pretend we got the lock on time.
      pthread_spin_lock(&lock_);
      if (waiter.hook.is_linked()) {
        read_waiters_.erase(read_waiters_.iterator_to(waiter));
        pthread_spin_unlock(&lock_);
        return false;
      }
      pthread_spin_unlock(&lock_);
    }
    return true;
  }
  assert(
      (state_ == State::UNLOCKED && readers_ == 0) ||
      (state_ == State::READ_LOCKED && readers_ > 0));
  assert(read_waiters_.empty());
  state_ = State::READ_LOCKED;
  readers_ += 1;
  pthread_spin_unlock(&lock_);
  return true;
}

template <typename BatonType>
bool TimedRWMutex<BatonType>::try_read_lock() {
  pthread_spin_lock(&lock_);
  if (state_ != State::WRITE_LOCKED) {
    assert(
        (state_ == State::UNLOCKED && readers_ == 0) ||
        (state_ == State::READ_LOCKED && readers_ > 0));
    assert(read_waiters_.empty());
    state_ = State::READ_LOCKED;
    readers_ += 1;
    pthread_spin_unlock(&lock_);
    return true;
  }
  pthread_spin_unlock(&lock_);
  return false;
}

template <typename BatonType>
void TimedRWMutex<BatonType>::write_lock() {
  pthread_spin_lock(&lock_);
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    pthread_spin_unlock(&lock_);
    return;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  pthread_spin_unlock(&lock_);
  waiter.baton.wait();
}

template <typename BatonType>
template <typename Rep, typename Period>
bool TimedRWMutex<BatonType>::timed_write_lock(
    const std::chrono::duration<Rep, Period>& duration) {
  pthread_spin_lock(&lock_);
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    pthread_spin_unlock(&lock_);
    return true;
  }
  MutexWaiter waiter;
  write_waiters_.push_back(waiter);
  pthread_spin_unlock(&lock_);

  if (!waiter.baton.timed_wait(duration)) {
    // We timed out. Two cases:
    // 1. We're still in the waiter list and we truly timed out
    // 2. We're not in the waiter list anymore. This could happen if the baton
    //    times out but the mutex is unlocked before we reach this code. In
    //    this case we'll pretend we got the lock on time.
    pthread_spin_lock(&lock_);
    if (waiter.hook.is_linked()) {
      write_waiters_.erase(write_waiters_.iterator_to(waiter));
      pthread_spin_unlock(&lock_);
      return false;
    }
    pthread_spin_unlock(&lock_);
  }
  assert(state_ == State::WRITE_LOCKED);
  return true;
}

template <typename BatonType>
bool TimedRWMutex<BatonType>::try_write_lock() {
  pthread_spin_lock(&lock_);
  if (state_ == State::UNLOCKED) {
    verify_unlocked_properties();
    state_ = State::WRITE_LOCKED;
    pthread_spin_unlock(&lock_);
    return true;
  }
  pthread_spin_unlock(&lock_);
  return false;
}

template <typename BatonType>
void TimedRWMutex<BatonType>::unlock() {
  pthread_spin_lock(&lock_);
  assert(state_ != State::UNLOCKED);
  assert(
      (state_ == State::READ_LOCKED && readers_ > 0) ||
      (state_ == State::WRITE_LOCKED && readers_ == 0));
  if (state_ == State::READ_LOCKED) {
    readers_ -= 1;
  }

  if (!read_waiters_.empty()) {
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
      assert(read_waiters_.empty());
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
  pthread_spin_unlock(&lock_);
}

template <typename BatonType>
void TimedRWMutex<BatonType>::downgrade() {
  pthread_spin_lock(&lock_);
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
  pthread_spin_unlock(&lock_);
}
}
}
