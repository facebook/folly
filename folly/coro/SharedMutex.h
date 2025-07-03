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
#include <folly/coro/Coroutine.h>
#include <folly/coro/SharedLock.h>
#include <folly/coro/ViaIfAsync.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

/// The folly::coro::SharedMutexFair class provides a thread synchronisation
/// primitive that allows a coroutine to asynchronously acquire a lock on the
/// mutex.
///
/// The mutex supports three kinds of locks:
/// - exclusive-lock - Also known as a write-lock.
///                    While an exclusive lock is held, no other thread will be
///                    able to acquire either an exclusive lock or a shared
///                    lock until the exclusive lock is released.
/// - shared-lock    - Also known as a read-lock.
///                    The mutex permits multiple shared locks to be held
///                    concurrently but does not permit shared locks to be held
///                    concurrently with exclusive locks.
/// - upgrade-lock   - When an upgrade lock is held, others can still acquire
///                    shared locks but no exclusive lock, or upgrade lock.
///                    An upgrade lock can be later upgraded to an exclusive
///                    lock atomically after all the outstanding shared locks
///                    are released.
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
/// Notably, lock transition (e.g. upgrade an upgrade lock to an exclusive lock)
/// does not respect the FIFO order and is eager. This means a pending lock
/// transition will be processed as soon as possible. This is to avoid deadlock
/// in following scenario
/// 1. coroutine A has the upgrade lock
/// 2. coroutine B is waiting for an exclusive lock
/// 3. coroutine A tries to upgrade the lock to exclusive
/// Coroutine A and B would deadlock if we process the lock transition
/// (operation #3) in FIFO order. The readers will not be starved because they
/// are not blocked by the upgrade state to begin with. The writers/upgraders
/// will not be starved because they cannot acquire the lock anyway.
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
  class LockUpgradeAwaiter;
  class ScopedLockUpgradeAwaiter;
  class UnlockUpgradeAndLockAwaiter;
  class ScopedUnlockUpgradeAndLockAwaiter;

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

  /// Try to acquire an upgrade lock on the mutex synchronously.
  ///
  /// If this returns true then the upgrade lock was acquired synchronously
  /// and the caller is responsible for calling .unlock_upgrade() later to
  /// release the upgrade lock.
  bool try_lock_upgrade() noexcept;

  /// Asynchronously acquire an exclusive lock on the mutex.
  ///
  /// Returns a SemiAwaitable<void> type that requires the caller to inject
  /// an executor by calling .viaIfAsync(executor) and then co_awaiting the
  /// result to wait for the lock to be acquired. Note that if the caller is
  /// awaiting the lock operation within a folly::coro::Task then the current
  /// executor will be injected implicitly without needing to call
  /// .viaIfAsync().
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
  /// result to wait for the lock to be acquired. Note that if the caller is
  /// awaiting the lock operation within a folly::coro::Task then the current
  /// executor will be injected implicitly without needing to call
  /// .viaIfAsync().
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

  /// Asynchronously acquire an upgrade lock on the mutex.
  ///
  /// Returns a SemiAwaitable<void> type that requires the caller to inject
  /// an executor by calling .viaIfAsync(executor) and then co_awaiting the
  /// result to wait for the lock to be acquired. Note that if the caller is
  /// awaiting the lock operation within a folly::coro::Task then the current
  /// executor will be injected implicitly without needing to call
  /// .viaIfAsync().
  ///
  /// If the lock was acquired synchronously then the awaiting coroutine
  /// continues on the current thread without suspending.
  /// If the lock could not be acquired synchronously then the awaiting
  /// coroutine is suspended and later resumed on the specified executor when
  /// the lock becomes available.
  ///
  /// After this operation completes, the caller is responsible for calling
  /// .unlock_upgrade() to release the lock.
  [[nodiscard]] LockOperation<LockUpgradeAwaiter> co_lock_upgrade() noexcept;

  /// Asynchronously acquire an upgrade lock on the mutex and return an object
  /// that will release the lock when it goes out of scope.
  ///
  /// Returns a SemiAwaitable<UpgradeLock<SharedMutexFair>> that, once
  /// associated with an executor using .viaIfAsync(), must be co_awaited to
  /// wait for the lock to be acquired.
  ///
  /// If the lock could be acquired immediately then the coroutine continues
  /// execution without suspending. Otherwise, the coroutine is suspended and
  /// will later be resumed on the specified executor once the lock has been
  /// acquired.
  [[nodiscard]] LockOperation<ScopedLockUpgradeAwaiter>
  co_scoped_lock_upgrade() noexcept;

  /// Asynchronously transition the currently held upgrade lock to exclusive.
  ///
  /// Returns a SemiAwaitable<void> type that requires the caller to inject
  /// an executor by calling .viaIfAsync(executor) and then co_awaiting the
  /// result to wait for the lock to be acquired. Note that if the caller is
  /// awaiting the lock operation within a folly::coro::Task then the current
  /// executor will be injected implicitly without needing to call
  /// .viaIfAsync().
  ///
  /// If the lock was transitioned synchronously then the awaiting coroutine
  /// continues on the current thread without suspending.
  /// If the lock could not be transitioned synchronously then the awaiting
  /// coroutine is suspended and later resumed on the specified executor when
  /// the lock becomes available.
  ///
  /// After this operation completes, the caller is responsible for calling
  /// .unlock() to release the lock.
  [[nodiscard]] LockOperation<UnlockUpgradeAndLockAwaiter>
  co_unlock_upgrade_and_lock() noexcept;

  /// Asynchronously transfer the currently held upgrade lock to exclusive
  /// and return an object that will release the exclusive lock when it
  /// goes out of scope.
  ///
  /// Notice that if the upgrade lock is acquired using
  /// `co_scoped_lock_upgrade()`, one should transfer the lock via
  /// `co_transition_lock(coro::UpgradeLock<coro::SharedMutex>&)` to avoid
  /// double unlock. This method is mostly useful if the original upgrade
  /// lock is acquired manually via `co_await mutex.co_lock_upgrade();`.
  ///
  /// Returns a SemiAwaitable<std::unique_lock<SharedMutexFair>> that, once
  /// associated with an executor using .viaIfAsync(), must be co_awaited to
  /// wait for the lock to be acquired.
  ///
  /// If the lock could be acquired immediately then the coroutine continues
  /// execution without suspending. Otherwise, the coroutine is suspended and
  /// will later be resumed on the specified executor once the lock has been
  /// acquired.
  [[nodiscard]] LockOperation<ScopedUnlockUpgradeAndLockAwaiter>
  co_scoped_unlock_upgrade_and_lock() noexcept;

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

  /// Release an upgrade lock.
  ///
  /// This will resume the next coroutine(s) waiting to acquire an exclusive
  /// lock or an upgrade lock, if any.
  void unlock_upgrade() noexcept;

  /// Try to atomically transition an upgrade lock to an exclusive lock
  /// synchronously.
  ///
  /// If this returns true then the lock was acquired synchronously
  /// and the caller is responsible for calling .unlock() later to
  /// release the lock. Otherwise, the caller remains responsible for calling
  /// .unlock_upgrade() later to release the upgrade lock.
  bool try_unlock_upgrade_and_lock() noexcept;

 private:
  using folly_coro_aware_mutex = std::true_type;

  enum class LockType : std::uint8_t { EXCLUSIVE, UPGRADE, SHARED };

  class LockAwaiterBase {
   protected:
    friend class SharedMutexFair;

    explicit LockAwaiterBase(SharedMutexFair& mutex, LockType lockType) noexcept
        : mutex_(&mutex), nextAwaiter_(nullptr), lockType_(lockType) {}

    void resume() noexcept { continuation_.resume(); }

    SharedMutexFair* mutex_;
    LockAwaiterBase* nextAwaiter_;
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
      auto lock = mutex_->state_.lock();

      // Exclusive lock can only be acquired if it's currently unlocked.
      if (lock->lockedFlagAndReaderCount_ == kUnlocked) {
        lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
        return false;
      }

      // Append to the end of the waiters queue.
      continuation_ = continuation;
      ++lock->waitingWriterCount_;
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
      auto lock = mutex_->state_.lock();

      if (canLockShared(*lock)) {
        lock->lockedFlagAndReaderCount_ += kSharedLockCountIncrement;
        // check for potential overflow
        assert(lock->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement);
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

  class LockUpgradeAwaiter : public LockAwaiterBase {
   public:
    explicit LockUpgradeAwaiter(SharedMutexFair& mutex) noexcept
        : LockAwaiterBase(mutex, LockType::UPGRADE) {}

    bool await_ready() noexcept { return mutex_->try_lock_upgrade(); }

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES bool await_suspend(
        coroutine_handle<> continuation) noexcept {
      auto lock = mutex_->state_.lock();

      if (canLockUpgrade(*lock)) {
        lock->lockedFlagAndReaderCount_ |= kUpgradeLockFlag;
        return false;
      }

      continuation_ = continuation;
      *lock->waitersTailNext_ = this;
      lock->waitersTailNext_ = &nextAwaiter_;
      return true;
    }

    void await_resume() noexcept {}
  };

  class UnlockUpgradeAndLockAwaiter : public LockAwaiterBase {
   public:
    explicit UnlockUpgradeAndLockAwaiter(SharedMutexFair& mutex) noexcept
        : LockAwaiterBase(mutex, LockType::EXCLUSIVE) {}

    bool await_ready() noexcept {
      return mutex_->try_unlock_upgrade_and_lock();
    }

    FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES bool await_suspend(
        coroutine_handle<> continuation) noexcept {
      auto lock = mutex_->state_.lock();

      assert(lock->lockedFlagAndReaderCount_ & kUpgradeLockFlag);
      if (lock->lockedFlagAndReaderCount_ == kUpgradeLockFlag) {
        lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
        return false;
      }

      continuation_ = continuation;
      assert(lock->upgrader_ == nullptr);
      lock->upgrader_ = this;
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

  class ScopedLockUpgradeAwaiter : public LockUpgradeAwaiter {
   public:
    using LockUpgradeAwaiter::LockUpgradeAwaiter;

    [[nodiscard]] UpgradeLock<SharedMutexFair> await_resume() noexcept {
      LockUpgradeAwaiter::await_resume();
      return UpgradeLock<SharedMutexFair>{*mutex_, std::adopt_lock};
    }
  };

  class ScopedUnlockUpgradeAndLockAwaiter : public UnlockUpgradeAndLockAwaiter {
   public:
    using UnlockUpgradeAndLockAwaiter::UnlockUpgradeAndLockAwaiter;

    [[nodiscard]] std::unique_lock<SharedMutexFair> await_resume() noexcept {
      UnlockUpgradeAndLockAwaiter::await_resume();
      return std::unique_lock<SharedMutexFair>{*mutex_, std::adopt_lock};
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

  // There is an invariant that if the mutex state is unlocked, there must be no
  // waiters; the converse is obviously not always true. This is guaranteed by
  // the `getWaitersToResume` function. If there are waiters after an unlock_*
  // operation, the mutex state will transition to a non-unlocked state.
  // This helps avoid a redundant check on the waiters list when the mutex is
  // unlocked.
  struct State {
    State() noexcept
        : lockedFlagAndReaderCount_(kUnlocked),
          waitingWriterCount_(0),
          waitersHead_(nullptr),
          upgrader_(nullptr),
          waitersTailNext_(&waitersHead_) {}

    // bit 0 - exclusive lock is held
    // bit 1 - upgrade lock is held
    // bits 2-[31/63] - count of held shared locks
    std::size_t lockedFlagAndReaderCount_;
    std::size_t waitingWriterCount_;
    LockAwaiterBase* waitersHead_;
    // active upgrade lock holder who's waiting to upgrade to exclusive
    // at most one waiter can be in such state
    LockAwaiterBase* upgrader_;
    LockAwaiterBase** waitersTailNext_;
  };

  static LockAwaiterBase* getWaitersToResume(
      State& state, LockType prevLockType) noexcept;
  static LockAwaiterBase* scanReadersAndUpgrader(
      LockAwaiterBase* head,
      State& lockedState,
      LockType prevLockType) noexcept;

  static void resumeWaiters(LockAwaiterBase* awaiters) noexcept;
  static bool canLockShared(const State& state) noexcept {
    // a shared lock can be acquired if there are no exclusive locks held,
    // exclusive lock pending or lock transition pending
    // an exclusive lock is pending if there are queued waiters for
    // it; there is a pending lock transition if there is active upgrade lock
    // waiting to be upgraded to exclusive
    return state.lockedFlagAndReaderCount_ == kUnlocked ||
        (state.lockedFlagAndReaderCount_ != kExclusiveLockFlag &&
         state.waitingWriterCount_ == 0 && state.upgrader_ == nullptr);
  }
  static bool canLockUpgrade(const State& state) noexcept {
    return state.lockedFlagAndReaderCount_ == kUnlocked ||
        ((state.lockedFlagAndReaderCount_ &
          (kExclusiveLockFlag | kUpgradeLockFlag)) == 0 &&
         state.waitingWriterCount_ == 0);
  }

  static constexpr std::size_t kUnlocked = 0;
  static constexpr std::size_t kExclusiveLockFlag = 1;
  static constexpr std::size_t kUpgradeLockFlag = 2;
  static constexpr std::size_t kSharedLockCountIncrement = 4;

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

inline SharedMutexFair::LockOperation<SharedMutexFair::LockUpgradeAwaiter>
SharedMutexFair::co_lock_upgrade() noexcept {
  return LockOperation<LockUpgradeAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<SharedMutexFair::ScopedLockUpgradeAwaiter>
SharedMutexFair::co_scoped_lock_upgrade() noexcept {
  return LockOperation<ScopedLockUpgradeAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<
    SharedMutexFair::UnlockUpgradeAndLockAwaiter>
SharedMutexFair::co_unlock_upgrade_and_lock() noexcept {
  return LockOperation<UnlockUpgradeAndLockAwaiter>{*this};
}

inline SharedMutexFair::LockOperation<
    SharedMutexFair::ScopedUnlockUpgradeAndLockAwaiter>
SharedMutexFair::co_scoped_unlock_upgrade_and_lock() noexcept {
  return LockOperation<ScopedUnlockUpgradeAndLockAwaiter>{*this};
}

// The default SharedMutex is SharedMutexFair.
using SharedMutex = SharedMutexFair;

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
