/*
 * Copyright 2016-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/Synchronized.h>
#include <folly/fibers/Baton.h>
#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Task.h>
#endif

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

#if FOLLY_HAS_COROUTINES

  /*
   * Wait for capacity in the semaphore.
   */
  coro::Task<void> co_wait();

#endif

  size_t getCapacity() const;

 private:
  bool waitSlow(folly::fibers::Baton& waitBaton);
  bool signalSlow();

  size_t capacity_;
  // Atomic counter
  std::atomic<int64_t> tokens_;
  folly::Synchronized<std::queue<folly::fibers::Baton*>> waitList_;
};

} // namespace fibers
} // namespace folly
