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
  if (lock->lockedFlagAndReaderCount_ == kUnlocked ||
      ((lock->lockedFlagAndReaderCount_ &
        (kExclusiveLockFlag | kUpgradeLockFlag)) == 0 &&
       lock->waitingWriterCount_ == 0)) {
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
    awaitersToResume = getWaitersToResume(*lockedState);
  }

  resumeWaiters(awaitersToResume);
}

void SharedMutexFair::unlock_shared() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.lock();
    assert(lockedState->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement);
    lockedState->lockedFlagAndReaderCount_ -= kSharedLockCountIncrement;
    awaitersToResume = getWaitersToResume(*lockedState);
  }

  resumeWaiters(awaitersToResume);
}

void SharedMutexFair::unlock_upgrade() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.lock();
    assert(lockedState->lockedFlagAndReaderCount_ & kUpgradeLockFlag);
    lockedState->lockedFlagAndReaderCount_ &= ~kUpgradeLockFlag;
    awaitersToResume = getWaitersToResume(*lockedState);
  }

  resumeWaiters(awaitersToResume);
}

SharedMutexFair::LockAwaiterBase* SharedMutexFair::getWaitersToResume(
    SharedMutexFair::State& state) noexcept {
  auto* head = state.waitersHead_;
  if (state.lockedFlagAndReaderCount_ != kUnlocked || head == nullptr) {
    // either the mutex state disallows resumption of any awaiters
    // or there's nothing to resume
    return nullptr;
  }

  // state transition from unlocked to the next state
  if (head->lockType_ == LockType::EXCLUSIVE) {
    state.waitersHead_ = std::exchange(head->nextAwaiter_, nullptr);
    state.lockedFlagAndReaderCount_ = kExclusiveLockFlag;
    --state.waitingWriterCount_;
  } else {
    std::size_t newState = kSharedLockCountIncrement;

    // Scan for a run of SHARED lock types.
    auto* last = head;
    auto* next = last->nextAwaiter_;
    while (next != nullptr && next->lockType_ == LockType::SHARED) {
      last = next;
      next = next->nextAwaiter_;
      newState += kSharedLockCountIncrement;
      // check for potential overflow
      assert(newState >= kSharedLockCountIncrement);
    }

    last->nextAwaiter_ = nullptr;
    state.lockedFlagAndReaderCount_ = newState;
    state.waitersHead_ = next;
  }

  if (state.waitersHead_ == nullptr) {
    state.waitersTailNext_ = &state.waitersHead_;
  }
  return head;
}

void SharedMutexFair::resumeWaiters(LockAwaiterBase* awaiters) noexcept {
  while (awaiters != nullptr) {
    std::exchange(awaiters, awaiters->nextAwaiter_)->resume();
  }
}

#endif
