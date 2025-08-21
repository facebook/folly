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

#include <folly/executors/EDFThreadPoolExecutor.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include <folly/ScopeGuard.h>
#include <folly/concurrency/ProcessLocalUniqueId.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/LifoSem.h>
#include <folly/synchronization/ThrottledLifoSem.h>
#include <folly/tracing/StaticTracepoint.h>

FOLLY_GFLAGS_DEFINE_bool(
    folly_edfthreadpoolexecutor_use_throttled_lifo_sem,
    true,
    "EDFThreadPoolExecutor will use ThrottledLifoSem by default");

namespace folly {

class EDFThreadPoolExecutor::Task {
 public:
  struct Poison {};

  explicit Task(Func&& f, uint64_t deadline)
      : f_(std::move(f)), total_(1), deadline_(deadline) {}

  explicit Task(std::vector<Func>&& fs, uint64_t deadline)
      : fs_(std::move(fs)), total_(fs_.size()), deadline_(deadline) {}

  explicit Task(Poison, int total) : total_(total), deadline_(kLatestDeadline) {
    CHECK_GT(total, 0);
  }

  uint64_t getDeadline() const { return deadline_; }
  uint64_t getEnqueueOrder() const { return enqueueOrder_; }

  bool isPoison() const { return !f_ && fs_.empty() && total_ > 0; }

  bool isDone() const {
    return iter_.load(std::memory_order_relaxed) >= total_;
  }

  // Cannot be set in the ctor because known only after acquiring the lock.
  void setEnqueueOrder(uint64_t enqueueOrder) { enqueueOrder_ = enqueueOrder; }

  int next() {
    if (isDone()) {
      return -1;
    }

    int result = iter_.fetch_add(1, std::memory_order_relaxed);
    return result < total_ ? result : -1;
  }

  void run(int i) {
    folly::RequestContextScopeGuard guard(context_);
    if (f_) {
      f_();
      if (i >= total_ - 1) {
        std::exchange(f_, nullptr);
      }
    } else {
      DCHECK(0 <= i && i < total_);
      fs_[i]();
      std::exchange(fs_[i], nullptr);
    }
  }

  Func f_;
  std::vector<Func> fs_;
  std::atomic<int> iter_{0};
  int total_;
  uint64_t deadline_;
  uint64_t enqueueOrder_ = 0;
  std::shared_ptr<RequestContext> context_ = RequestContext::saveContext();
  std::chrono::steady_clock::time_point enqueueTime_ =
      std::chrono::steady_clock::now();
  uint64_t taskId_ = processLocalUniqueId();
};

class EDFThreadPoolExecutor::TaskQueue {
 public:
  using TaskPtr = std::shared_ptr<Task>;

  // This is not a `Synchronized` because we perform a few "peek" operations.
  struct Bucket {
    mutable SharedMutex mutex;

    struct Compare {
      bool operator()(const TaskPtr& lhs, const TaskPtr& rhs) const {
        if (lhs->getDeadline() != rhs->getDeadline()) {
          return lhs->getDeadline() > rhs->getDeadline();
        }
        return lhs->getEnqueueOrder() > rhs->getEnqueueOrder();
      }
    };

    std::priority_queue<TaskPtr, std::vector<TaskPtr>, Compare> tasks;
    std::atomic<bool> empty{true};
    uint64_t enqueued = 0;
  };

  static constexpr std::size_t kNumBuckets = 2 << 5;

  void push(TaskPtr task) {
    auto deadline = task->getDeadline();
    auto& bucket = getBucket(deadline);
    {
      std::unique_lock guard(bucket.mutex);
      task->setEnqueueOrder(bucket.enqueued++);
      bucket.tasks.push(std::move(task));
      bucket.empty.store(bucket.tasks.empty(), std::memory_order_relaxed);
    }

    // Update current earliest deadline if necessary
    uint64_t curDeadline = curDeadline_.load(std::memory_order_relaxed);
    do {
      if (curDeadline <= deadline) {
        break;
      }
    } while (!curDeadline_.compare_exchange_weak(
        curDeadline, deadline, std::memory_order_relaxed));
  }

