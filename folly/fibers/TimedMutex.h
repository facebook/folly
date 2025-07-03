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

#include <folly/IntrusiveList.h>
#include <folly/Portability.h>
#include <folly/SpinLock.h>
#include <folly/fibers/GenericBaton.h>

namespace folly {
namespace fibers {

namespace detail {

// Represents a waiter waiting for the lock. The waiter waits on the baton until
// it is woken up by a post or timeout expires.
//
// The destructor blocks until wake() is called. This is to ensure that if a
// waiter times out, it is not invalidated for a waker that might have already
// have acquired a reference to it. Hence, whoever removes the waiter from a
// list is responsible for waking it.
template <class BatonType>
class MutexWaiter {
 public:
  MutexWaiter() = default;
  ~MutexWaiter();

  void wait();

  template <class Deadline>
  bool try_wait_until(Deadline deadline);

  void wake();

  folly::SafeIntrusiveListHook hook;

 private:
  BatonType baton_;
  // This is silly, but Baton implementations do not allow to check the state
  // after a timed out wait, so we need to duplicate the state.
  std::atomic<bool> posted_{false};
};

} // namespace detail

/**
 * @class TimedMutex
 *
 * Like mutex but allows timed_lock in addition to lock and try_lock.
 **/
class TimedMutex {
 public:
  struct Options {
    constexpr Options(bool stealing = true) : stealing_(stealing) {}
    /**
     * Prefer thread waiter and steal from fiber waiters if possible
     */
    bool stealing_ = true;
  };

  TimedMutex(Options options = Options()) noexcept
      : options_(std::move(options)) {}

  ~TimedMutex() {
    DCHECK(threadWaiters_.empty());
    DCHECK(fiberWaiters_.empty());
    DCHECK(notifiedFiber_ == nullptr);
  }

  TimedMutex(const TimedMutex& rhs) = delete;
  TimedMutex& operator=(const TimedMutex& rhs) = delete;
  TimedMutex(TimedMutex&& rhs) = delete;
  TimedMutex& operator=(TimedMutex&& rhs) = delete;

  // Lock the mutex. The thread / fiber is blocked until the mutex is free
  void lock();

  // Lock the mutex. The thread / fiber will be blocked until a timeout elapses.
  //
  // @return        true if the mutex was locked, false otherwise
  template <typename Rep, typename Period>
  bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout);

  // Lock the mutex. The thread / fiber will be blocked until a deadline
  //
  // @return        true if the mutex was locked, false otherwise
  template <typename Clock, typename Duration>
  bool try_lock_until(const std::chrono::time_point<Clock, Duration>& deadline);

  // Try to obtain lock without blocking the thread or fiber
  bool try_lock();

  // Unlock the mutex and wake up a waiter if there is one
  void unlock();

 private:
  enum class LockResult { SUCCESS, TIMEOUT, STOLEN };

  using MutexWaiter = detail::MutexWaiter<Baton>;
  using MutexWaiterList =
      folly::SafeIntrusiveList<MutexWaiter, &MutexWaiter::hook>;

  template <typename WaitFunc>
  LockResult lockHelper(WaitFunc&& waitFunc);

  const Options options_;
  folly::SpinLock lock_; //< lock to protect waiter list
  bool locked_ = false; //< is this locked by some thread?
  MutexWaiterList threadWaiters_; //< list of waiters
  MutexWaiterList fiberWaiters_; //< list of waiters
  MutexWaiter* notifiedFiber_{nullptr}; //< Fiber waiter which has been notified
};

/**
 * @class TimedRWMutexImpl
 *
 * A readers-writer lock which allows multiple readers to hold the
 * lock simultaneously or only one writer.
 *
 * NOTE: When ReaderPriority is set to true then the lock is a reader-preferred
 * RWLock i.e. readers are given priority when there are both readers and
 * writers waiting to get the lock.
 *
 * When ReaderPriority is set to false then the lock is a writer-preferred
 * RWLock i.e. writers are given priority when there are both readers and
 * writers waiting to get the lock. Note that when the lock is in
 * writer-preferred mode, the readers are not re-entrant (e.g. if a caller owns
 * a read lock, it can't attempt to acquire the read lock again as it can
 * deadlock.)
 **/
template <bool ReaderPriority, typename BatonType>
class TimedRWMutexImpl {
 public:
  TimedRWMutexImpl() = default;
  ~TimedRWMutexImpl() = default;

