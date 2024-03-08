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
#include <cassert>
#include <limits>
#include <mutex>
#include <utility>

#include <folly/Executor.h>
#include <folly/SpinLock.h>
#include <folly/Synchronized.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/SharedLock.h>
#include <folly/experimental/coro/ViaIfAsync.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// The folly::coro::SharedMutexFair class provides a thread synchronisation
/// primitive that allows a coroutine to asynchronously acquire a lock on the
/// mutex.
///
/// The mutex supports two kinds of locks:
/// - exclusive-lock - Also known as a write-lock.
///                    While an exclusive lock is held, no other thread will be
///                    able to acquire either an exclusive lock or a shared
///                    lock until the exclusive lock is released.
/// - shared-lock    - Also known as a read-lock.
///                    The mutex permits multiple shared locks to be held
///                    concurrently but does not permit shared locks to be held
///                    concurrently with exclusive locks.
///
/// This mutex employs a fair lock acquisition strategy that attempts to process
/// locks in a mostly FIFO order in which they arrive at the mutex.
/// This means that if the mutex is currently read-locked and some coroutine
/// tries to acquire a write-lock, that subsequent read-lock attempts will
/// be queued up behind the write-lock, allowing the write-lock to be acquired
/// in a bounded amount of time.
///
/// One implication of this strategy is that it is not safe to unconditionally
/// acquire a new read-lock while already holding a read-lock, since it's
/// possible that this could lead to deadlock if there was another coroutine
/// that was currently waiting on a write-lock.
///
/// The locks acquired by this mutex do not have thread affinity. A coroutine
/// can acquire the lock on one thread and release the lock on another thread.
///
/// Example usage:
///
///  class AsyncStringSet {
///    mutable folly::coro::SharedMutexFair mutex_;
///    std::unordered_set<std::string> values_;
///
///    AsyncStringSet() = default;
///
///    folly::coro::Task<bool> insert(std::string value) {
///      auto lock = co_await mutex_.co_scoped_lock();
///      co_return values_.insert(value).second;
///    }
///
///    folly::coro::Task<bool> remove(std::string value) {
///      auto lock = co_await mutex_.co_scoped_lock();
///      co_return values_.erase(value) > 0;
///    }
///
///    folly::coro::Task<bool> contains(std::string value) const {
///      auto lock = co_await mutex_.co_scoped_lock_shared();
///      co_return values_.count(value) > 0;
///    }
///  };
class SharedMutexFair : private folly::NonCopyableNonMovable {
  template <typename Awaiter>
  class LockOperation;
  class LockAwaiter;
  class ScopedLockAwaiter;
  class LockSharedAwaiter;
  class ScopedLockSharedAwaiter;

 public:
  SharedMutexFair() noexcept = default;

  ~SharedMutexFair();

  /// Try to acquire an exclusive lock on the mutex synchronously.
  ///
  /// If this returns true then the exclusive lock was acquired synchronously
  /// and the caller is responsible for calling .unlock() later to release
  /// the exclusive lock. If this returns false then the lock was not acquired.
  ///
  /// Consider using a std::unique_lock to ensure the lock is released at the
  /// end of a scope.
  bool try_lock() noexcept;

  /// Try to acquire a shared lock on the mutex synchronously.
  ///
  /// If this returns true then the shared lock was acquired synchronously
  /// and the caller is responsible for calling .unlock_shared() later to
  /// release the shared lock.
  bool try_lock_shared() noexcept;

  /// Asynchronously acquire an exclusive lock on the mutex.
  ///
  /// Returns a SemiAwaitable<void> type that requires the caller to inject
  /// an executor by calling .viaIfAsync(executor) and then co_awaiting the
  /// result to wait for the lock to be acquired. Note that if you are awaiting
  /// the lock operation within a folly::coro::Task then the current executor
  /// will be injected implicitly without needing to call .viaIfAsync().
  ///
  /// If the lock was acquired synchronously then the awaiting coroutine
  /// continues on the current thread without suspending.
  /// If the lock could not be acquired synchronously then the awaiting
  /// coroutine is suspended and later resumed on the specified executor when
  /// the lock becomes available.
  ///
  /// After this operation completes, the caller is responsible for calling
  /// .unlock() to release the lock.
  [[nodiscard]] LockOperation<LockAwaiter> co_lock() noexcept;