  // Should only be called on a nonempty queue.
  TaskPtr pop() {
    bool needDeadlineUpdate = false;
    for (;;) {
      auto curDeadline = curDeadline_.load(std::memory_order_relaxed);
      auto& bucket = getBucket(curDeadline);

      if (needDeadlineUpdate || bucket.empty.load(std::memory_order_relaxed)) {
        // Try setting the next earliest deadline. However no need to
        // enforce as there might be insertion happening.
        // If there is no next deadline, we set deadline to `kLatestDeadline`.
        curDeadline_.compare_exchange_weak(
            curDeadline,
            findNextDeadline(curDeadline),
            std::memory_order_relaxed);
        needDeadlineUpdate = false;
        continue;
      }

      {
        // Fast path. Take bucket reader lock.
        std::shared_lock guard(bucket.mutex);
        if (bucket.tasks.empty()) {
          continue;
        }
        const auto& task = bucket.tasks.top();
        if (!task->isDone() && task->getDeadline() == curDeadline) {
          return task;
        }
        // If the task is finished already, fall through to remove it.
      }

      {
        // Take the writer lock to clean up the finished task.
        std::unique_lock guard(bucket.mutex);
        if (bucket.tasks.empty()) {
          continue;
        }
        const auto& task = bucket.tasks.top();
        if (task->isDone()) {
          // Current task finished. Remove from the queue.
          bucket.tasks.pop();
          bucket.empty.store(bucket.tasks.empty(), std::memory_order_relaxed);
        }
      }

      // We may have finished processing the current task / bucket. Going back
      // to the beginning of the loop to find the next bucket.
      needDeadlineUpdate = true;
    }
  }

 private:
  Bucket& getBucket(uint64_t deadline) {
    return buckets_[deadline % kNumBuckets];
  }

  uint64_t findNextDeadline(uint64_t prevDeadline) {
    auto begin = prevDeadline % kNumBuckets;

    uint64_t earliestDeadline = kLatestDeadline;
    for (std::size_t i = 0; i < kNumBuckets; ++i) {
      auto& bucket = buckets_[(begin + i) % kNumBuckets];

      // Peek without locking first.
      if (bucket.empty.load(std::memory_order_relaxed)) {
        continue;
      }

      std::shared_lock guard(bucket.mutex);
      auto curDeadline = curDeadline_.load(std::memory_order_relaxed);
      if (prevDeadline != curDeadline) {
        // Bail out early if something already happened
        return curDeadline;
      }

      // Verify again after locking
      if (bucket.tasks.empty()) {
        continue;
      }

      const auto& task = bucket.tasks.top();
      auto deadline = task->getDeadline();

      if (deadline < earliestDeadline) {
        earliestDeadline = deadline;
      }

      if ((deadline <= prevDeadline) ||
          (deadline - prevDeadline < kNumBuckets)) {
        // Found the next highest priority, or new tasks were added.
        // No need to scan anymore.
        break;
      }
    }

    return earliestDeadline;
  }

  std::array<Bucket, kNumBuckets> buckets_;
  std::atomic<uint64_t> curDeadline_ = kLatestDeadline;
};

/* static */ std::unique_ptr<EDFThreadPoolSemaphore>
EDFThreadPoolExecutor::makeDefaultSemaphore() {
  return FLAGS_folly_edfthreadpoolexecutor_use_throttled_lifo_sem
      ? makeThrottledLifoSemSemaphore()
      : makeLifoSemSemaphore();
}

/* static */ std::unique_ptr<EDFThreadPoolSemaphore>
EDFThreadPoolExecutor::makeLifoSemSemaphore() {
  return std::make_unique<EDFThreadPoolSemaphoreImpl<LifoSem>>();
}

/* static */ std::unique_ptr<EDFThreadPoolSemaphore>
EDFThreadPoolExecutor::makeThrottledLifoSemSemaphore(
    std::chrono::nanoseconds wakeUpInterval) {
  ThrottledLifoSem::Options opts;
  opts.wakeUpInterval = wakeUpInterval;
  return std::make_unique<EDFThreadPoolSemaphoreImpl<ThrottledLifoSem>>(opts);
}

EDFThreadPoolExecutor::EDFThreadPoolExecutor(
    std::size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory,
    std::unique_ptr<EDFThreadPoolSemaphore> semaphore)
    : ThreadPoolExecutor(numThreads, numThreads, std::move(threadFactory)),
      taskQueue_(std::make_unique<TaskQueue>()),
      sem_(std::move(semaphore)) {
  setNumThreads(numThreads);
  registerThreadPoolExecutor(this);
}

EDFThreadPoolExecutor::~EDFThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  stop();
}

void EDFThreadPoolExecutor::add(Func f) {
  add(std::move(f), kLatestDeadline);
}

void EDFThreadPoolExecutor::add(Func f, uint64_t deadline) {
  auto task = std::make_shared<Task>(std::move(f), deadline);
  registerTaskEnqueue(*task);
  taskQueue_->push(std::move(task));
  sem_->post(1);
}

void EDFThreadPoolExecutor::add(std::vector<Func> fs, uint64_t deadline) {
  if (FOLLY_UNLIKELY(fs.empty())) {
    return;
  }

  auto total = fs.size();
  auto task = std::make_shared<Task>(std::move(fs), deadline);
  registerTaskEnqueue(*task);
  taskQueue_->push(std::move(task));
  sem_->post(static_cast<uint32_t>(total));
}

