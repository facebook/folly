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

#include <memory>
#include <folly/concurrency/DeadlockDetector.h>
#include <folly/executors/IOThreadPoolDeadlockDetectorObserver.h>
#include <folly/system/ThreadId.h>

namespace folly {

namespace {
class ThreadIdCollector : public WorkerProvider {
 public:
  ThreadIdCollector(pid_t tid, std::mutex& eventBaseShutdownMutex)
      : tid_(tid), eventBaseShutdownMutex_(eventBaseShutdownMutex) {}

  IdsWithKeepAlive collectThreadIds() override final {
    std::unique_lock<std::mutex> guard(eventBaseShutdownMutex_);
    return {
        std::make_unique<WorkerKeepAlive>(std::move(guard)),
        std::vector<pid_t>{tid_}};
  }

 private:
  class WorkerKeepAlive : public WorkerProvider::KeepAlive {
   public:
    explicit WorkerKeepAlive(std::unique_lock<std::mutex> lock)
        : lock_(std::move(lock)) {}
    ~WorkerKeepAlive() override {}

   private:
    std::unique_lock<std::mutex> lock_;
  };

  pid_t tid_;
  std::mutex& eventBaseShutdownMutex_;
};
} // namespace

IOThreadPoolDeadlockDetectorObserver::IOThreadPoolDeadlockDetectorObserver(
    DeadlockDetectorFactory* deadlockDetectorFactory, const std::string& name)
    : name_(name), deadlockDetectorFactory_(deadlockDetectorFactory) {}

void IOThreadPoolDeadlockDetectorObserver::threadStarted(
    folly::ThreadPoolExecutor::ThreadHandle* h) {
  if (!deadlockDetectorFactory_) {
    return;
  }

  auto eventBase = folly::IOThreadPoolExecutor::getEventBase(h);
  auto eventBaseShutdownMutex =
      folly::IOThreadPoolExecutor::getEventBaseShutdownMutex(h);
  // This Observer only works with IOThreadPoolExecutor class.
  CHECK_NOTNULL(eventBase);
  CHECK_NOTNULL(eventBaseShutdownMutex);
  eventBase->runInEventBaseThread([=] {
    auto tid = folly::getOSThreadID();
    auto name = name_ + ":" + folly::to<std::string>(tid);
    auto deadlockDetector = deadlockDetectorFactory_->create(
        eventBase,
        name,
        std::make_unique<ThreadIdCollector>(tid, *eventBaseShutdownMutex));
    detectors_.wlock()->insert_or_assign(h, std::move(deadlockDetector));
  });
}

void IOThreadPoolDeadlockDetectorObserver::threadStopped(
    folly::ThreadPoolExecutor::ThreadHandle* h) {
  if (!deadlockDetectorFactory_) {
    return;
  }

  detectors_.wlock()->erase(h);
}

/* static */ std::unique_ptr<IOThreadPoolDeadlockDetectorObserver>
IOThreadPoolDeadlockDetectorObserver::create(const std::string& name) {
  auto* deadlockDetectorFactory = DeadlockDetectorFactory::instance();
  return std::make_unique<IOThreadPoolDeadlockDetectorObserver>(
      deadlockDetectorFactory, name);
}

} // namespace folly