  /// Asynchronously acquire an exclusive lock on the mutex and return an object
  /// that will release the lock when it goes out of scope.
  ///
  /// Returns a SemiAwaitable<std::unique_lock<SharedMutexFair>> that, once
  /// associated with an executor using .viaIfAsync(), must be co_awaited to
  /// wait for the lock to be acquired.
  ///
  /// If the lock could be acquired immediately then the coroutine continues
  /// execution without suspending. Otherwise, the coroutine is suspended and
  /// will later be resumed on the specified executor once the lock has been
  /// acquired.
  [[nodiscard]] LockOperation<ScopedLockAwaiter> co_scoped_lock() noexcept;

  /// Asynchronously acquire a shared lock on the mutex.
  ///
  /// Returns a SemiAwaitable<void> type that requires the caller to inject
  /// an executor by calling .viaIfAsync(executor) and then co_awaiting the
  /// result to wait for the lock to be acquired. Note that if you are awaiting
  /// the lock operation within a folly::coro::Task then the current executor
  /// will be injected implicitly without needing to call .viaIfAsync().
  ///
  /// If the lock was acquired synchronously then the awaiting coroutine
  /// continues on the current thread without suspending.
  /// If the lock could not be acquired synchronously then the awaiting
  /// coroutine is suspended and later resumed on the specified executor when
  /// the lock becomes available.
  ///
  /// After this operation completes, the caller is responsible for calling
  /// .unlock_shared() to release the lock.
  [[nodiscard]] LockOperation<LockSharedAwaiter> co_lock_shared() noexcept;

  /// Asynchronously acquire a shared lock on the mutex and return an object
  /// that will release the lock when it goes out of scope.
  ///
  /// Returns a SemiAwaitable<std::shared_lock<SharedMutexFair>> that, once
  /// associated with an executor using .viaIfAsync(), must be co_awaited to
  /// wait for the lock to be acquired.
  ///
  /// If the lock could be acquired immediately then the coroutine continues
  /// execution without suspending. Otherwise, the coroutine is suspended and
  /// will later be resumed on the specified executor once the lock has been
  /// acquired.
  [[nodiscard]] LockOperation<ScopedLockSharedAwaiter>
  co_scoped_lock_shared() noexcept;

  /// Release the exclusive lock.
  ///
  /// This will resume the next coroutine(s) waiting to acquire the lock, if
  /// any.
  void unlock() noexcept;

  /// Release a shared lock.
  ///
  /// If this is the last shared lock then this will resume the next
  /// coroutine(s) waiting to acquire the lock, if any.
  void unlock_shared() noexcept;

 private:
  using folly_coro_aware_mutex = std::true_type;

  enum class LockType : std::uint8_t { EXCLUSIVE, SHARED };

  class LockAwaiterBase {
   protected:
    friend class SharedMutexFair;

    explicit LockAwaiterBase(SharedMutexFair& mutex, LockType lockType) noexcept
        : mutex_(&mutex), nextAwaiter_(nullptr), lockType_(lockType) {}

    void resume() noexcept { continuation_.resume(); }

    SharedMutexFair* mutex_;
    LockAwaiterBase* nextAwaiter_;
    LockAwaiterBase* nextReader_;
    coroutine_handle<> continuation_;
    LockType lockType_;
  };

  class LockAwaiter : public LockAwaiterBase {
   public:
    explicit LockAwaiter(SharedMutexFair& mutex) noexcept
        : LockAwaiterBase(mutex, LockType::EXCLUSIVE) {}

    bool await_ready() noexcept { return mutex_->try_lock(); }

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES bool await_suspend(
        coroutine_handle<> continuation) noexcept {
      auto lock = mutex_->state_.contextualLock();

      // Exclusive lock can only be acquired if it's currently unlocked.
      if (lock->lockedFlagAndReaderCount_ == kUnlocked) {
        lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
        return false;
      }

      // Append to the end of the waiters queue.
      continuation_ = continuation;
      *lock->waitersTailNext_ = this;
      lock->waitersTailNext_ = &nextAwaiter_;
      return true;
    }

