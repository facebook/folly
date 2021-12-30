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

#include <folly/fibers/SemaphoreBase.h>

namespace folly {
namespace fibers {

/*
 * Fiber-compatible batch semaphore with ability to perform batch token
 * increment/decrement. Will safely block fibers that wait when no tokens are
 * available and wake fibers when signalled.
 */
class BatchSemaphore : public SemaphoreBase {
 public:
  explicit BatchSemaphore(size_t tokenCount) : SemaphoreBase{tokenCount} {}

  BatchSemaphore(const BatchSemaphore&) = delete;
  BatchSemaphore(BatchSemaphore&&) = delete;
  BatchSemaphore& operator=(const BatchSemaphore&) = delete;
  BatchSemaphore& operator=(BatchSemaphore&&) = delete;

  /*
   * Release requested tokens in the semaphore. Signal the waiter if necessary.
   */
  void signal(int64_t tokens);

  /*
   * Wait for requested tokens in the semaphore.
   */
  void wait(int64_t tokens);

  /**
   * Try to wait on the semaphore.
   * Return true on success.
   * On failure, the passed waiter is enqueued, its baton will be posted once
   * semaphore has requested tokens. Caller is responsible to wait then signal.
   */
  bool try_wait(Waiter& waiter, int64_t tokens);

#if FOLLY_HAS_COROUTINES

  /*
   * Wait for requested tokens in the semaphore.
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
  coro::Task<void> co_wait(int64_t tokens);

#endif

  /*
   * Wait for requested tokens in the semaphore.
   */
  SemiFuture<Unit> future_wait(int64_t tokens);
};

} // namespace fibers
} // namespace folly
