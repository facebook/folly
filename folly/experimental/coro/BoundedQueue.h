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
#include <folly/experimental/coro/Task.h>
#include <folly/fibers/Semaphore.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// A coroutine version of bounded queue with given capacity. Both enqueue and
// dequeue are async awaitable.
template <typename T, bool SingleProducer = false, bool SingleConsumer = false>
class BoundedQueue {
 public:
  explicit BoundedQueue(uint32_t capacity)
      : queue_(
            SingleProducer && SingleConsumer
                ? capacity + 1 // One more extra space because usable space of
                               // ProducerConsumerQueue used below is (size-1)
                : capacity),
        enqueueSemaphore_{capacity},
        dequeueSemaphore_{0} {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  template <typename U = T>
  folly::coro::Task<void> enqueue(U&& item) {
    co_await enqueueSemaphore_.co_wait();
    queue_.write(std::forward<U>(item));
    dequeueSemaphore_.signal();
  }

  template <typename U = T>
  bool try_enqueue(U&& item) {
    auto waitSuccess = enqueueSemaphore_.try_wait();
    if (!waitSuccess) {
      return false;
    }
    queue_.write(std::forward<U>(item));
    dequeueSemaphore_.signal();
    return true;
  }

  folly::coro::Task<T> dequeue() {
    co_await dequeueSemaphore_.co_wait();
    T item;
    queue_.read(item);
    enqueueSemaphore_.signal();
    co_return item;
  }

  folly::coro::Task<void> dequeue(T& item) {
    co_await dequeueSemaphore_.co_wait();
    queue_.read(item);
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
    queue_.read(item);
    enqueueSemaphore_.signal();
    return true;
  }

  bool empty() const { return queue_.isEmpty(); }

  size_t size() const {
    if constexpr (SingleProducer && SingleConsumer) {
      return queue_.sizeGuess();
    } else {
      return queue_.size();
    }
  }

 private:
  std::conditional_t<
      SingleProducer && SingleConsumer,
      ProducerConsumerQueue<T>,
      MPMCQueue<T>>
      queue_;
  folly::fibers::Semaphore enqueueSemaphore_;
  folly::fibers::Semaphore dequeueSemaphore_;
};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
