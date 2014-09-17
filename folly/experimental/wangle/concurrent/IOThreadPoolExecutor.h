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
#include <folly/io/async/EventBase.h>

namespace folly { namespace wangle {

class IOThreadPoolExecutor : public ThreadPoolExecutor {
 public:
  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::unique_ptr<ThreadFactory> threadFactory =
          folly::make_unique<NamedThreadFactory>("IOThreadPool"));

  ~IOThreadPoolExecutor();

  void add(Func func) override;

 private:
  ThreadPtr makeThread() override;
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;

  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING IOThread : public Thread {
    IOThread() : shouldRun(true), outstandingTasks(0) {};
    std::atomic<bool> shouldRun;
    std::atomic<size_t> outstandingTasks;
    EventBase eventBase;
  };

  size_t nextThread_;
};

}} // folly::wangle
