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

#ifndef FOLLY_EXPERIMENTAL_EVENTCOUNT_H_
#define FOLLY_EXPERIMENTAL_EVENTCOUNT_H_

#include <unistd.h>
#include <syscall.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <climits>
#include <atomic>
#include <thread>


namespace folly {

namespace detail {

inline int futex(int* uaddr, int op, int val, const timespec* timeout,
                 int* uaddr2, int val3) noexcept {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

}  // namespace detail

/**
 * Event count: a condition variable for lock free algorithms.
 *
 * See http://www.1024cores.net/home/lock-free-algorithms/eventcounts for
 * details.
 *
 * Event counts allow you to convert a non-blocking lock-free / wait-free
 * algorithm into a blocking one, by isolating the blocking logic.  You call
 * prepareWait() before checking your condition and then either cancelWait()
 * or wait() depending on whether the condition was true.  When another
 * thread makes the condition true, it must call notify() / notifyAll() just
 * like a regular condition variable.
 *
 * If "<" denotes the happens-before relationship, consider 2 threads (T1 and
 * T2) and 3 events:
 * - E1: T1 returns from prepareWait
 * - E2: T1 calls wait
 *   (obviously E1 < E2, intra-thread)
 * - E3: T2 calls notifyAll
 *
 * If E1 < E3, then E2's wait will complete (and T1 will either wake up,
 * or not block at all)
 *
 * This means that you can use an EventCount in the following manner:
 *
 * Waiter:
 *   if (!condition()) {  // handle fast path first
 *     for (;;) {
 *       auto key = eventCount.prepareWait();
 *       if (condition()) {
 *         eventCount.cancelWait();
 *         break;
 *       } else {
 *         eventCount.wait(key);
 *       }
 *     }
 *  }
 *
 *  (This pattern is encapsulated in await())
 *
 * Poster:
 *   make_condition_true();
 *   eventCount.notifyAll();
 *
 * Note that, just like with regular condition variables, the waiter needs to
 * be tolerant of spurious wakeups and needs to recheck the condition after
 * being woken up.  Also, as there is no mutual exclusion implied, "checking"
 * the condition likely means attempting an operation on an underlying
 * data structure (push into a lock-free queue, etc) and returning true on
 * success and false on failure.
 */
class EventCount {
 public:
  EventCount() noexcept : epoch_(0), waiters_(0) { }

  class Key {
    friend class EventCount;
    explicit Key(int e) noexcept : epoch_(e) { }
    int epoch_;
  };

  void notify() noexcept;
  void notifyAll() noexcept;
  Key prepareWait() noexcept;
  void cancelWait() noexcept;
  void wait(Key key) noexcept;

  /**
   * Wait for condition() to become true.  Will clean up appropriately if
   * condition() throws, and then rethrow.
   */
  template <class Condition>
  void await(Condition condition);

 private:
  void doNotify(int n) noexcept;
  EventCount(const EventCount&) = delete;
  EventCount(EventCount&&) = delete;
  EventCount& operator=(const EventCount&) = delete;
  EventCount& operator=(EventCount&&) = delete;

  std::atomic<int> epoch_;
  std::atomic<int> waiters_;
};

inline void EventCount::notify() noexcept {
  doNotify(1);
}

inline void EventCount::notifyAll() noexcept {
  doNotify(INT_MAX);
}

inline void EventCount::doNotify(int n) noexcept {
  // The order is important: epoch_ is incremented before waiters_ is checked.
  // prepareWait() increments waiters_ before checking epoch_, so it is
  // impossible to miss a wakeup.
  ++epoch_;
  if (waiters_ != 0) {
    detail::futex(reinterpret_cast<int*>(&epoch_), FUTEX_WAKE, n, nullptr,
                  nullptr, 0);
  }
}

inline EventCount::Key EventCount::prepareWait() noexcept {
  ++waiters_;
  return Key(epoch_);
}

inline void EventCount::cancelWait() noexcept {
  --waiters_;
}

inline void EventCount::wait(Key key) noexcept {
  while (epoch_ == key.epoch_) {
    detail::futex(reinterpret_cast<int*>(&epoch_), FUTEX_WAIT, key.epoch_,
                  nullptr, nullptr, 0);
  }
  --waiters_;
}

template <class Condition>
void EventCount::await(Condition condition) {
  if (condition()) return;  // fast path

  // condition() is the only thing that may throw, everything else is
  // noexcept, so we can hoist the try/catch block outside of the loop
  try {
    for (;;) {
      auto key = prepareWait();
      if (condition()) {
        cancelWait();
        break;
      } else {
        wait(key);
      }
    }
  } catch (...) {
    cancelWait();
    throw;
  }
}

}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_EVENTCOUNT_H_ */

