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

#include <folly/fibers/BatchSemaphore.h>

namespace folly {
namespace fibers {

void BatchSemaphore::signal(int64_t tokens) {
  auto oldVal = tokens_.load(std::memory_order_acquire);
  do {
    if (signalSlow(tokens, oldVal)) {
      return;
    }
    oldVal = tokens_.load(std::memory_order_acquire);
  } while (!tokens_.compare_exchange_weak(
      oldVal,
      oldVal + tokens,
      std::memory_order_release,
      std::memory_order_acquire));
}

void BatchSemaphore::wait(int64_t tokens) {
  wait_common(tokens);
}

bool BatchSemaphore::try_wait(Waiter& waiter, int64_t tokens) {
  return try_wait_common(waiter, tokens);
}

#if FOLLY_HAS_COROUTINES

coro::Task<void> BatchSemaphore::co_wait(int64_t tokens) {
  co_await co_wait_common(tokens);
}

#endif

#if FOLLY_FUTURE_USING_FIBER

SemiFuture<Unit> BatchSemaphore::future_wait(int64_t tokens) {
  return future_wait_common(tokens);
}

#endif

} // namespace fibers
} // namespace folly