  TimedRWMutexImpl(const TimedRWMutexImpl& rhs) = delete;
  TimedRWMutexImpl& operator=(const TimedRWMutexImpl& rhs) = delete;
  TimedRWMutexImpl(TimedRWMutexImpl&& rhs) = delete;
  TimedRWMutexImpl& operator=(TimedRWMutexImpl&& rhs) = delete;

  // Lock for shared access. The thread / fiber is blocked until the lock
  // can be acquired.
  void lock_shared();

  // Like lock_shared except the thread / fiber is blocked until a timeout
  // elapses
  // @return        true if locked successfully, false otherwise.
  template <typename Rep, typename Period>
  bool try_lock_shared_for(const std::chrono::duration<Rep, Period>& timeout);

  // Like lock_shared except the thread / fiber is blocked until a deadline
  // @return        true if locked successfully, false otherwise.
  template <typename Clock, typename Duration>
  bool try_lock_shared_until(
      const std::chrono::time_point<Clock, Duration>& deadline);

  // Like lock_shared but doesn't block the thread / fiber if the lock can't
  // be acquired.
  // @return        true if lock was acquired, false otherwise.
  bool try_lock_shared();

  // Release the lock. The thread / fiber will wake up a writer if there is one
  // and if this is the last concurrently-held read lock to be released.
  void unlock_shared();

  // Obtain an exclusive lock. The thread / fiber is blocked until the lock
  // is available.
  void lock();

  // Like lock except the thread / fiber is blocked until a timeout elapses
  // @return        true if locked successfully, false otherwise.
  template <typename Rep, typename Period>
  bool try_lock_for(const std::chrono::duration<Rep, Period>& timeout);

  // Like lock except the thread / fiber is blocked until a deadline
  // @return        true if locked successfully, false otherwise.
  template <typename Clock, typename Duration>
  bool try_lock_until(const std::chrono::time_point<Clock, Duration>& deadline);

  // Like lock but doesn't block the thread / fiber if the lock cant be
  // obtained.
  // @return        true if lock was acquired, false otherwise.
  bool try_lock();

  // Realease the lock. The thread / fiber will wake up all readers if there are
  // any. If there are waiting writers then only one of them will be woken up.
  // NOTE: readers are given priority over writers (see above comment)
  void unlock();

  // Downgrade the lock. The thread / fiber will wake up all readers if there
  // are any.
  void unlock_and_lock_shared();

 private:
  // invariants that must hold when the lock is not held by anyone
  void verify_unlocked_properties() {
    assert(readers_ == 0);
    assert(read_waiters_.empty());
    assert(write_waiters_.empty());
  }

  bool shouldReadersWait() const;

  void unlock_();

  enum class State : uint8_t { UNLOCKED, READ_LOCKED, WRITE_LOCKED };

  using MutexWaiter = detail::MutexWaiter<BatonType>;
  using MutexWaiterList =
      folly::CountedIntrusiveList<MutexWaiter, &MutexWaiter::hook>;

  folly::SpinLock lock_; //< lock protecting the internal state
  // (state_, read_waiters_, etc.)
  State state_ = State::UNLOCKED;

  uint32_t readers_ = 0; //< Number of readers who have the lock

  MutexWaiterList write_waiters_; //< List of thread / fibers waiting for
  //  exclusive access

  MutexWaiterList read_waiters_; //< List of thread / fibers waiting for
  //  shared access
};

template <typename BatonType>
using TimedRWMutexReadPriority = TimedRWMutexImpl<true, BatonType>;

template <typename BatonType>
using TimedRWMutexWritePriority = TimedRWMutexImpl<false, BatonType>;

template <typename BatonType>
using TimedRWMutex = TimedRWMutexReadPriority<BatonType>;

} // namespace fibers
} // namespace folly

#include <folly/fibers/TimedMutex-inl.h>
