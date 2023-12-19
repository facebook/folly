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

#include <folly/AtomicLinkedList.h>
#include <folly/Portability.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/executors/task_queue/UnboundedBlockingQueue.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/ThrottledLifoSem.h>

namespace folly {
/**
 * A pool of EventBases scheduled over a pool of threads
 *
 * Similar to the folly::IOThreadPoolExecutor with the following differences
 * The number of event bases can be larger than the number of threads
 * Event base loops will be scheduled on an available thread based on the
 * wakeup interval specified in the options - the logic to wake up threads is
 * described in the ThrottledLifoSem documentation
 * This allows us to reduce the number of wakeups and also to allow for better
 * load balancing across handlers - for example, we can create a large number of
 * event bases processed by a smaller number of threads and distribute the
 * handlersA pool of EventBases scheduled over a pool of threads.
 * We currently do not support dynamically changing the number of threads at
 * runtime
 */
class MuxIOThreadPoolExecutor : public IOThreadPoolExecutorBase {
 public:
  struct Options {
    Options() {}

    Options& setEnableThreadIdCollection(bool b) {
      enableThreadIdCollection = b;
      return *this;
    }

    Options& setNumEVBs(size_t num) {
      numEvbs = num;
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
    size_t numEvbs{0}; // 0 means number of threads
    std::chrono::nanoseconds wakeUpInterval{std::chrono::microseconds(100)};
    size_t maxEvents{64};
  };

  struct Stats {
    // events - number of events returned
    // from the epoll_wait
    int minNumEvents{-1};
    int maxNumEvents{-1};
    size_t totalNumEvents{0};
    size_t totalWakeups{0};
    std::chrono::microseconds totalWait{};
    std::chrono::microseconds minWait{0};
    std::chrono::microseconds maxWait{0};

    void update(int numEvents, std::chrono::microseconds wait);
  };

  explicit MuxIOThreadPoolExecutor(
      size_t numThreads,
      Options options = {},
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("MuxIOThreadPoolExecutor"),
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

  // Returns nullptr unless explicitly enabled through constructor
  folly::WorkerProvider* getThreadIdCollector() override {
    return threadIdCollector_.get();
  }

  void addObserver(std::shared_ptr<Observer> o) override;
  void removeObserver(std::shared_ptr<Observer> o) override;

 private:
  struct alignas(Thread) IOThread : public Thread {
    explicit IOThread(MuxIOThreadPoolExecutor* pool) : Thread(pool) {}

    std::atomic<bool> shouldRun{true};
  };

  struct Handler {
    folly::AtomicIntrusiveLinkedListHook<Handler> hook_;
    int fd{-1};
    const bool bHandleInline;

    explicit Handler(bool handleInline) : bHandleInline(handleInline) {}

    bool handleInline() const { return bHandleInline; }

    Handler*& next() { return hook_.next; }

    virtual ~Handler();
    virtual void handle(MuxIOThreadPoolExecutor* parent) = 0;
    virtual folly::EventBase* getEventBase() { return nullptr; }
  };

  struct EvbHandler : public Handler {
    explicit EvbHandler(folly::EventBase* e);
    ~EvbHandler() override;
    void handle(MuxIOThreadPoolExecutor* parent) override;

    folly::EventBase* evb{nullptr};
    folly::EventBase* getEventBase() override { return evb; }
  };

  struct EventFdHandler : public Handler {
    EventFdHandler();
    ~EventFdHandler() override;

    void notifyFd();
    void drainFd();

    void handle(MuxIOThreadPoolExecutor* parent) override;
  };

  struct HandlerTask {
    explicit HandlerTask(Handler* h) : handler(h) {}
    Handler* handler{nullptr};
  };

  void addHandler(Handler* handler, bool first, bool persist);
  void enqueueHandler(Handler* handler);
  void wakeup(size_t num);

  void notifyFd() { returnEvfd_.notifyFd(); }
  void drainFd() { returnEvfd_.drainFd(); }

  void mainThreadFunc();

  void handleDequeue();

  void maybeUnregisterEventBases(Observer* o);

  ThreadPtr makeThread() override;
  folly::EventBase* pickEVB();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  std::unique_ptr<folly::EventBase> makeEventBase();

  Options options_;
  relaxed_atomic<size_t> nextEVB_;
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  folly::EventBaseManager* eventBaseManager_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;

  int epFd_{-1};
  std::vector<std::unique_ptr<folly::EventBase>> evbs_;
  std::vector<std::unique_ptr<EvbHandler>> handlers_;
  std::unique_ptr<std::thread> mainThread_;
  std::atomic<bool> stop_{false};
  std::atomic<size_t> pendingTasks_{0};
  std::unique_ptr<
      folly::UnboundedBlockingQueue<HandlerTask, folly::ThrottledLifoSem>>
      queue_;
  Stats stats_;

  // queueing back
  using AtomicList = folly::AtomicIntrusiveLinkedList<Handler, &Handler::hook_>;
  AtomicList returnList_;
  EventFdHandler returnEvfd_;
};

} // namespace folly
#endif
