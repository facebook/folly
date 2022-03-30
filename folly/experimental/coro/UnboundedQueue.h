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

#include <folly/concurrency/UnboundedQueue.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Task.h>
#include <folly/fibers/Semaphore.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Wrapper around folly::UnboundedQueue with async wait

template <typename T, bool SingleProducer = false, bool SingleConsumer = false>
class UnboundedQueue {
 public:
  template <typename U = T>
  void enqueue(U&& val) {
    queue_.enqueue(std::forward<U>(val));
    sem_.signal();
  }

  folly::coro::Task<T> dequeue() {
    folly::Try<void> result = co_await folly::coro::co_awaitTry(sem_.co_wait());
    if (result.hasException()) {
      co_yield co_error(std::move(result).exception());
    }

    co_return queue_.dequeue();
  }

  folly::coro::Task<void> dequeue(T& out) {
    co_await sem_.co_wait();
    queue_.dequeue(out);
  }

  folly::Optional<T> try_dequeue() {
    return sem_.try_wait() ? queue_.try_dequeue() : folly::none;
  }

  bool try_dequeue(T& out) {
    return sem_.try_wait() ? queue_.try_dequeue(out) : false;
  }

  bool empty() { return queue_.empty(); }

  const T* try_peek() noexcept { return queue_.try_peek(); }

  size_t size() { return queue_.size(); }

 private:
  folly::UnboundedQueue<T, SingleProducer, SingleConsumer, false> queue_;
  folly::fibers::Semaphore sem_{0};
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
