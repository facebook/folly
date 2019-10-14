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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/SharedMutex.h>

using namespace folly::coro;

SharedMutexFair::~SharedMutexFair() {
  assert(state_.lock()->lockedFlagAndReaderCount_ == kUnlocked);
  assert(state_.lock()->waitersHead_ == nullptr);
}

bool SharedMutexFair::try_lock() noexcept {
  auto lock = state_.contextualLock();
  if (lock->lockedFlagAndReaderCount_ == kUnlocked) {
    lock->lockedFlagAndReaderCount_ = kExclusiveLockFlag;
    return true;
  }
  return false;
}

bool SharedMutexFair::try_lock_shared() noexcept {
  auto lock = state_.contextualLock();
  if (lock->lockedFlagAndReaderCount_ == kUnlocked ||
      (lock->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement &&
       lock->waitersHead_ == nullptr)) {
    lock->lockedFlagAndReaderCount_ += kSharedLockCountIncrement;
    return true;
  }
  return false;
}

void SharedMutexFair::unlock() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.contextualLock();
    assert(lockedState->lockedFlagAndReaderCount_ == kExclusiveLockFlag);
    awaitersToResume = unlockOrGetNextWaitersToResume(*lockedState);
  }

  resumeWaiters(awaitersToResume);
}

void SharedMutexFair::unlock_shared() noexcept {
  LockAwaiterBase* awaitersToResume = nullptr;
  {
    auto lockedState = state_.contextualLock();
    assert(lockedState->lockedFlagAndReaderCount_ >= kSharedLockCountIncrement);
    lockedState->lockedFlagAndReaderCount_ -= kSharedLockCountIncrement;
    if (lockedState->lockedFlagAndReaderCount_ != kUnlocked) {
      return;
    }

    awaitersToResume = unlockOrGetNextWaitersToResume(*lockedState);
  }

  resumeWaiters(awaitersToResume);
}

SharedMutexFair::LockAwaiterBase*
SharedMutexFair::unlockOrGetNextWaitersToResume(
    SharedMutexFair::State& state) noexcept {
  auto* head = state.waitersHead_;
  if (head != nullptr) {
    if (head->lockType_ == LockType::EXCLUSIVE) {
      state.waitersHead_ = std::exchange(head->nextAwaiter_, nullptr);
      state.lockedFlagAndReaderCount_ = kExclusiveLockFlag;
    } else {
      std::size_t newState = kSharedLockCountIncrement;

      // Scan for a run of SHARED lock types.
      auto* last = head;
      auto* next = last->nextAwaiter_;
      while (next != nullptr && next->lockType_ == LockType::SHARED) {
        last = next;
        next = next->nextAwaiter_;
        newState += kSharedLockCountIncrement;
      }

      last->nextAwaiter_ = nullptr;
      state.lockedFlagAndReaderCount_ = newState;
      state.waitersHead_ = next;
    }

    if (state.waitersHead_ == nullptr) {
      state.waitersTailNext_ = &state.waitersHead_;
    }
  } else {
    state.lockedFlagAndReaderCount_ = kUnlocked;
  }

  return head;
}

void SharedMutexFair::resumeWaiters(LockAwaiterBase* awaiters) noexcept {
  while (awaiters != nullptr) {
    std::exchange(awaiters, awaiters->nextAwaiter_)->resume();
  }
}

#endif
