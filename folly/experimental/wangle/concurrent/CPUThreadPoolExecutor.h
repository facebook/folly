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
#include <folly/experimental/wangle/concurrent/ThreadPoolExecutor.h>

namespace folly { namespace wangle {

class CPUThreadPoolExecutor : public ThreadPoolExecutor {
 public:
  struct CPUTask;

  explicit CPUThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<BlockingQueue<CPUTask>> taskQueue =
          folly::make_unique<LifoSemMPMCQueue<CPUTask>>(
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
      std::unique_ptr<ThreadFactory> threadFactory =
          folly::make_unique<NamedThreadFactory>("CPUThreadPool"));

  ~CPUThreadPoolExecutor();

  void add(Func func) override;

  struct CPUTask : public ThreadPoolExecutor::Task {
    // Must be noexcept move constructible so it can be used in MPMCQueue
    explicit CPUTask(Func&& f) : Task(std::move(f)), poison(false) {}
    CPUTask() : Task(nullptr), poison(true) {}
    CPUTask(CPUTask&& o) noexcept : Task(std::move(o)), poison(o.poison) {}
    CPUTask(const CPUTask&) = default;
    CPUTask& operator=(const CPUTask&) = default;
    bool poison;
  };

  static const size_t kDefaultMaxQueueSize;

 private:
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  uint64_t getPendingTaskCount() override;

  std::unique_ptr<BlockingQueue<CPUTask>> taskQueue_;
  std::atomic<ssize_t> threadsToStop_{0};
};

}} // folly::wangle
