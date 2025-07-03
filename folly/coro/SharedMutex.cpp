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

#include <folly/coro/SharedMutex.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

SharedMutexFair::~SharedMutexFair() {
  assert(state_.lock()->lockedFlagAndReaderCount_ == kUnlocked);
  assert(state_.lock()->waitersHead_ == nullptr);
}

bool SharedMutexFair::try_lock() noexcept {
  auto lock = state_.lock();
  if (lock->lockedFlagAndReaderCount_ == kUnlocked) {
    lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
    return true;
  }
  return false;
}

bool SharedMutexFair::try_lock_shared() noexcept {
  auto lock = state_.lock();
  if (canLockShared(*lock)) {
    lock->lockedFlagAndReaderCount_ += kSharedLockCountIncrement;
    // check for potential overflow
    assert(lock->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement);
    return true;
  }
  return false;
}

bool SharedMutexFair::try_lock_upgrade() noexcept {
  auto lock = state_.lock();
  if (canLockUpgrade(*lock)) {
    lock->lockedFlagAndReaderCount_ |= kUpgradeLockFlag;
    return true;
  }
  return false;
}

bool SharedMutexFair::try_unlock_upgrade_and_lock() noexcept {
  auto lock = state_.lock();
  assert(lock->lockedFlagAndReaderCount_ & kUpgradeLockFlag);
  // skip the line and perform the upgrade as long as there is
  // no outstanding shared locks
  if (lock->lockedFlagAndReaderCount_ == kUpgradeLockFlag) {
    lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
    return true;
  }
  return false;
}

void SharedMutexFair::unlock() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.lock();
    assert(lockedState->lockedFlagAndReaderCount_ == kExclusiveLockFlag);
    lockedState->lockedFlagAndReaderCount_ = kUnlocked;
    awaitersToResume = getWaitersToResume(*lockedState, LockType::EXCLUSIVE);
  }

  resumeWaiters(awaitersToResume);
}

void SharedMutexFair::unlock_shared() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.lock();
    assert(lockedState->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement);
    lockedState->lockedFlagAndReaderCount_ -= kSharedLockCountIncrement;
    awaitersToResume = getWaitersToResume(*lockedState, LockType::SHARED);
  }

  resumeWaiters(awaitersToResume);
}

void SharedMutexFair::unlock_upgrade() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.lock();
    assert(lockedState->lockedFlagAndReaderCount_ & kUpgradeLockFlag);
    lockedState->lockedFlagAndReaderCount_ &= ~kUpgradeLockFlag;
    awaitersToResume = getWaitersToResume(*lockedState, LockType::UPGRADE);
  }

  resumeWaiters(awaitersToResume);
}

// `getWaitersToResume` can sometimes perform long reader scan to
// find readers to resume. But the amortized cost of it is still O(1),
// as we never scan the same waiter more than once (except the head).
SharedMutexFair::LockAwaiterBase* SharedMutexFair::getWaitersToResume(
    SharedMutexFair::State& state, LockType prevLockType) noexcept {
  // to keep the state transition code concise we only modify
  // the mutex's waitersHead_ pointer, and depend on the SCOPE_EXIT
  // to ensure the consistency of the state
  SCOPE_EXIT {
    if (state.waitersHead_ == nullptr) {
      state.waitersTailNext_ = &state.waitersHead_;
    }
  };

  // the state transition is a function of the lock type of the
  // previous lock (what just got unlocked), the waiter(s) and the current mutex
  // state (outstanding locks)
  if (state.upgrader_ != nullptr) {
    // there is an active upgrade lock holder waiting to upgrade to exclusive
    // prioritize the lock transfer over other lock acquisition waiters
    assert(state.lockedFlagAndReaderCount_ & kUpgradeLockFlag);
    if (state.lockedFlagAndReaderCount_ == kUpgradeLockFlag) {
      auto* waiter = std::exchange(state.upgrader_, nullptr);
      // there can only be an active upgrade lock holder waiting to transfer to
      // exclusive
      assert(waiter->nextAwaiter_ == nullptr);
      state.lockedFlagAndReaderCount_ = kExclusiveLockFlag;
      return waiter;
    } else {
      // drain the readers and do not grant any locks to other waiters
      return nullptr;
    }
  }

  // there is no active upgrader; process the pending lock acquisition requests
  // in order
  auto* head = state.waitersHead_;
  if (head == nullptr) {
    // there is no waiters to resume
    return nullptr;
  }

  // There is no pending lock transfers. The mutex can only be unlock_* into one
  // of the following states:
  // - unlocked (from unlock)
  // - shared locked (from unlock_shared or unlock_upgrade)
  // - upgrade and shared locked (from unlock_shared)
  assert(state.lockedFlagAndReaderCount_ != kExclusiveLockFlag);
  if (head->lockType_ == LockType::EXCLUSIVE) {
    if (state.lockedFlagAndReaderCount_ == kUnlocked) {
      // transition to exclusively locked state
      state.waitersHead_ = std::exchange(head->nextAwaiter_, nullptr);
      state.lockedFlagAndReaderCount_ = kExclusiveLockFlag;
      --state.waitingWriterCount_;
      return head;
    }
  } else if (
      head->lockType_ != LockType::UPGRADE ||
      (state.lockedFlagAndReaderCount_ & kUpgradeLockFlag) == 0) {
    // Now the next waiter is either a reader or an upgrader, and the mutex
    // state ensures that the next waiter can always be resumed.
    // The only case that we can't resume the next head (and skip scanning)
    // is when the head is an upgrader and the mutex is in an upgrade locked
    // state. There is nothing to resume in that case (no readers will be queued
    // to begin with).
    state.waitersHead_ = scanReadersAndUpgrader(head, state, prevLockType);
    return head;
  }
  return nullptr;
}

