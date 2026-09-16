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

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <folly/concurrency/CacheLocality.h>

namespace folly {

/// ProducerConsumerQueue.
///
/// A wait-free single-producer single-consumer fixed-size queue.
///
/// Queue elements are stored in a single ring buffer. If the queue is full,
/// enqueue operations (writes) fail by returning false. If the queue is empty,
/// dequeue operations (reads) fail by returning false. The ring buffer is fixed
/// at its initial capacity and is never grown.
///
/// For performance, the fields are divided between common, producer-owned, and
/// consumer-owned cache-lines. The producer maintains a cache of the consumer's
/// index and vice versa, in order to accelerate the checks of whether the queue
/// is full or empty.
template <class T>
struct alignas(hardware_destructive_interference_size) ProducerConsumerQueue {
  using value_type = T;

  ProducerConsumerQueue(const ProducerConsumerQueue&) = delete;
  ProducerConsumerQueue& operator=(const ProducerConsumerQueue&) = delete;

  // size must be >= 2.
  //
  // Also, note that the number of usable slots in the queue at any
  // given time is actually (size-1), so if you start with an empty queue,
  // isFull() will return true after size-1 insertions.
  explicit ProducerConsumerQueue(uint32_t size)
      : size_(size), records_(static_cast<T*>(std::malloc(sizeof(T) * size))) {
    assert(size >= 2);
    if (!records_) {
      throw std::bad_alloc();
    }
  }

  ~ProducerConsumerQueue() {
    // We need to destruct anything that may still exist in our queue.
    // (No real synchronization needed at destructor time: only one
    // thread can be doing this.)
    if (!std::is_trivially_destructible<T>::value) {
      auto readIndex = readIndex_.load(std::memory_order_relaxed);
      auto const endIndex = writeIndex_.load(std::memory_order_relaxed);
      while (readIndex != endIndex) {
        records_[readIndex % size_].~T();
        ++readIndex;
      }
    }

    std::free(records_);
  }

  //  Checks a private, unsynchronized cache of the other side's cursor
  //  before touching the real cross-thread atomic. writeIndex_/readIndex_
  //  only ever increase, so a stale cached value is always a safe
  //  (conservative) underestimate of the other side's true progress - it is
  //  always safe to trust the cache when it says there is room/data, and a
  //  fresh cross-thread read is only needed when it says there might not
  //  be. In steady state, where the two sides stay within a queue's worth
  //  of each other, that real read is rare rather than universal. This
  //  relies on the cursors being ever-increasing logical counters, wrapped
  //  via `% size_` only at the point of indexing into records_, never
  //  wrapped in the stored/compared value itself - with a wrapped cursor,
  //  a stale cache can alias to the wrong answer in either direction, since
  //  "behind" is no longer a total order once values cycle. The comparisons
  //  below must be magnitude checks (`>=`/subtraction), not equality: the
  //  cache is only ever refreshed from within write()/read()/frontPtr(), so
  //  a caller that advances its own cursor another way (e.g. popFront()
  //  after an external frontPtr()) can leave the far side's cache stuck
  //  behind the local cursor by more than zero - equality would then never
  //  match again and the cache would be trusted forever after it stopped
  //  meaning anything.
  template <class... Args>
  bool write(Args&&... recordArgs) {
    auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
    if (currentWrite - readIndexCache_ >= size_ - 1) {
      readIndexCache_ = readIndex_.load(std::memory_order_acquire);
      if (currentWrite - readIndexCache_ >= size_ - 1) {
        return false; // queue is full
      }
    }
    new (&records_[currentWrite % size_]) T(std::forward<Args>(recordArgs)...);
    writeIndex_.store(currentWrite + 1, std::memory_order_release);
    return true;
  }

  // move (or copy) the value at the front of the queue to given variable
  bool read(T& record) {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    if (currentRead >= writeIndexCache_) {
      writeIndexCache_ = writeIndex_.load(std::memory_order_acquire);
      if (currentRead >= writeIndexCache_) {
        return false; // queue is empty
      }
    }
    auto const idx = currentRead % size_;
    record = std::move(records_[idx]);
    records_[idx].~T();
    readIndex_.store(currentRead + 1, std::memory_order_release);
    return true;
  }

  // pointer to the value at the front of the queue (for use in-place) or
  // nullptr if empty. Cached the same way as read(); see write()'s comment.
  T* frontPtr() {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    if (currentRead >= writeIndexCache_) {
      writeIndexCache_ = writeIndex_.load(std::memory_order_acquire);
      if (currentRead >= writeIndexCache_) {
        // queue is empty
        return nullptr;
      }
    }
    return &records_[currentRead % size_];
  }

  // queue must not be empty
  void popFront() {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    assert(currentRead != writeIndex_.load(std::memory_order_acquire));

    auto const idx = currentRead % size_;
    records_[idx].~T();
    readIndex_.store(currentRead + 1, std::memory_order_release);
  }

  // * If called by consumer, then true size may be more (because producer may
  //   be adding items concurrently).
  // * If called by producer, then true size may be less (because consumer may
  //   be removing items concurrently).
  // * It is undefined to call this from any other thread.
  size_t sizeGuess() const {
    return writeIndex_.load(std::memory_order_acquire) -
        readIndex_.load(std::memory_order_acquire);
  }

  bool isEmpty() const { return sizeGuess() == 0; }

  bool isFull() const { return sizeGuess() == size_ - 1; }

  // maximum number of items in the queue.
  size_t capacity() const { return size_ - 1; }

 private:
  using AtomicIndex = std::atomic<uint64_t>;

  //  One line of state common to both sides: read-only after construction,
  //  so concurrent reads of it from both threads never contend.
  const uint32_t size_;
  T* const records_;

  //  One line owned by the producer: writeIndex_ is written on every
  //  write(), and readIndexCache_ is the producer's own cached lower bound
  //  on the consumer's readIndex_, read every write() and refreshed only
  //  rarely. The consumer only ever reaches this line via its own rare
  //  refresh of writeIndexCache_.
  alignas(hardware_destructive_interference_size) AtomicIndex writeIndex_{0};
  uint64_t readIndexCache_{0};

  //  One line owned by the consumer, symmetric with the producer's.
  alignas(hardware_destructive_interference_size) AtomicIndex readIndex_{0};
  uint64_t writeIndexCache_{0};
};

} // namespace folly
