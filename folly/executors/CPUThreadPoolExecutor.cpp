/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <folly/Memory.h>
#include <folly/Optional.h>
#include <folly/concurrency/QueueObserver.h>
#include <folly/executors/task_queue/PriorityLifoSemMPMCQueue.h>
#include <folly/executors/task_queue/PriorityUnboundedBlockingQueue.h>
#include <folly/executors/task_queue/UnboundedBlockingQueue.h>
#include <folly/portability/GFlags.h>

DEFINE_bool(
    dynamic_cputhreadpoolexecutor,
    true,
    "CPUThreadPoolExecutor will dynamically create and destroy threads");

namespace folly {

namespace {
//  queue_alloc custom allocator is necessary until C++17
//    http://open-std.org/JTC1/SC22/WG21/docs/papers/2012/n3396.htm
//    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=65122
//    https://bugs.llvm.org/show_bug.cgi?id=22634
using default_queue = UnboundedBlockingQueue<CPUThreadPoolExecutor::CPUTask>;
using default_queue_alloc =
    AlignedSysAllocator<default_queue, FixedAlign<alignof(default_queue)>>;
} // namespace

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 14;

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : CPUThreadPoolExecutor(
          std::make_pair(
              numThreads, FLAGS_dynamic_cputhreadpoolexecutor ? 0 : numThreads),
          std::move(taskQueue),
          std::move(threadFactory),
          std::move(opt)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    std::pair<size_t, size_t> numThreads,
    std::unique_ptr<BlockingQueue<CPUTask>> taskQueue,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : ThreadPoolExecutor(
          numThreads.first, numThreads.second, std::move(threadFactory)),
      taskQueue_(taskQueue.release()),
      prohibitBlockingOnThreadPools_{opt.blocking} {
  setNumThreads(numThreads.first);
  if (numThreads.second == 0) {
    minThreads_.store(1, std::memory_order_relaxed);
  }
  registerThreadPoolExecutor(this);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : CPUThreadPoolExecutor(
          std::make_pair(
              numThreads, FLAGS_dynamic_cputhreadpoolexecutor ? 0 : numThreads),
          std::move(threadFactory),
          std::move(opt)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    std::pair<size_t, size_t> numThreads,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : ThreadPoolExecutor(
          numThreads.first, numThreads.second, std::move(threadFactory)),
      taskQueue_(std::allocate_shared<default_queue>(default_queue_alloc{})),
      prohibitBlockingOnThreadPools_{opt.blocking} {
  setNumThreads(numThreads.first);
  if (numThreads.second == 0) {
    minThreads_.store(1, std::memory_order_relaxed);
  }
  registerThreadPoolExecutor(this);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t numThreads, Options opt)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_shared<NamedThreadFactory>("CPUThreadPool"),
          std::move(opt)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_unique<PriorityUnboundedBlockingQueue<CPUTask>>(
              numPriorities),
          std::move(threadFactory),
          std::move(opt)) {}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    int8_t numPriorities,
    size_t maxQueueSize,
    std::shared_ptr<ThreadFactory> threadFactory,
    Options opt)
    : CPUThreadPoolExecutor(
          numThreads,
          std::make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
              numPriorities, maxQueueSize),
          std::move(threadFactory),
          std::move(opt)) {}

CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  stop();
  CHECK(threadsToStop_ == 0);
  if (getNumPriorities() == 1) {
    delete queueObservers_[0];
  } else {
    for (auto& observer : queueObservers_) {
      delete observer.load(std::memory_order_relaxed);
    }
  }
}

QueueObserver* FOLLY_NULLABLE
CPUThreadPoolExecutor::getQueueObserver(int8_t pri) {
  if (!queueObserverFactory_) {
    return nullptr;
  }

  auto& slot = queueObservers_[folly::to_unsigned(pri)];
  if (auto observer = slot.load(std::memory_order_acquire)) {
    return observer;
  }

  // common case is only one queue, need only one observer
  if (getNumPriorities() == 1 && pri != 0) {
    auto sharedObserver = getQueueObserver(0);
    slot.store(sharedObserver, std::memory_order_release);
    return sharedObserver;
  }
  QueueObserver* existingObserver = nullptr;
  auto newObserver = queueObserverFactory_->create(pri);
  if (!slot.compare_exchange_strong(existingObserver, newObserver.get())) {
    return existingObserver;
  } else {
    return newObserver.release();
  }
}

void CPUThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func, std::chrono::milliseconds expiration, Func expireCallback) {
  addImpl<false>(std::move(func), 0, expiration, std::move(expireCallback));
}

void CPUThreadPoolExecutor::addWithPriority(Func func, int8_t priority) {
  add(std::move(func), priority, std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::add(
    Func func,
    int8_t priority,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  addImpl<true>(
      std::move(func), priority, expiration, std::move(expireCallback));
}

template <bool withPriority>
void CPUThreadPoolExecutor::addImpl(
    Func func,
    int8_t priority,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  if (withPriority) {
    CHECK(getNumPriorities() > 0);
  }
  CPUTask task(
      std::move(func), expiration, std::move(expireCallback), priority);
  if (auto queueObserver = getQueueObserver(priority)) {
    task.queueObserverPayload() =
        queueObserver->onEnqueued(task.context_.get());
  }

  // It's not safe to expect that the executor is alive after a task is added to
  // the queue (this task could be holding the last KeepAlive and when finished
  // - it may unblock the executor shutdown).
  // If we need executor to be alive after adding into the queue, we have to
  // acquire a KeepAlive.
  bool mayNeedToAddThreads = minThreads_.load(std::memory_order_relaxed) == 0 ||
      activeThreads_.load(std::memory_order_relaxed) <
          maxThreads_.load(std::memory_order_relaxed);
  folly::Executor::KeepAlive<> ka = mayNeedToAddThreads
      ? getKeepAliveToken(this)
      : folly::Executor::KeepAlive<>{};

  auto result = withPriority
      ? taskQueue_->addWithPriority(std::move(task), priority)
      : taskQueue_->add(std::move(task));

  if (mayNeedToAddThreads && !result.reusedThread) {
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

// threadListLock_ must be writelocked.
bool CPUThreadPoolExecutor::tryDecrToStop() {
  auto toStop = threadsToStop_.load(std::memory_order_relaxed);
  if (toStop <= 0) {
    return false;
  }
  threadsToStop_.store(toStop - 1, std::memory_order_relaxed);
  return true;
}

bool CPUThreadPoolExecutor::taskShouldStop(folly::Optional<CPUTask>& task) {
  if (tryDecrToStop()) {
    return true;
  }
  if (task) {
    return false;
  } else {
    return tryTimeoutThread();
  }
  return true;
}

void CPUThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();
  folly::Optional<ExecutorBlockingGuard> guard; // optional until C++17
  if (prohibitBlockingOnThreadPools_ == Options::Blocking::prohibit) {
    guard.emplace(ExecutorBlockingGuard::ProhibitTag{}, this, namePrefix_);
  } else {
    guard.emplace(ExecutorBlockingGuard::TrackTag{}, this, namePrefix_);
  }

  thread->startupBaton.post();
  while (true) {
    auto task = taskQueue_->try_take_for(threadTimeout_);

    // Handle thread stopping, either by task timeout, or
    // by 'poison' task added in join() or stop().
    if (UNLIKELY(!task || task.value().poison)) {
      // Actually remove the thread from the list.
      SharedMutex::WriteHolder w{&threadListLock_};
      if (taskShouldStop(task)) {
        for (auto& o : observers_) {
          o->threadStopped(thread.get());
        }
        threadList_.remove(thread);
        stoppedThreads_.add(thread);
        return;
      } else {
        continue;
      }
    }

    if (auto queueObserver = getQueueObserver(task->queuePriority())) {
      queueObserver->onDequeued(task->queueObserverPayload());
    }
    runTask(thread, std::move(task.value()));

    if (UNLIKELY(threadsToStop_ > 0 && !isJoin_)) {
      SharedMutex::WriteHolder w{&threadListLock_};
      if (tryDecrToStop()) {
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
size_t CPUThreadPoolExecutor::getPendingTaskCountImpl() const {
  return taskQueue_->size();
}

std::unique_ptr<folly::QueueObserverFactory>
CPUThreadPoolExecutor::createQueueObserverFactory() {
  for (auto& observer : queueObservers_) {
    observer.store(nullptr, std::memory_order_release);
  }
  return QueueObserverFactory::make(
      "cpu." + getName(), taskQueue_->getNumPriorities());
}

} // namespace folly
