/*
 * Copyright 2012 Facebook, Inc.
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

// @author Bo Hu (bhu@fb.com)
// @author Jordan DeLong (delong.j@fb.com)

#ifndef PRODUCER_CONSUMER_QUEUE_H_
#define PRODUCER_CONSUMER_QUEUE_H_

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <boost/noncopyable.hpp>

namespace folly {

/*
 * ProducerConsumerQueue is a one producer and one consumer queue
 * without locks.
 */
template<class T>
struct ProducerConsumerQueue : private boost::noncopyable {
  typedef T value_type;

  // size must be >= 2
  explicit ProducerConsumerQueue(uint32_t size)
    : size_(size)
    , records_(static_cast<T*>(std::malloc(sizeof(T) * size)))
    , readIndex_(0)
    , writeIndex_(0)
  {
    assert(size >= 2);
    if (!records_) {
      throw std::bad_alloc();
    }
  }

  ~ProducerConsumerQueue() {
    // We need to destruct anything that may still exist in our queue.
    // (No real synchronization needed at destructor time: only one
    // thread can be doing this.)
    if (!std::has_trivial_destructor<T>::value) {
      int read = readIndex_;
      int end = writeIndex_;
      while (read != end) {
        records_[read].~T();
        if (++read == size_) {
          read = 0;
        }
      }
    }

    std::free(records_);
  }

  template<class ...Args>
  bool write(Args&&... recordArgs) {
    auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
    auto nextRecord = currentWrite + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }
    if (nextRecord != readIndex_.load(std::memory_order_acquire)) {
      new (&records_[currentWrite]) T(std::forward<Args>(recordArgs)...);
      writeIndex_.store(nextRecord, std::memory_order_release);
      return true;
    }

    // queue is full
    return false;
  }

  bool read(T& record) {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
      // queue is empty
      return false;
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

  bool isFull() const {
    auto nextRecord = writeIndex_.load(std::memory_order_consume) + 1;
    if (nextRecord == size_) {
      nextRecord = 0;
    }
    if (nextRecord != readIndex_.load(std::memory_order_consume)) {
      return false;
    }
    // queue is full
    return true;
  }

private:
  const uint32_t size_;
  T* const records_;

  std::atomic<int> readIndex_;
  std::atomic<int> writeIndex_;
};

}

#endif
