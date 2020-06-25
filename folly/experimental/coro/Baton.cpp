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

#include <folly/experimental/coro/Baton.h>
#include <folly/synchronization/AtomicUtil.h>

#include <cassert>
#include <utility>

using namespace folly::coro;

Baton::~Baton() {
  // Should not be any waiting coroutines when the baton is destructed.
  // Caller should ensure the baton is posted before destructing.
  assert(
      state_.load(std::memory_order_relaxed) == static_cast<void*>(this) ||
      state_.load(std::memory_order_relaxed) == nullptr);
}

void Baton::post() noexcept {
  void* const signalledState = static_cast<void*>(this);
  void* oldValue = state_.exchange(signalledState, std::memory_order_acq_rel);
  if (oldValue != signalledState) {
    // We are the first thread to set the state to signalled and there is
    // a waiting coroutine. We are responsible for resuming it.
    WaitOperation* awaiter = static_cast<WaitOperation*>(oldValue);
    while (awaiter != nullptr) {
      std::exchange(awaiter, awaiter->next_)->awaitingCoroutine_.resume();
    }
  }
}

bool Baton::waitImpl(WaitOperation* awaiter) const noexcept {
  // Try to push the awaiter onto the front of the queue of waiters.
  const auto signalledState = static_cast<const void*>(this);
  void* oldValue = state_.load(std::memory_order_acquire);
  do {
    if (oldValue == signalledState) {
      // Already in the signalled state, don't enqueue it.
      return false;
    }
    awaiter->next_ = static_cast<WaitOperation*>(oldValue);
  } while (!folly::atomic_compare_exchange_weak_explicit(
      &state_,
      &oldValue,
      static_cast<void*>(awaiter),
      std::memory_order_release,
      std::memory_order_acquire));
  return true;
}

#endif // FOLLY_HAS_COROUTINES
