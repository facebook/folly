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
#include <folly/coro/Coroutine.h>
#include <folly/coro/Task.h>
#include <folly/fibers/Semaphore.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Wrapper around folly::UnboundedQueue with async wait

template <
    typename T,
    bool SingleProducer = false,
    bool SingleConsumer = false,
    bool MayBlock = false,
    size_t LgSegmentSize = 8>
class UnboundedQueue {
 public:
  template <typename U = T>
  void enqueue(U&& val) {
    queue_.enqueue(std::forward<U>(val));
    sem_.signal();
  }

  // Dequeue a value from the queue.
  // Note that this operation can be safely cancelled by requesting cancellation
  // on the awaiting coroutine's associated CancellationToken.
  // If the operation is successfully cancelled then it will complete with
  // an error of type folly::OperationCancelled.
  // WARNING: It is not safe to wrap this with folly::coro::timeout(). Wrap with
  // folly::coro::timeoutNoDiscard(), or use co_try_dequeue_for() instead.
  folly::coro::Task<T> dequeue() {
    folly::Try<void> result = co_await folly::coro::co_awaitTry(sem_.co_wait());
    if (result.hasException()) {
      co_yield co_error(std::move(result).exception());
    }

    co_return queue_.dequeue();
  }

  // Try to dequeue a value from the queue with a timeout. The operation will
  // either successfully dequeue an item from the queue, or else be cancelled
  // and complete with an error of type folly::OperationCancelled.
  template <typename Duration>
  folly::coro::Task<T> co_try_dequeue_for(Duration timeout) {
    folly::Try<void> result =
        co_await folly::coro::co_awaitTry(sem_.co_try_wait_for(timeout));
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

  bool empty() const { return queue_.empty(); }

  const T* try_peek() noexcept { return queue_.try_peek(); }

  size_t size() const { return queue_.size(); }

 private:
  folly::UnboundedQueue< //
      T,
      SingleProducer,
      SingleConsumer,
      MayBlock,
      LgSegmentSize>
      queue_;
  folly::fibers::Semaphore sem_{0};
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