void SharedMutexFair::resumeWaiters(LockAwaiterBase* awaiters) noexcept {
  while (awaiters != nullptr) {
    std::exchange(awaiters, awaiters->nextAwaiter_)->resume();
  }
}

// Scan for a run of SHARED and UPGRADE lock types and return the next
// waiter
SharedMutexFair::LockAwaiterBase* SharedMutexFair::scanReadersAndUpgrader(
    SharedMutexFair::LockAwaiterBase* head,
    SharedMutexFair::State& lockedState,
    LockType prevLockType) noexcept {
  SharedMutexFair::LockAwaiterBase* last =
      nullptr; // tail of the waiters to be resumed
  size_t& state = lockedState.lockedFlagAndReaderCount_;
  // Scan for a continuous run of SHARED and UPGRADE lock types
  while (head != nullptr) {
    if (head->lockType_ == LockType::SHARED) {
      state += kSharedLockCountIncrement;
      // check for potential overflow
      assert(state >= kSharedLockCountIncrement);
    } else if (
        head->lockType_ == LockType::UPGRADE &&
        (state & kUpgradeLockFlag) == 0) {
      state |= kUpgradeLockFlag;
    } else {
      break;
    }
    last = head;
    head = head->nextAwaiter_;
  }
  assert(last != nullptr);

  auto* newWaiterHead = head;
  auto* prev = last;
  if (prevLockType == LockType::EXCLUSIVE) {
    // Do a long reader scan up until the next exclusive lock waiter
    // iff someone just unlocked an exclusive lock.
    // e.g. when the waiter list looks like
    // U1 U2 U3 U4 S1 W S2
    // , and someone just unlocked an exclusive lock
    // we unlock U1 _and_ S1.
    // However, doing long reader scan everytime someone unlocks any lock
    // is wasteful. Readers can only be blocked when the mutex has an active
    // exclusive lock or there are writers waiting ahead of it. In other words,
    // a reader can only be blocked when there is one or more writers ahead of
    // it. The writer can be holding an active lock or simply waiting ahead of
    // the reader.
    //
    // So we only need to do the long reader scan when an exclusive lock is
    // released, and we do a long reader scan up until the next exclusive
    // waiter. If any other lock (shared or upgrade) is released, all the
    // readers are blocked right now should remain blocked (as they are blocked
    // one or more writers ahead of them).
    //
    // We never want to scan past an exclusive waiter, as it would lead to
    // writer starvation and makes this mutex "unfair" or reader-prioritized. In
    // this way, we can keep the amortized cost of `getWaitersToResume` be O(1).
    //
    // Notice that previous lock type being EXCLUSIVE is different from mutex
    // state being UNLOCKED, as the first condition is more restrictive. E.g. if
    // the mutex enters UNLOCKED from unlock_upgrade(), and the waiter list
    // looks like "U U W S W U ... " there is no point to scan the waiter list
    // for readers, as either there is no blocked readers to begin with, or
    // they are blocked by another writer ahead of them, and should remain
    // blocked. The waiter list would never look like "U U S" at the time of
    // unlock_upgrade() because the "S" should never be blocked to begin with.
    //
    // This behavior should not lead to starvation
    // - it does not starve writers because we do not scan pass "W"
    // - it does not starve upgraders because these upgraders are not blocked
    //   by the readers anyway
    // - it does not block lock transition (upgrade -> exclusive) because
    //   lock transition is handled with highest priority, and no readers
    //   are granted when a lock transition is in progress, when it is trying
    //   to drain all the readers
    // E.g. the waiter list might look like U1 U2 S1 U3 S2 W1 S3
    // After calling this function, the `head` should point to U1 S1 S2
    // and the return value should point to U2 U3 W1 S3
    while (head != nullptr) {
      if (head->lockType_ == LockType::SHARED) {
        assert(head != newWaiterHead);
        assert(prev != last);
        state += kSharedLockCountIncrement;
        // check for potential overflow
        assert(state >= kSharedLockCountIncrement);
        prev->nextAwaiter_ = head->nextAwaiter_;
        last->nextAwaiter_ = head;
        last = head;
        head = head->nextAwaiter_;
        if (head == nullptr) {
          // if we skipped ahead and resumed the last waiter
          // we need to update the waiter tail pointer
          lockedState.waitersTailNext_ = &prev->nextAwaiter_;
        }
      } else if (head->lockType_ == LockType::UPGRADE) {
        // skip the upgrade waiter
        prev = head;
        head = head->nextAwaiter_;
      } else {
        break;
      }
    }
  }

  last->nextAwaiter_ = nullptr;
  return newWaiterHead;
}
#endif
