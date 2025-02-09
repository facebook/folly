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

#include <folly/MPMCQueue.h>
#include <folly/ProducerConsumerQueue.h>
#include <folly/coro/Task.h>
#include <folly/fibers/Semaphore.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// A coroutine version of bounded queue with given capacity. Both enqueue and
// dequeue are async awaitable.
template <typename T, bool SingleProducer = false, bool SingleConsumer = false>
class BoundedQueue {
  static constexpr bool kSPSC = SingleProducer && SingleConsumer;

 public:
  explicit BoundedQueue(uint32_t capacity)
      : queue_(
            kSPSC ? capacity + 1 // One more extra space because usable space of
                                 // ProducerConsumerQueue used below is (size-1)
                  : capacity),
        enqueueSemaphore_{capacity},
        dequeueSemaphore_{0} {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  template <typename U = T>
  folly::coro::Task<void> enqueue(U&& item) {
    co_await folly::coro::co_nothrow(enqueueSemaphore_.co_wait());
    enqueueReady(std::forward<U>(item));
    dequeueSemaphore_.signal();
  }

  template <typename U = T>
  bool try_enqueue(U&& item) {
    auto waitSuccess = enqueueSemaphore_.try_wait();
    if (!waitSuccess) {
      return false;
    }
    enqueueReady(std::forward<U>(item));
    dequeueSemaphore_.signal();
    return true;
  }

  // Dequeue a value from the queue.
  // Note that this operation can be safely cancelled by requesting cancellation
  // on the awaiting coroutine's associated CancellationToken.
  // If the operation is successfully cancelled then it will complete with
  // an error of type folly::OperationCancelled.
  // WARNING: It is not safe to wrap this with folly::coro::timeout(). Wrap with
  // folly::coro::timeoutNoDiscard(), or use co_try_dequeue_for() instead.
  folly::coro::Task<T> dequeue() {
    co_await folly::coro::co_nothrow(dequeueSemaphore_.co_wait());
    T item;
    dequeueReady(item);
    enqueueSemaphore_.signal();
    co_return item;
  }

  // Try to dequeue a value from the queue with a timeout. The operation will
  // either successfully dequeue an item from the queue, or else be cancelled
  // and complete with an error of type folly::OperationCancelled.
  template <typename Duration>
  folly::coro::Task<T> co_try_dequeue_for(Duration timeout) {
    co_await folly::coro::co_nothrow(
        dequeueSemaphore_.co_try_wait_for(timeout));
    T item;
    dequeueReady(item);
    enqueueSemaphore_.signal();
    co_return item;
  }

  folly::coro::Task<void> dequeue(T& item) {
    co_await folly::coro::co_nothrow(dequeueSemaphore_.co_wait());
    dequeueReady(item);
    enqueueSemaphore_.signal();
  }

  std::optional<T> try_dequeue() {
    T item;
    if (try_dequeue(item)) {
      return item;
    }
    return std::nullopt;
  }

  bool try_dequeue(T& item) {
    auto waitSuccess = dequeueSemaphore_.try_wait();
    if (!waitSuccess) {
      return false;
    }
    dequeueReady(item);
    enqueueSemaphore_.signal();
    return true;
  }

  bool empty() const { return queue_.isEmpty(); }

  size_t size() const {
    if constexpr (kSPSC) {
      return queue_.sizeGuess();
    } else {
      return queue_.size();
    }
  }

 private:
  template <typename U = T>
  void enqueueReady(U&& item) {
    if constexpr (kSPSC) {
      CHECK(queue_.write(std::forward<U>(item)));
    } else {
      // Cannot use write() because the thread that acquired the next ticket may
      // not have completed the read yet.
      CHECK(queue_.writeIfNotFull(std::forward<U>(item)));
    }
  }

  void dequeueReady(T& item) {
    if constexpr (kSPSC) {
      CHECK(queue_.read(item));
    } else {
      // Cannot use read() because the thread that acquired the next ticket may
      // not have completed the write yet.
      CHECK(queue_.readIfNotEmpty(item));
    }
  }

  std::conditional_t<kSPSC, ProducerConsumerQueue<T>, MPMCQueue<T>> queue_;
  folly::fibers::Semaphore enqueueSemaphore_;
  folly::fibers::Semaphore dequeueSemaphore_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
