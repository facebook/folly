/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/io/async/EventBaseManager.h>
#include <folly/wangle/concurrent/IOExecutor.h>
#include <folly/wangle/concurrent/ThreadPoolExecutor.h>

namespace folly { namespace wangle {

// N.B. For this thread pool, stop() behaves like join() because outstanding
// tasks belong to the event base and will be executed upon its destruction.
class IOThreadPoolExecutor : public ThreadPoolExecutor, public IOExecutor {
 public:
  explicit IOThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("IOThreadPool"),
      EventBaseManager* ebm = folly::EventBaseManager::get());

  ~IOThreadPoolExecutor();

  void add(Func func) override;
  void add(
      Func func,
      std::chrono::milliseconds expiration,
      Func expireCallback = nullptr) override;

  EventBase* getEventBase() override;

  static EventBase* getEventBase(ThreadPoolExecutor::ThreadHandle*);

  EventBaseManager* getEventBaseManager();

 private:
  struct FOLLY_ALIGN_TO_AVOID_FALSE_SHARING IOThread : public Thread {
    IOThread(IOThreadPoolExecutor* pool)
      : Thread(pool),
        shouldRun(true),
        pendingTasks(0) {};
    std::atomic<bool> shouldRun;
    std::atomic<size_t> pendingTasks;
    EventBase* eventBase;
  };

  ThreadPtr makeThread() override;
  std::shared_ptr<IOThread> pickThread();
  void threadRun(ThreadPtr thread) override;
  void stopThreads(size_t n) override;
  uint64_t getPendingTaskCount() override;

  size_t nextThread_;
  ThreadLocal<std::shared_ptr<IOThread>> thisThread_;
  EventBaseManager* eventBaseManager_;
};

}} // folly::wangle
