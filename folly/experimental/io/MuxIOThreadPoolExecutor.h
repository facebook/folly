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

#include <folly/experimental/io/Epoll.h>

#if FOLLY_HAS_EPOLL

#include <chrono>
#include <limits>

#include <folly/Portability.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/executors/task_queue/UnboundedBlockingQueue.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/ThrottledLifoSem.h>

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
 * TODO(ott): Fully support setNumThreads().
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

    Options& setMaxEvents(size_t v) {
      maxEvents = v;
      return *this;
    }

    bool enableThreadIdCollection{false};
    // If 0, the number of EventBases is set to the number of threads.
    size_t numEventBases{0};
    std::chrono::nanoseconds wakeUpInterval{std::chrono::microseconds(100)};
    size_t maxEvents{64};
  };

  struct Stats {
    // Track number of epoll wake-ups and number of events returned.
    int minNumEvents{std::numeric_limits<int>::max()};
    int maxNumEvents{std::numeric_limits<int>::min()};
    size_t totalNumEvents{0};
    size_t totalWakeups{0};
    std::chrono::microseconds totalWait{0};
    std::chrono::microseconds minWait{std::chrono::microseconds::max()};
    std::chrono::microseconds maxWait{std::chrono::microseconds::min()};

    void update(int numEvents, std::chrono::microseconds wait);
  };

  explicit MuxIOThreadPoolExecutor(
      size_t numThreads,
      Options options = {},
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("MuxIOTPEx"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get());

  ~MuxIOThreadPoolExecutor() override;

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
  struct alignas(Thread) IOThread : public Thread {
    explicit IOThread(MuxIOThreadPoolExecutor* pool) : Thread(pool) {}

    EventBase* curEventBase; // Only accessed inside the worker thread.
  };

  struct Handler {
    virtual ~Handler() = default;
    virtual void handle(MuxIOThreadPoolExecutor* parent) = 0;
    virtual bool isEvbHandler() const { return false; }

    int fd{-1};
  };

  struct EvbHandler : public Handler {
    EvbHandler() = default;
    explicit EvbHandler(folly::EventBase* e);
    void handle(MuxIOThreadPoolExecutor* parent) override;

    folly::EventBase* evb{nullptr};

    bool isEvbHandler() const override { return true; }

    bool isPoison() const { return evb == nullptr; }

    EvbHandler* next{nullptr};
  };

  struct EventFdHandler : public Handler {
    EventFdHandler();
    ~EventFdHandler() override;

    void notifyFd();
    void drainFd();

    void handle(MuxIOThreadPoolExecutor* parent) override;
  };

  template <class T>
  class Queue {
   public:
    bool insert(T* t);
    T* arm();

   private:
    static T* kQueueArmedTag() { return reinterpret_cast<T*>(1); }

    std::atomic<T*> head_{nullptr};
  };

  enum class AddHandlerType { kPersist, kOneShot, kOneShotRearm };

  void addHandler(Handler* handler, AddHandlerType type);
  void returnHandler(EvbHandler* handler);
  void mainThreadFunc();

  void handleDequeue();

  void maybeUnregisterEventBases(Observer* o);

  ThreadPtr makeThread() override;
  folly::EventBase* pickEvb();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  std::unique_ptr<folly::EventBase> makeEventBase();

  const Options options_;
  folly::EventBaseManager* eventBaseManager_;

  int epFd_{-1};
  relaxed_atomic<size_t> nextEvb_{0};
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;
  std::vector<std::unique_ptr<folly::EventBase>> evbs_;
  std::vector<Executor::KeepAlive<EventBase>> keepAlives_;
  std::vector<std::unique_ptr<EvbHandler>> handlers_;
  std::unique_ptr<std::thread> mainThread_;
  std::atomic<bool> stop_{false};
  std::atomic<size_t> pendingTasks_{0};
  folly::UnboundedBlockingQueue<EvbHandler*, folly::ThrottledLifoSem> queue_;
  Stats stats_;

  Queue<EvbHandler> returnQueue_;
  EventFdHandler returnEvfd_;
};

} // namespace folly
#endif
