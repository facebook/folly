/*
 * Copyright 2017-present Facebook, Inc.
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

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <queue>

#include <folly/detail/Futex.h>
#include <folly/experimental/flat_combining/FlatCombining.h>
#include <glog/logging.h>

namespace folly {

/// Thread-safe priority queue based on flat combining. If the
/// constructor parameter maxSize is greater than 0 (default = 0),
/// then the queue is bounded. This template provides blocking,
/// non-blocking, and timed variants of each of push(), pop(), and
/// peek() operations. The empty() and size() functions are inherently
/// non-blocking.
///
/// PriorityQueue must support the interface of std::priority_queue,
/// specifically empty(), size(), push(), top(), and pop().  Mutex
/// must meet the standard Lockable requirements.
///
/// By default FlatCombining uses a dedicated combiner thread, which
/// yields better latency and throughput under high contention but
/// higher overheads under low contention. If the constructor
/// parameter dedicated is false, then there will be no dedicated
/// combiner thread and any requester may do combining of operations
/// requested by other threads. For more details see the comments for
/// FlatCombining.
///
/// Usage examples:
/// @code
///   FlatCombiningPriorityQueue<int> pq(1);
///   CHECK(pq.empty());
///   CHECK(pq.size() == 0);
///   int v;
///   CHECK(!tryPop(v));
///   CHECK(!tryPop(v, now() + seconds(1)));
///   CHECK(!tryPeek(v));
///   CHECK(!tryPeek(v, now() + seconds(1)));
///   pq.push(10);
///   CHECK(!pq.empty());
///   CHECK(pq.size() == 1);
///   CHECK(!pq.tryPush(20));
///   CHECK(!pq.tryPush(20), now() + seconds(1)));
///   peek(v);
///   CHECK_EQ(v, 10);
///   CHECK(pq.size() == 1);
///   pop(v);
///   CHECK_EQ(v, 10);
///   CHECK(pq.empty());
/// @encode

template <
    typename T,
    typename PriorityQueue = std::priority_queue<T>,
    typename Mutex = std::mutex,
    template <typename> class Atom = std::atomic>
class FlatCombiningPriorityQueue
    : public folly::FlatCombining<
          FlatCombiningPriorityQueue<T, PriorityQueue, Mutex, Atom>,
          Mutex,
          Atom> {
  using FCPQ = FlatCombiningPriorityQueue<T, PriorityQueue, Mutex, Atom>;
  using FC = folly::FlatCombining<FCPQ, Mutex, Atom>;

 public:
  template <
      typename... PQArgs,
      typename = decltype(PriorityQueue(std::declval<PQArgs>()...))>
  explicit FlatCombiningPriorityQueue(
      // Concurrent priority queue parameter
      const size_t maxSize = 0,
      // Flat combining parameters
      const bool dedicated = true,
      const uint32_t numRecs = 0,
      const uint32_t maxOps = 0,
      // (Sequential) PriorityQueue Parameters
      PQArgs... args)
      : FC(dedicated, numRecs, maxOps),
        maxSize_(maxSize),
        pq_(std::forward<PQArgs>(args)...) {}

  /// Returns true iff the priority queue is empty
  bool empty() const {
    bool res;
    auto fn = [&] { res = pq_.empty(); };
    const_cast<FCPQ*>(this)->requestFC(fn);
    return res;
  }

  /// Returns the number of items in the priority queue
  size_t size() const {
    size_t res;
    auto fn = [&] { res = pq_.size(); };
    const_cast<FCPQ*>(this)->requestFC(fn);
    return res;
  }

  /// Non-blocking push. Succeeds if there is space in the priority
  /// queue to insert the new item. Tries once if no time point is
  /// provided or until the provided time_point is reached. If
  /// successful, inserts the provided item in the priority queue
  /// according to its priority.
  template <class Clock = std::chrono::steady_clock>
  bool tryPush(
      const T& val,
      const std::chrono::time_point<Clock>& when =
          std::chrono::time_point<Clock>::min());

  /// Non-blocking pop. Succeeds if the priority queue is
  /// nonempty. Tries once if no time point is provided or until the
  /// provided time_point is reached.  If successful, copies the
  /// highest priority item and removes it from the priority queue.
  template <class Clock = std::chrono::steady_clock>
  bool tryPop(
      T& val,
      const std::chrono::time_point<Clock>& when =
          std::chrono::time_point<Clock>::min());

  /// Non-blocking peek. Succeeds if the priority queue is
  /// nonempty. Tries once if no time point is provided or until the
  /// provided time_point is reached.  If successful, copies the
  /// highest priority item without removing it.
  template <class Clock = std::chrono::steady_clock>
  bool tryPeek(
      T& val,
      const std::chrono::time_point<Clock>& when =
          std::chrono::time_point<Clock>::min());

  /// Blocking push. Inserts the provided item in the priority
  /// queue. If it is full, this function blocks until there is space
  /// for the new item.
  void push(const T& val) {
    tryPush(val, std::chrono::time_point<std::chrono::steady_clock>::max());
  }

  /// Blocking pop. Copies the highest priority item and removes
  /// it. If the priority queue is empty, this function blocks until
  /// it is nonempty.
  void pop(T& val) {
    tryPop(val, std::chrono::time_point<std::chrono::steady_clock>::max());
  }

  /// Blocking peek. Copies the highest priority item without
  /// removing it. If the priority queue is empty, this function
  /// blocks until it is nonempty.
  void peek(T& val) {
    tryPeek(val, std::chrono::time_point<std::chrono::steady_clock>::max());
  }

 private:
  size_t maxSize_;
  PriorityQueue pq_;
  detail::Futex<Atom> empty_{};
  detail::Futex<Atom> full_{};

  bool isTrue(detail::Futex<Atom>& futex) {
    return futex.load(std::memory_order_relaxed) != 0;
  }

  void setFutex(detail::Futex<Atom>& futex, uint32_t val) {
    futex.store(val, std::memory_order_relaxed);
  }

  bool futexSignal(detail::Futex<Atom>& futex) {
    if (isTrue(futex)) {
      setFutex(futex, 0);
      return true;
    } else {
      return false;
    }
  }
};

/// Implementation

template <
    typename T,
    typename PriorityQueue,
    typename Mutex,
    template <typename> class Atom>
template <class Clock>
inline bool FlatCombiningPriorityQueue<T, PriorityQueue, Mutex, Atom>::tryPush(
    const T& val,
    const std::chrono::time_point<Clock>& when) {
  while (true) {
    bool res;
    bool wake;

    auto fn = [&] {
      if (maxSize_ > 0 && pq_.size() == maxSize_) {
        setFutex(full_, 1);
        res = false;
        return;
      }
      DCHECK(maxSize_ == 0 || pq_.size() < maxSize_);
      try {
        pq_.push(val);
        wake = futexSignal(empty_);
        res = true;
        return;
      } catch (const std::bad_alloc&) {
        setFutex(full_, 1);
        res = false;
        return;
      }
    };
    this->requestFC(fn);

    if (res) {
      if (wake) {
        empty_.futexWake();
      }
      return true;
    }
    if (when == std::chrono::time_point<Clock>::min()) {
      return false;
    }
    while (isTrue(full_)) {
      if (when == std::chrono::time_point<Clock>::max()) {
        full_.futexWait(1);
      } else {
        if (Clock::now() > when) {
          return false;
        } else {
          full_.futexWaitUntil(1, when);
        }
      }
    } // inner while loop
  } // outer while loop
}

template <
    typename T,
    typename PriorityQueue,
    typename Mutex,
    template <typename> class Atom>
template <class Clock>
inline bool FlatCombiningPriorityQueue<T, PriorityQueue, Mutex, Atom>::tryPop(
    T& val,
    const std::chrono::time_point<Clock>& when) {
  while (true) {
    bool res;
    bool wake;

    auto fn = [&] {
      res = !pq_.empty();
      if (res) {
        val = pq_.top();
        pq_.pop();
        wake = futexSignal(full_);
      } else {
        setFutex(empty_, 1);
      }
    };
    this->requestFC(fn);

    if (res) {
      if (wake) {
        full_.futexWake();
      }
      return true;
    }
    while (isTrue(empty_)) {
      if (when == std::chrono::time_point<Clock>::max()) {
        empty_.futexWait(1);
      } else {
        if (Clock::now() > when) {
          return false;
        } else {
          empty_.futexWaitUntil(1, when);
        }
      }
    } // inner while loop
  } // outer while loop
}

template <
    typename T,
    typename PriorityQueue,
    typename Mutex,
    template <typename> class Atom>
template <class Clock>
inline bool FlatCombiningPriorityQueue<T, PriorityQueue, Mutex, Atom>::tryPeek(
    T& val,
    const std::chrono::time_point<Clock>& when) {
  while (true) {
    bool res;

    auto fn = [&] {
      res = !pq_.empty();
      if (res) {
        val = pq_.top();
      } else {
        setFutex(empty_, 1);
      }
    };
    this->requestFC(fn);

    if (res) {
      return true;
    }
    while (isTrue(empty_)) {
      if (when == std::chrono::time_point<Clock>::max()) {
        empty_.futexWait(1);
      } else {
        if (Clock::now() > when) {
          return false;
        } else {
          empty_.futexWaitUntil(1, when);
        }
      }
    } // inner while loop
  } // outer while loop
}

} // namespace folly
