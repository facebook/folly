/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/task_queue/PriorityLifoSemMPMCQueue.h>

DEFINE_bool(
    dynamic_cputhreadpoolexecutor,
    true,
    "CPUThreadPoolExecutor will dynamically create and destroy threads");

namespace folly {

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 14;

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory)
    : ThreadPoolExecutor(
          numThreads,
          FLAGS_dynamic_cputhreadpoolexecutor ? 0 : numThreads,
          std::move(threadFactory)),
      taskQueue_(std::move(taskQueue)) {
  setNumThreads(numThreads);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    std::pair<size_t, size_t> numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory)
    : ThreadPoolExecutor(
          numThreads.first,
          numThreads.second,
          std::move(threadFactory)),
      taskQueue_(std::move(taskQueue)) {
  setNumThreads(numThreads.first);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_unique<LifoSemMPMCQueue<CPUTask>>(
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t numThreads)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_shared<NamedThreadFactory>("CPUThreadPool")) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities,
              CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    size_t maxQueueSize,
    std::shared_ptr<ThreadFactory> threadFactory)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities,
              maxQueueSize),
          std::move(threadFactory)) {}

CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  joinKeepAlive();
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
  if (!taskQueue_->add(
          CPUTask(std::move(func), expiration, std::move(expireCallback)))) {
    ensureActiveThreads();
  }
}

void CPUThreadPoolExecutor::addWithPriority(Func func, int8_t priority) {
  add(std::move(func), priority, std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    int8_t priority,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  CHECK(getNumPriorities() > 0);
  if (!taskQueue_->addWithPriority(
          CPUTask(std::move(func), expiration, std::move(expireCallback)),
          priority)) {
    ensureActiveThreads();
  }
}

uint8_t CPUThreadPoolExecutor::getNumPriorities() const {
  return taskQueue_->getNumPriorities();
}

size_t CPUThreadPoolExecutor::getTaskQueueSize() const {
  return taskQueue_->size();
}

BlockingQueue<CPUThreadPoolExecutor::CPUTask>*
CPUThreadPoolExecutor::getTaskQueue() {
  return taskQueue_.get();
}

bool CPUThreadPoolExecutor::tryDecrToStop() {
  while (true) {
    auto toStop = threadsToStop_.load(std::memory_order_relaxed);
    if (toStop <= 0) {
      return false;
    }
    if (threadsToStop_.compare_exchange_strong(
            toStop, toStop - 1, std::memory_order_relaxed)) {
      return true;
    }
  }
}

bool CPUThreadPoolExecutor::taskShouldStop(folly::Optional<CPUTask>& task) {
  if (task) {
    if (!tryDecrToStop()) {
      // Some other thread beat us to it.
      return false;
    }
  } else {
    // Try to stop based on idle thread timeout (try_take_for),
    // if there are at least minThreads running.
    if (!minActive()) {
      return false;
    }
    // If this is based on idle thread timeout, then
    // adjust vars appropriately (otherwise stop() or join()
    // does this).
    activeThreads_.fetch_sub(1, std::memory_order_relaxed);
    threadsToJoin_.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}

void CPUThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();

  thread->startupBaton.post();
  while (true) {
    auto task = taskQueue_->try_take_for(threadTimeout_);
    // Handle thread stopping, either by task timeout, or
    // by 'poison' task added in join() or stop().
    if (UNLIKELY(!task || task.value().poison)) {
      if (taskShouldStop(task)) {
        for (auto& o : observers_) {
          o->threadStopped(thread.get());
        }
        // Actually remove the thread from the list.
        folly::RWSpinLock::WriteHolder w{&threadListLock_};
        threadList_.remove(thread);
        stoppedThreads_.add(thread);
        return;
      } else {
        continue;
      }
    }

    runTask(thread, std::move(task.value()));

    if (UNLIKELY(threadsToStop_ > 0 && !isJoin_)) {
      if (tryDecrToStop()) {
        folly::RWSpinLock::WriteHolder w{&threadListLock_};
        threadList_.remove(thread);
        stoppedThreads_.add(thread);
        return;
      }
    }
  }
}

void CPUThreadPoolExecutor::stopThreads(size_t n) {
  threadsToStop_ += n;
  for (size_t i = 0; i < n; i++) {
    taskQueue_->addWithPriority(CPUTask(), Executor::LO_PRI);
  }
}

// threadListLock_ is read (or write) locked.
size_t CPUThreadPoolExecutor::getPendingTaskCountImpl() {
  return taskQueue_->size();
}

} // namespace folly
