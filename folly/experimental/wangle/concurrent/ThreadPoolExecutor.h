/*
 * Copyright 2014 Facebook, Inc.
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
#include <folly/experimental/wangle/concurrent/Executor.h>
#include <folly/experimental/wangle/concurrent/LifoSemMPMCQueue.h>
#include <folly/experimental/wangle/concurrent/NamedThreadFactory.h>
#include <folly/experimental/wangle/rx/Observable.h>
#include <folly/Baton.h>
#include <folly/Memory.h>
#include <folly/RWSpinLock.h>

#include <algorithm>
#include <mutex>
#include <queue>

#include <glog/logging.h>

namespace folly { namespace wangle {

class ThreadPoolExecutor : public experimental::Executor {
 public:
  explicit ThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<ThreadFactory> threadFactory);

  ~ThreadPoolExecutor();

  virtual void add(Func func) override = 0;
  virtual void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback) = 0;

  size_t numThreads();
  void setNumThreads(size_t numThreads);
  void stop();
  void join();

  struct PoolStats {
    PoolStats() : threadCount(0), idleThreadCount(0), activeThreadCount(0),
                  pendingTaskCount(0), totalTaskCount(0) {}
    size_t threadCount, idleThreadCount, activeThreadCount;
    uint64_t pendingTaskCount, totalTaskCount;
  };

  PoolStats getPoolStats();

  struct TaskStats {
    TaskStats() : expired(false), waitTime(0), runTime(0) {}
    bool expired;
    std::chrono::nanoseconds waitTime;
    std::chrono::nanoseconds runTime;
  };

  Subscription subscribeToTaskStats(
      const ObserverPtr<TaskStats>& observer) {
    return taskStatsSubject_.subscribe(observer);
  }

 protected:
  // Prerequisite: threadListLock_ writelocked
  void addThreads(size_t n);
  // Prerequisite: threadListLock_ writelocked
  void removeThreads(size_t n, bool isJoin);

  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING Thread {
    virtual ~Thread() {}
    Thread() : id(nextId++), handle(), idle(true) {};
    static std::atomic<uint64_t> nextId;
    uint64_t id;
    std::thread handle;
    bool idle;
    Baton<> startupBaton;
  };

  typedef std::shared_ptr<Thread> ThreadPtr;

  struct Task {
    explicit Task(
        Func&& func,
        std::chrono::milliseconds expiration,
        Func&& expireCallback);
    Func func_;
    TaskStats stats_;
    std::chrono::steady_clock::time_point enqueueTime_;
    std::chrono::milliseconds expiration_;
    Func expireCallback_;
  };

  void runTask(const ThreadPtr& thread, Task&& task);

  // The function that will be bound to pool threads. It must call
  // thread->startupBaton.post() when it's ready to consume work.
  virtual void threadRun(ThreadPtr thread) = 0;

  // Stop n threads and put their ThreadPtrs in the threadsStopped_ queue
  // Prerequisite: threadListLock_ writelocked
  virtual void stopThreads(size_t n) = 0;

  // Create a suitable Thread struct
  virtual ThreadPtr makeThread() {
    return std::make_shared<Thread>();
  }

  // Prerequisite: threadListLock_ readlocked
  virtual uint64_t getPendingTaskCount() = 0;

  class ThreadList {
   public:
    void add(const ThreadPtr& state) {
      auto it = std::lower_bound(vec_.begin(), vec_.end(), state, compare);
      vec_.insert(it, state);
    }

    void remove(const ThreadPtr& state) {
      auto itPair = std::equal_range(vec_.begin(), vec_.end(), state, compare);
      CHECK(itPair.first != vec_.end());
      CHECK(std::next(itPair.first) == itPair.second);
      vec_.erase(itPair.first);
    }

    const std::vector<ThreadPtr>& get() const {
      return vec_;
    }

   private:
    static bool compare(const ThreadPtr& ts1, const ThreadPtr& ts2) {
      return ts1->id < ts2->id;
    }

    std::vector<ThreadPtr> vec_;
  };

  class StoppedThreadQueue : public BlockingQueue<ThreadPtr> {
   public:
    void add(ThreadPtr item) override;
    ThreadPtr take() override;
    size_t size() override;

   private:
    LifoSem sem_;
    std::mutex mutex_;
    std::queue<ThreadPtr> queue_;
  };

  std::unique_ptr<ThreadFactory> threadFactory_;
  ThreadList threadList_;
  RWSpinLock threadListLock_;
  StoppedThreadQueue stoppedThreads_;
  std::atomic<bool> isJoin_; // whether the current downsizing is a join

  Subject<TaskStats> taskStatsSubject_;
};

}} // folly::wangle