size_t EDFThreadPoolExecutor::getTaskQueueSize() const {
  return sem_->valueGuess();
}

bool EDFThreadPoolExecutor::tryStopThread(
    const ThreadPtr& thread, bool isPoison) {
  auto threadsToStop = threadsToStop_.load(std::memory_order_relaxed);
  do {
    if (threadsToStop == 0 || (!isPoison && isJoin_)) {
      return false;
    }
  } while (!threadsToStop_.compare_exchange_weak(
      threadsToStop, threadsToStop - 1, std::memory_order_relaxed));

  std::unique_lock w{threadListLock_};
  for (auto& o : observers_) {
    o->threadStopped(thread.get());
  }
  threadList_.remove(thread);
  stoppedThreads_.add(thread);
  return true;
}

void EDFThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();
  ExecutorBlockingGuard guard{
      ExecutorBlockingGuard::TrackTag{}, this, getName()};

  thread->startupBaton.post();
  for (;;) {
    sem_->wait();

    // We consumed a post so the queue is non-empty, but we need to discard
    // finished tasks.
    int iter;
    std::shared_ptr<EDFThreadPoolExecutor::Task> task;
    do {
      task = taskQueue_->pop();
      iter = task->next();
    } while (iter < 0);

    if (task->isPoison()) {
      if (tryStopThread(thread, /* isPoison */ true)) {
        return;
      } else {
        continue; // Poison was consumed early by tryStopThread().
      }
    }

    thread->idle.store(false, std::memory_order_relaxed);
    auto startTime = std::chrono::steady_clock::now();
    ProcessedTaskInfo taskInfo;
    fillTaskInfo(*task, taskInfo);
    taskInfo.waitTime = startTime - taskInfo.enqueueTime;
    FOLLY_SDT(
        folly,
        thread_pool_executor_task_dequeued,
        threadFactory_->getNamePrefix().c_str(),
        taskInfo.requestId,
        taskInfo.enqueueTime.time_since_epoch().count(),
        taskInfo.waitTime.count(),
        taskInfo.taskId);
    forEachTaskObserver([&](auto& observer) {
      observer.taskDequeued(taskInfo);
    });

    invokeCatchingExns("EDFThreadPoolExecutor: func", [&] {
      std::exchange(task, {})->run(iter);
    });
    taskInfo.runTime = std::chrono::steady_clock::now() - startTime;

    FOLLY_SDT(
        folly,
        thread_pool_executor_task_taskInfo,
        threadFactory_->getNamePrefix().c_str(),
        taskInfo.requestId,
        taskInfo.enqueueTime.time_since_epoch().count(),
        taskInfo.waitTime.count(),
        taskInfo.runTime.count(),
        taskInfo.taskId);
    forEachTaskObserver([&](auto& observer) {
      observer.taskProcessed(taskInfo);
    });

    thread->idle.store(true, std::memory_order_relaxed);
    thread->lastActiveTime.store(
        std::chrono::steady_clock::now(), std::memory_order_relaxed);

    if (tryStopThread(thread, /* isPoison */ false)) {
      return;
    }
  }
}

// threadListLock_ is writelocked.
void EDFThreadPoolExecutor::stopThreads(std::size_t numThreads) {
  if (numThreads == 0) {
    return;
  }
  threadsToStop_.fetch_add(numThreads, std::memory_order_relaxed);
  taskQueue_->push(
      std::make_shared<Task>(Task::Poison{}, static_cast<int>(numThreads)));
  sem_->post(numThreads);
}

// threadListLock_ is read (or write) locked.
std::size_t EDFThreadPoolExecutor::getPendingTaskCountImpl() const {
  return getTaskQueueSize();
}

void EDFThreadPoolExecutor::fillTaskInfo(const Task& task, TaskInfo& info) {
  info.priority = 0; // Priorities are not supported.
  if (task.context_) {
    info.requestId = task.context_->getRootId();
  }
  info.enqueueTime = task.enqueueTime_;
  info.taskId = task.taskId_;
}

void EDFThreadPoolExecutor::registerTaskEnqueue(const Task& task) {
  TaskInfo info;
  fillTaskInfo(task, info);
  forEachTaskObserver([&](auto& observer) { observer.taskEnqueued(info); });
  FOLLY_SDT(
      folly,
      thread_pool_executor_task_enqueued,
      threadFactory_->getNamePrefix().c_str(),
      info.requestId,
      info.enqueueTime.time_since_epoch().count(),
      info.taskId);
}

} // namespace folly
