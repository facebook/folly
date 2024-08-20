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
#include <limits>

#include <folly/Portability.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/experimental/io/EventBasePoller.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/Baton.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/ThrottledLifoSem.h>
#include <folly/synchronization/WaitOptions.h>

namespace folly {

/**
 * NOTE: This is highly experimental. Do not use.
 *
 * A pool of EventBases scheduled over a pool of threads.
 *
 * Intended as a drop-in replacement for folly::IOThreadPoolExecutor, but with a
 * substantially different design: EventBases are not pinned to threads, so it
 * is possible to have more EventBases than threads. EventBases that have ready
 * events can be scheduled on any of the threads in the pool, with the
 * scheduling governed by ThrottledLifoSem.
 *
 * This allows to batch the loops of multiple EventBases on a single thread as
 * long as each runs for a short enough time, reducing the number of wake-ups
 * and allowing for better load balancing across handlers. For example, we can
 * create a large number of EventBases processed by a smaller number of threads
 * and distribute the handlers.
 *
 * The number of EventBases is set at construction time and cannot be changed
 * later. The number of threads can be changed dynamically, but setting it to 0
 * is not supported (otherwise no thread would be left to drive the EventBases)
 * and it is not useful to run more threads than EventBases, so that is not
 * supported either: attempting to set the number of threads to 0 or to a value
 * greater than numEventBases() (either in construction or using
 * setNumThreads()) will throw std::invalid_argument).
 */
class MuxIOThreadPoolExecutor : public IOThreadPoolExecutorBase {
 public:
  struct Options {
    Options() {}

    Options& setEnableThreadIdCollection(bool b) {
      enableThreadIdCollection = b;
      return *this;
    }

    Options& setNumEventBases(size_t num) {
      numEventBases = num;
      return *this;
    }

    Options& setWakeUpInterval(std::chrono::nanoseconds w) {
      wakeUpInterval = w;
      return *this;
    }

    Options& setIdleSpinMax(std::chrono::nanoseconds s) {
      idleSpinMax = s;
      return *this;
    }

    bool enableThreadIdCollection{false};
    // If 0, the number of EventBases is set to the number of threads.
    size_t numEventBases{0};
    std::chrono::nanoseconds wakeUpInterval{std::chrono::microseconds{100}};
    // Max spin for an idle thread waiting for work before going to sleep.
    std::chrono::nanoseconds idleSpinMax = std::chrono::microseconds{10};
  };

  explicit MuxIOThreadPoolExecutor(
      size_t numThreads,
      Options options = {},
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("MuxIOTPEx"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get());

  ~MuxIOThreadPoolExecutor() override;

  size_t numEventBases() const { return numEventBases_; }

  void add(Func func) override;
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  folly::EventBase* getEventBase() override;

  // Returns all the EventBase instances
  std::vector<folly::Executor::KeepAlive<folly::EventBase>> getAllEventBases()
      override;

  folly::EventBaseManager* getEventBaseManager() override;

  // Returns nullptr unless explicitly enabled through constructor
  folly::WorkerProvider* getThreadIdCollector() override {
    return threadIdCollector_.get();
  }

  void addObserver(std::shared_ptr<Observer> o) override;
  void removeObserver(std::shared_ptr<Observer> o) override;

  void stop() override;
  void join() override;

 private:
  using EventBasePoller = folly::detail::EventBasePoller;

  struct EvbState;

  struct alignas(Thread) IOThread : public Thread {
    EvbState* curEvbState; // Only accessed inside the worker thread.
  };

  void maybeUnregisterEventBases(Observer* o);

  void validateNumThreads(size_t numThreads) override;
  ThreadPtr makeThread() override;
  EvbState& pickEvbState();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  const Options options_;
  const size_t numEventBases_;
  folly::EventBaseManager* eventBaseManager_;

  std::unique_ptr<EventBasePoller::FdGroup> fdGroup_;
  std::vector<std::unique_ptr<EvbState>> evbStates_;
  std::vector<Executor::KeepAlive<EventBase>> keepAlives_;

  relaxed_atomic<size_t> nextEvb_{0};
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;
  std::atomic<size_t> pendingTasks_{0};

  USPMCQueue<EventBasePoller::Handle*, /* MayBlock */ false> readyQueue_;
  folly::ThrottledLifoSem readyQueueSem_;
};

} // namespace folly
