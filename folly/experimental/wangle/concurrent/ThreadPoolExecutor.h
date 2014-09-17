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
#include <folly/Memory.h>
#include <folly/RWSpinLock.h>

#include <algorithm>
#include <mutex>
#include <queue>

#include <glog/logging.h>

namespace folly { namespace wangle {

class ThreadPoolExecutor : public Executor {
 public:
  explicit ThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<ThreadFactory> threadFactory);

  ~ThreadPoolExecutor();

  size_t numThreads();
  void setNumThreads(size_t numThreads);
  void stop();
  void join();
  // TODO expose stats

 protected:
  void addThreads(size_t n);
  void removeThreads(size_t n, bool isJoin);

  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING Thread {
    virtual ~Thread() {}
    Thread() : id(nextId++), handle(), idle(true) {};
    static std::atomic<uint64_t> nextId;
    uint64_t id;
    std::thread handle;
    bool idle;
    // TODO per-thread stats go here
  };

  typedef std::shared_ptr<Thread> ThreadPtr;

  // The function that will be bound to pool threads
  virtual void threadRun(ThreadPtr thread) = 0;
  // Stop n threads and put their Thread structs in the threadsStopped_ queue
  virtual void stopThreads(size_t n) = 0;
  // Create a suitable Thread struct
  virtual ThreadPtr makeThread() {
    return std::make_shared<Thread>();
  }
  // need a stopThread(id) for keepalive feature

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
};

}} // folly::wangle
