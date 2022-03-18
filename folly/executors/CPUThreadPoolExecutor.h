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

#include <limits.h>

#include <array>

#include <folly/executors/QueueObserver.h>
#include <folly/executors/ThreadPoolExecutor.h>

DECLARE_bool(dynamic_cputhreadpoolexecutor);

namespace folly {

/**
 * A Thread pool for CPU bound tasks.
 *
 * @note A single queue backed by folly/LifoSem and folly/MPMC queue.
 * Because of this contention can be quite high,
 * since all the worker threads and all the producer threads hit
 * the same queue. MPMC queue excels in this situation but dictates a max queue
 * size.
 *
 * @note The default queue throws when full (folly::QueueBehaviorIfFull::THROW),
 * so add() can fail. Furthermore, join() can also fail if the queue is full,
 * because it enqueues numThreads poison tasks to stop the threads. If join() is
 * needed to be guaranteed to succeed PriorityLifoSemMPMCQueue can be used
 * instead, initializing the lowest priority's (LO_PRI) capacity to at least
 * numThreads. Poisons use LO_PRI so if that priority is not used for any user
 * task join() is guaranteed not to encounter a full queue.
 *
 * @note If a blocking queue (folly::QueueBehaviorIfFull::BLOCK) is used, and
 * tasks executing on a given thread pool schedule more tasks, deadlock is
 * possible if the queue becomes full.  Deadlock is also possible if there is
 * a circular dependency among multiple thread pools with blocking queues.
 * To avoid this situation, use non-blocking queue(s), or schedule tasks only
 * from threads not belonging to the given thread pool(s), or use
 * folly::IOThreadPoolExecutor.
 *
 * @note LifoSem wakes up threads in Lifo order - i.e. there are only few
 * threads as necessary running, and we always try to reuse the same few threads
 * for better cache locality.
 * Inactive threads have their stack madvised away. This works quite well in
 * combination with Lifosem - it almost doesn't matter if more threads than are
 * necessary are specified at startup.
 *
 * @note Supports priorities - priorities are implemented as multiple queues -
 * each worker thread checks the highest priority queue first. Threads
 * themselves don't have priorities set, so a series of long running low
 * priority tasks could still hog all the threads. (at last check pthreads
 * thread priorities didn't work very well).
 */
class CPUThreadPoolExecutor : public ThreadPoolExecutor {
 public:
  struct CPUTask;
  struct Options {
    enum class Blocking {
      prohibit,
      allow,
    };

    constexpr Options() noexcept : blocking{Blocking::allow} {}

    Options setBlocking(Blocking b) {
      blocking = b;
      return *this;
    }

    Blocking blocking;
  };

  CPUThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
      Options opt = {});

  CPUThreadPoolExecutor(
      std::pair<size_t, size_t> numThreads,
      std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
      Options opt = {});

  explicit CPUThreadPoolExecutor(size_t numThreads, Options opt = {});

  CPUThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory,
      Options opt = {});

  explicit CPUThreadPoolExecutor(
      std::pair<size_t, size_t> numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
      Options opt = {});

  CPUThreadPoolExecutor(
      size_t numThreads,
      int8_t numPriorities,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
      Options opt = {});

  CPUThreadPoolExecutor(
      size_t numThreads,
      int8_t numPriorities,
      size_t maxQueueSize,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
      Options opt = {});

  ~CPUThreadPoolExecutor() override;

  void add(Func func) override;
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  void addWithPriority(Func func, int8_t priority) override;
  virtual void add(
      Func func,
      int8_t priority,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr);

  size_t getTaskQueueSize() const;

  uint8_t getNumPriorities() const override;

  struct CPUTask : public ThreadPoolExecutor::Task {
    // Must be noexcept move constructible so it can be used in MPMCQueue

    explicit CPUTask(
        Func&& f,
        std::chrono::milliseconds expiration,
        Func&& expireCallback,
        int8_t pri)
        : Task(std::move(f), expiration, std::move(expireCallback)),
          poison(false),
          priority_(pri) {}
    CPUTask()
        : Task(nullptr, std::chrono::milliseconds(0), nullptr),
          poison(true),
          priority_(0) {}

    size_t queuePriority() const { return priority_; }

    intptr_t& queueObserverPayload() { return queueObserverPayload_; }

    bool poison;

   private:
    int8_t priority_;
    intptr_t queueObserverPayload_;
  };

  static const size_t kDefaultMaxQueueSize;

 protected:
  BlockingQueue<CPUTask>* getTaskQueue();
  std::unique_ptr<ThreadIdWorkerProvider> threadIdCollector_{
      std::make_unique<ThreadIdWorkerProvider>()};

 private:
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  size_t getPendingTaskCountImpl() const override final;

  bool tryDecrToStop();
  bool taskShouldStop(folly::Optional<CPUTask>&);

  template <bool withPriority>
  void addImpl(
      Func func,
      int8_t priority,
      std::chrono::milliseconds expiration,
      Func expireCallback);

  std::unique_ptr<folly::QueueObserverFactory> createQueueObserverFactory();
  QueueObserver* FOLLY_NULLABLE getQueueObserver(int8_t pri);

  // shared_ptr for type erased dtor to handle extended alignment.
  std::shared_ptr<BlockingQueue<CPUTask>> taskQueue_;
  // It is possible to have as many detectors as there are priorities,
  std::array<std::atomic<folly::QueueObserver*>, UCHAR_MAX + 1> queueObservers_;
  std::unique_ptr<folly::QueueObserverFactory> queueObserverFactory_{
      createQueueObserverFactory()};
  std::atomic<ssize_t> threadsToStop_{0};
  Options::Blocking prohibitBlockingOnThreadPools_ = Options::Blocking::allow;
};

} // namespace folly
