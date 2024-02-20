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

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include <folly/container/IntrusiveHeap.h>
#include <folly/futures/Future.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/SaturatingSemaphore.h>

namespace folly {

/**
 * A Timekeeper with a dedicated thread that manages the timeouts using a
 * heap. Timeouts can be scheduled with microsecond resolution, though in
 * practice the accuracy depends on the OS scheduler's ability to wake up the
 * worker thread in a timely fashion.
 */
class HeapTimekeeper : public Timekeeper {
 public:
  HeapTimekeeper();
  ~HeapTimekeeper() override;

  SemiFuture<Unit> after(HighResDuration) override;

 private:
  using Clock = std::chrono::steady_clock;
  using Semaphore = SaturatingSemaphore<>;

  static constexpr size_t kQueueBatchSize = 256;
  // Queue capacity is kept in this band to make sure that it is reallocated
  // under the lock as infrequently as possible.
  static constexpr size_t kDefaultQueueCapacity = 2 * kQueueBatchSize;
  static constexpr size_t kMaxQueueCapacity = 2 * kDefaultQueueCapacity;

  class Timeout : public IntrusiveHeapNode<> {
   public:
    struct DecRef {
      void operator()(Timeout* timeout) const { timeout->decRef(); }
    };
    using Ref = std::unique_ptr<Timeout, DecRef>;

    static std::pair<Ref, SemiFuture<Unit>> create(
        HeapTimekeeper& parent, Clock::time_point expiration);

    void decRef();
    bool tryFulfill(Try<Unit> t);

    bool operator<(const Timeout& other) const {
      return expiration > other.expiration;
    }

    const Clock::time_point expiration;

   private:
    static void interruptHandler(
        Ref self, HeapTimekeeper& parent, exception_wrapper ew);

    Timeout(
        HeapTimekeeper& timekeeper,
        Clock::time_point exp,
        Promise<Unit> promise);

    std::atomic<uint8_t> refCount_ = 2; // Heap and interrupt handler.
    relaxed_atomic<bool> fulfilled_ = false;
    Promise<Unit> promise_;
  };

  struct Op {
    enum class Type { kSchedule, kCancel };

    Type type;
    Timeout::Ref timeout;
  };

  static void clearAndAdjustCapacity(std::vector<Op>& queue);

  void enqueue(Op::Type type, Timeout::Ref&& timeout);
  void shutdown();
  void worker();

  DistributedMutex mutex_;
  // These variables are synchronized using mutex_. nextWakeUp_ is only modified
  // by the worker thread, so it can be read back in that thread without a lock.
  bool stop_ = false;
  std::vector<Op> queue_;
  Clock::time_point nextWakeUp_ = Clock::time_point::max();
  Semaphore* wakeUp_ = nullptr;

  std::thread thread_;
  // Only accessed by the worker thread.
  IntrusiveHeap<Timeout> heap_;
};

} // namespace folly
