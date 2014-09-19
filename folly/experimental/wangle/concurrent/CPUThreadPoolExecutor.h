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
  struct Task;

  // TODO thread naming, perhaps a required input to ThreadFactories
  explicit CPUThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<BlockingQueue<Task>> taskQueue =
          folly::make_unique<LifoSemMPMCQueue<Task>>(
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
      std::unique_ptr<ThreadFactory> threadFactory =
          folly::make_unique<NamedThreadFactory>("CPUThreadPool"));

  ~CPUThreadPoolExecutor();

  void add(Func func) override;

  struct Task {
    explicit Task(Func&& taskArg) : func(std::move(taskArg)), poison(false) {}
    Task() : func(nullptr), poison(true) {}
    Task(Task&& o) noexcept : func(std::move(o.func)), poison(o.poison) {}
    Task(const Task&) = default;
    Task& operator=(const Task&) = default;
    Func func;
    bool poison;
    // TODO per-task stats, timeouts, expirations
  };

  static const size_t kDefaultMaxQueueSize;

 private:
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;

  std::unique_ptr<BlockingQueue<Task>> taskQueue_;
  std::atomic<ssize_t> threadsToStop_{0};
};

}} // folly::wangle