    void await_resume() noexcept {}
  };

  class LockSharedAwaiter : public LockAwaiterBase {
   public:
    explicit LockSharedAwaiter(SharedMutexFair& mutex) noexcept
        : LockAwaiterBase(mutex, LockType::SHARED) {}

    bool await_ready() noexcept { return mutex_->try_lock_shared(); }

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES bool await_suspend(
        coroutine_handle<> continuation) noexcept {
      auto lock = mutex_->state_.contextualLock();

      // shared-lock can be acquired if it's either unlocked or it is
      // currently locked shared and there is no queued waiters.
      if (lock->lockedFlagAndReaderCount_ == kUnlocked ||
          (lock->lockedFlagAndReaderCount_ != kExclusiveLockFlag &&
           lock->waitersHead_ == nullptr)) {
        lock->lockedFlagAndReaderCount_ += kSharedLockCountIncrement;
        return false;
      }

      // Lock not available immediately.
      // Queue up for later resumption.
      continuation_ = continuation;
      *lock->waitersTailNext_ = this;
      lock->waitersTailNext_ = &nextAwaiter_;
      return true;
    }

    void await_resume() noexcept {}
  };

  class ScopedLockAwaiter : public LockAwaiter {
   public:
    using LockAwaiter::LockAwaiter;

    [[nodiscard]] std::unique_lock<SharedMutexFair> await_resume() noexcept {
      LockAwaiter::await_resume();
      return std::unique_lock<SharedMutexFair>{*mutex_, std::adopt_lock};
    }
  };

  class ScopedLockSharedAwaiter : public LockSharedAwaiter {
   public:
    using LockSharedAwaiter::LockSharedAwaiter;

    [[nodiscard]] SharedLock<SharedMutexFair> await_resume() noexcept {
      LockSharedAwaiter::await_resume();
      return SharedLock<SharedMutexFair>{*mutex_, std::adopt_lock};
    }
  };

  friend class LockAwaiter;

  template <typename Awaiter>
  class LockOperation {
   public:
    explicit LockOperation(SharedMutexFair& mutex) noexcept : mutex_(mutex) {}

    auto viaIfAsync(folly::Executor::KeepAlive<> executor) const {
      return folly::coro::co_viaIfAsync(std::move(executor), Awaiter{mutex_});
    }

   private:
    SharedMutexFair& mutex_;
  };

  struct State {
    State() noexcept
        : lockedFlagAndReaderCount_(kUnlocked),
          waitersHead_(nullptr),
          waitersTailNext_(&waitersHead_) {}

    // bit 0          - locked by writer
    // bits 1-[31/63] - reader lock count
    std::size_t lockedFlagAndReaderCount_;

    LockAwaiterBase* waitersHead_;
    LockAwaiterBase** waitersTailNext_;
  };

  static LockAwaiterBase* unlockOrGetNextWaitersToResume(State& state) noexcept;

  static void resumeWaiters(LockAwaiterBase* awaiters) noexcept;

  static constexpr std::size_t kUnlocked = 0;
  static constexpr std::size_t kExclusiveLockFlag = 1;
  static constexpr std::size_t kSharedLockCountIncrement = 2;

  folly::Synchronized<State, folly::SpinLock> state_;
};

inline SharedMutexFair::LockOperation<SharedMutexFair::LockAwaiter>
SharedMutexFair::co_lock() noexcept {
  return LockOperation<LockAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<SharedMutexFair::LockSharedAwaiter>
SharedMutexFair::co_lock_shared() noexcept {
  return LockOperation<LockSharedAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<SharedMutexFair::ScopedLockAwaiter>
SharedMutexFair::co_scoped_lock() noexcept {
  return LockOperation<ScopedLockAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<SharedMutexFair::ScopedLockSharedAwaiter>
SharedMutexFair::co_scoped_lock_shared() noexcept {
  return LockOperation<ScopedLockSharedAwaiter>{*this};
}

// The default SharedMutex is SharedMutexFair.
using SharedMutex = SharedMutexFair;

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
