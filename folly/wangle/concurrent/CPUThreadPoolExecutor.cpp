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

#include <folly/wangle/concurrent/CPUThreadPoolExecutor.h>
#include <folly/wangle/concurrent/PriorityLifoSemMPMCQueue.h>

namespace folly { namespace wangle {

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 18;
const size_t CPUThreadPoolExecutor::kDefaultNumPriorities = 2;

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory)
    : ThreadPoolExecutor(numThreads, std::move(threadFactory)),
      taskQueue_(std::move(taskQueue)) {
  addThreads(numThreads);
  CHECK(threadList_.get().size() == numThreads);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          folly::make_unique<LifoSemMPMCQueue<CPUTask>>(
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t numThreads)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_shared<NamedThreadFactory>("CPUThreadPool")) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    uint32_t numPriorities,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          folly::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities,
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  stop();
  CHECK(threadsToStop_ == 0);
}

void CPUThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  // TODO handle enqueue failure, here and in other add() callsites
  taskQueue_->add(
      CPUTask(std::move(func), expiration, std::move(expireCallback)));
}

void CPUThreadPoolExecutor::add(Func func, uint32_t priority) {
  add(std::move(func), priority, std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    uint32_t priority,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  CHECK(priority < getNumPriorities());
  taskQueue_->addWithPriority(
      CPUTask(std::move(func), expiration, std::move(expireCallback)),
      priority);
}

uint32_t CPUThreadPoolExecutor::getNumPriorities() const {
  return taskQueue_->getNumPriorities();
}

BlockingQueue<CPUThreadPoolExecutor::CPUTask>*
CPUThreadPoolExecutor::getTaskQueue() {
  return taskQueue_.get();
}

void CPUThreadPoolExecutor::threadRun(std::shared_ptr<Thread> thread) {
  thread->startupBaton.post();
  while (1) {
    auto task = taskQueue_->take();
    if (UNLIKELY(task.poison)) {
      CHECK(threadsToStop_-- > 0);
      for (auto& o : observers_) {
        o->threadStopped(thread.get());
      }

      stoppedThreads_.add(thread);
      return;
    } else {
      runTask(thread, std::move(task));
    }

    if (UNLIKELY(threadsToStop_ > 0 && !isJoin_)) {
      if (--threadsToStop_ >= 0) {
        stoppedThreads_.add(thread);
        return;
      } else {
        threadsToStop_++;
      }
    }
  }
}

void CPUThreadPoolExecutor::stopThreads(size_t n) {
  CHECK(stoppedThreads_.size() == 0);
  threadsToStop_ = n;
  for (size_t i = 0; i < n; i++) {
    taskQueue_->add(CPUTask());
  }
}

uint64_t CPUThreadPoolExecutor::getPendingTaskCount() {
  return taskQueue_->size();
}

}} // folly::wangle
