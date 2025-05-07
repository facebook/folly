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

/*
 * ProducerConsumerQueue is a one producer and one consumer queue
 * without locks.
 */
template <class T>
struct alignas(hardware_destructive_interference_size) ProducerConsumerQueue {
  typedef T value_type;

  ProducerConsumerQueue(const ProducerConsumerQueue&) = delete;
  ProducerConsumerQueue& operator=(const ProducerConsumerQueue&) = delete;

  // size must be >= 2.
  //
  // Also, note that the number of usable slots in the queue at any
  // given time is actually (size-1), so if you start with an empty queue,
  // isFull() will return true after size-1 insertions.
  explicit ProducerConsumerQueue(uint32_t size)
      : size_(size),
        records_(static_cast<T*>(std::malloc(sizeof(T) * size))),
        readIndex_(0),
        writeIndexCache_(0),
        writeIndex_(0),
        readIndexCache_(0) {
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
      size_t readIndex = readIndex_;
      size_t endIndex = writeIndex_;
      while (readIndex != endIndex) {
        records_[readIndex].~T();
        if (++readIndex == size_) {
          readIndex = 0;
        }
      }
    }

    std::free(records_);
  }

  template <class... Args>
  bool write(Args&&... recordArgs) {
    auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
    auto nextRecord = currentWrite + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }

    auto readIndex = readIndexCache_;
    if (nextRecord == readIndex) {
      readIndex = readIndexCache_ = readIndex_.load(std::memory_order_acquire);
      if (nextRecord == readIndex) {
        return false;
      }
    }

    new (&records_[currentWrite]) T(std::forward<Args>(recordArgs)...);
    writeIndex_.store(nextRecord, std::memory_order_release);
    return true;
  }

  // move (or copy) the value at the front of the queue to given variable
  bool read(T& record) {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    auto writeIndex = writeIndexCache_;
    if (currentRead == writeIndex) {
      writeIndex = writeIndexCache_ = writeIndex_.load(std::memory_order_acquire);
      if (currentRead == writeIndex) {
        // queue is empty
        return false;
      }
    }

    auto nextRecord = currentRead + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }
    record = std::move(records_[currentRead]);
    records_[currentRead].~T();
    readIndex_.store(nextRecord, std::memory_order_release);
    return true;
  }

  // pointer to the value at the front of the queue (for use in-place) or
  // nullptr if empty.
  T* frontPtr() {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    auto writeIndex = writeIndexCache_;
    if (currentRead == writeIndex) {
      writeIndex = writeIndexCache_ = writeIndex_.load(std::memory_order_acquire);
      if (currentRead == writeIndex) {
        // queue is empty
        return nullptr;
      }
    }
    return &records_[currentRead];
  }

  // queue must not be empty
  void popFront() {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    assert(currentRead != writeIndex_.load(std::memory_order_acquire));

    auto nextRecord = currentRead + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }
    records_[currentRead].~T();
    readIndex_.store(nextRecord, std::memory_order_release);
  }

  bool isEmpty() const {
    return readIndex_.load(std::memory_order_acquire) ==
        writeIndex_.load(std::memory_order_acquire);
  }

  bool isFull() const {
    auto nextRecord = writeIndex_.load(std::memory_order_acquire) + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }
    if (nextRecord != readIndex_.load(std::memory_order_acquire)) {
      return false;
    }
    // queue is full
    return true;
  }

  // * If called by consumer, then true size may be more (because producer may
  //   be adding items concurrently).
  // * If called by producer, then true size may be less (because consumer may
  //   be removing items concurrently).
  // * It is undefined to call this from any other thread.
  size_t sizeGuess() const {
    int ret = writeIndex_.load(std::memory_order_acquire) -
        readIndex_.load(std::memory_order_acquire);
    if (ret < 0) {
      ret += size_;
    }
    return ret;
  }

  // maximum number of items in the queue.
  size_t capacity() const { return size_ - 1; }

 private:
  using IndexType = unsigned int;
  using AtomicIndex = std::atomic<IndexType>;

  const uint32_t size_;
  T* const records_;

  alignas(hardware_destructive_interference_size) AtomicIndex readIndex_;
  IndexType writeIndexCache_;
  alignas(hardware_destructive_interference_size) AtomicIndex writeIndex_;
  IndexType readIndexCache_;
};

} // namespace folly
