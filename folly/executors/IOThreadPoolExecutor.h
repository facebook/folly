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

#include <folly/Portability.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {

FOLLY_PUSH_WARNING
// Suppress "IOThreadPoolExecutor inherits DefaultKeepAliveExecutor
// keepAliveAcquire/keepAliveRelease via dominance"
FOLLY_MSVC_DISABLE_WARNING(4250)

class IOThreadPoolExecutorBase : public ThreadPoolExecutor,
                                 public IOExecutor,
                                 public GetThreadIdCollector {
 public:
  using ThreadPoolExecutor::ThreadPoolExecutor;

  ~IOThreadPoolExecutorBase() override = default;

  folly::EventBase* getEventBase() override = 0;

  virtual std::vector<folly::Executor::KeepAlive<folly::EventBase>>
  getAllEventBases() = 0;

  static folly::EventBase* getEventBase(ThreadPoolExecutor::ThreadHandle*);

 protected:
  struct alignas(Thread) IOThread : public Thread {
    explicit IOThread(IOThreadPoolExecutorBase* pool)
        : Thread(pool), shouldRun(true), pendingTasks(0) {}
    std::atomic<bool> shouldRun;
    std::atomic<size_t> pendingTasks;
    folly::EventBase* eventBase{nullptr};
    std::mutex eventBaseShutdownMutex_;
  };
};

/**
 * A Thread Pool for IO bound tasks
 *
 * @note Uses event_fd for notification, and waking an epoll loop.
 * There is one queue (NotificationQueue specifically) per thread/epoll.
 * If the thread is already running and not waiting on epoll,
 * we don't make any additional syscalls to wake up the loop,
 * just put the new task in the queue.
 * If any thread has been waiting for more than a few seconds,
 * its stack is madvised away. Currently however tasks are scheduled round
 * robin on the queues, so unless there is no work going on,
 * this isn't very effective.
 * Since there is one queue per thread, there is hardly any contention
 * on the queues - so a simple spinlock around an std::deque is used for
 * the tasks. There is no max queue size.
 * By default, there is one thread per core - it usually doesn't make sense to
 * have more IO threads than this, assuming they don't block.
 *
 * @note ::getEventBase() will return an EventBase you can schedule IO work on
 * directly, chosen round-robin.
 *
 * @note N.B. For this thread pool, stop() behaves like join() because
 * outstanding tasks belong to the event base and will be executed upon its
 * destruction.
 */
class IOThreadPoolExecutor : public IOThreadPoolExecutorBase {
 public:
  struct Options {
    Options() : waitForAll(false), enableThreadIdCollection(false) {}

    Options& setWaitForAll(bool b) {
      this->waitForAll = b;
      return *this;
    }
    Options& setEnableThreadIdCollection(bool b) {
      this->enableThreadIdCollection = b;
      return *this;
    }

    bool waitForAll;
    bool enableThreadIdCollection;
  };

  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get(),
      Options options = Options());

  IOThreadPoolExecutor(
      size_t maxThreads,
      size_t minThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      folly::EventBaseManager* ebm = folly::EventBaseManager::get(),
      Options options = Options());

  ~IOThreadPoolExecutor() override;

  void add(Func func) override;
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  folly::EventBase* getEventBase() override;

  // Ensures that the maximum number of active threads is running and returns
  // the EventBase associated with each thread.
  std::vector<folly::Executor::KeepAlive<folly::EventBase>> getAllEventBases()
      override;

  static folly::EventBase* getEventBase(ThreadPoolExecutor::ThreadHandle* h) {
    return IOThreadPoolExecutorBase::getEventBase(h);
  }

  folly::EventBaseManager* getEventBaseManager();

  // Returns nullptr unless explicitly enabled through constructor
  folly::WorkerProvider* getThreadIdCollector() override {
    return threadIdCollector_.get();
  }

 private:
  ThreadPtr makeThread() override;
  std::shared_ptr<IOThread> pickThread();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  const bool isWaitForAll_; // whether to wait till event base loop exits
  relaxed_atomic<size_t> nextThread_;
  folly::ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  folly::EventBaseManager* eventBaseManager_;
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_;
};

FOLLY_POP_WARNING

} // namespace folly
