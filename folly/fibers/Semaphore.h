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
#include <folly/Synchronized.h>
#include <folly/experimental/coro/Task.h>
#include <folly/fibers/Baton.h>
#include <folly/futures/Future.h>

#include <deque>

namespace folly {
namespace fibers {

/*
 * Fiber-compatible semaphore. Will safely block fibers that wait when no
 * tokens are available and wake fibers when signalled.
 */
class Semaphore {
 public:
  explicit Semaphore(size_t tokenCount)
      : capacity_(tokenCount), tokens_(int64_t(capacity_)) {}

  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore& operator=(Semaphore&&) = delete;

  /*
   * Release a token in the semaphore. Signal the waiter if necessary.
   */
  void signal();

  /*
   * Wait for capacity in the semaphore.
   */
  void wait();

  struct Waiter {
    Waiter() noexcept {}

    // The baton will be signalled when this waiter acquires the semaphore.
    Baton baton;

   private:
    friend Semaphore;
    folly::SafeIntrusiveListHook hook_;
  };

  /**
   * Try to wait on the semaphore.
   * Return true on success.
   * On failure, the passed waiter is enqueued, its baton will be posted once
   * semaphore has capacity. Caller is responsible to wait then signal.
   */
  bool try_wait(Waiter& waiter);

  /**
   * If the semaphore has capacity, removes a token and returns true. Otherwise
   * returns false and leaves the semaphore unchanged.
   */
  bool try_wait();

#if FOLLY_HAS_COROUTINES

  /*
   * Wait for capacity in the semaphore.
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
  coro::Task<void> co_wait();

#endif

  /*
   * Wait for capacity in the semaphore.
   */
  SemiFuture<Unit> future_wait();

  size_t getCapacity() const;

  size_t getAvailableTokens() const;

 private:
  bool waitSlow(Waiter& waiter);
  bool signalSlow();

  size_t capacity_;
  // Atomic counter
  std::atomic<int64_t> tokens_;

  using WaiterList = folly::SafeIntrusiveList<Waiter, &Waiter::hook_>;

  folly::Synchronized<WaiterList> waitList_;
};

} // namespace fibers
} // namespace folly
