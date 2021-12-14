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

#include <folly/IntrusiveList.h>
#include <folly/Synchronized.h>
#include <folly/fibers/Baton.h>
#include <folly/futures/Future.h>
#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Task.h>
#endif

#include <deque>

namespace folly {
namespace fibers {

/*
 * Fiber-compatible semaphore base. Will safely block fibers that wait when no
 * tokens are available and wake fibers when signalled.
 */
class SemaphoreBase {
 public:
  explicit SemaphoreBase(size_t tokenCount)
      : capacity_(tokenCount), tokens_(int64_t(capacity_)) {}

  SemaphoreBase(const SemaphoreBase&) = delete;
  SemaphoreBase(SemaphoreBase&&) = delete;
  SemaphoreBase& operator=(const SemaphoreBase&) = delete;
  SemaphoreBase& operator=(SemaphoreBase&&) = delete;

  struct Waiter {
    explicit Waiter(int64_t tokens = 1) noexcept : tokens_{tokens} {}

    // The baton will be signalled when this waiter acquires the semaphore.
    Baton baton;

   private:
    friend SemaphoreBase;
    folly::SafeIntrusiveListHook hook_;
    int64_t tokens_;
  };

  size_t getCapacity() const;

 protected:
  /*
   * Wait for request capacity in the semaphore.
   */
  void wait_common(int64_t tokens);

  /**
   * Try to wait on the semaphore.
   * Return true on success.
   * On failure, the passed waiter is enqueued, its baton will be posted once
   * semaphore has capacity. Caller is responsible to wait then signal.
   */
  bool try_wait_common(Waiter& waiter, int64_t tokens);

#if FOLLY_HAS_COROUTINES

  /*
   * Wait for request capacity in the semaphore.
   *
   * Note that this wait-operation can be cancelled by requesting cancellation
   * on the awaiting coroutine's associated CancellationToken.
   * If the operation is successfully cancelled then it will complete with
   * an error of type folly::OperationCancelled.
   *
   * Note that requesting cancellation of the operation will only have an
   * effect if the operation does not complete synchronously (ie. was not
   * already in a signalled state).
   *
   * If the semaphore was already in a signalled state prior to awaiting the
   * returned Task then the operation will complete successfully regardless
   * of whether cancellation was requested.
   */
  coro::Task<void> co_wait_common(int64_t tokens);

#endif

  /*
   * Wait for request capacity in the semaphore.
   */
  SemiFuture<Unit> future_wait_common(int64_t tokens);

  bool waitSlow(Waiter& waiter, int64_t tokens);
  bool signalSlow(int64_t tokens);

  size_t capacity_;
  // Atomic counter
  std::atomic<int64_t> tokens_;

  using WaiterList = folly::SafeIntrusiveList<Waiter, &Waiter::hook_>;

  folly::Synchronized<WaiterList> waitList_;
};

} // namespace fibers
} // namespace folly
