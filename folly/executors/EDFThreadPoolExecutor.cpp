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
#include <folly/synchronization/LifoSem.h>
#include <folly/synchronization/ThrottledLifoSem.h>

namespace folly {

class EDFThreadPoolExecutor::Task {
 public:
  explicit Task(Func&& f, int repeat, uint64_t deadline)
      : f_(std::move(f)), total_(repeat), deadline_(deadline) {}

  explicit Task(std::vector<Func>&& fs, uint64_t deadline)
      : fs_(std::move(fs)), total_(fs_.size()), deadline_(deadline) {}

  uint64_t getDeadline() const { return deadline_; }
  uint64_t getEnqueueOrder() const { return enqueueOrder_; }

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
  uint64_t enqueueOrder_;
  std::shared_ptr<RequestContext> context_ = RequestContext::saveContext();
  std::chrono::steady_clock::time_point enqueueTime_ =
      std::chrono::steady_clock::now();
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

  explicit TaskQueue()
      : buckets_{}, curDeadline_(kLatestDeadline), numItems_(0) {}

  void push(TaskPtr task) {
    auto deadline = task->getDeadline();
    auto& bucket = getBucket(deadline);
    {
      std::unique_lock guard(bucket.mutex);
      task->setEnqueueOrder(bucket.enqueued++);
      bucket.tasks.push(std::move(task));
      bucket.empty.store(bucket.tasks.empty(), std::memory_order_relaxed);
    }

    numItems_.fetch_add(1, std::memory_order_seq_cst);

    // Update current earliest deadline if necessary
    uint64_t curDeadline = curDeadline_.load(std::memory_order_relaxed);
    do {
      if (curDeadline <= deadline) {
        break;
      }
    } while (!curDeadline_.compare_exchange_weak(
        curDeadline, deadline, std::memory_order_relaxed));
  }

  TaskPtr pop() {
    bool needDeadlineUpdate = false;
    for (;;) {
      if (numItems_.load(std::memory_order_seq_cst) == 0) {
        return nullptr;
      }

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
          numItems_.fetch_sub(1, std::memory_order_seq_cst);
        }
      }

      // We may have finished processing the current task / bucket. Going back
      // to the beginning of the loop to find the next bucket.
      needDeadlineUpdate = true;
    }
  }

  std::size_t size() const { return numItems_.load(std::memory_order_seq_cst); }

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
  std::atomic<uint64_t> curDeadline_;

  // All operations performed on `numItems_` explicitly specify memory
  // ordering of `std::memory_order_seq_cst`. This is due to `numItems_`
  // performing Dekker's algorithm with `numIdleThreads_` prior to consumer
  // threads (workers) wait on `sem_`.
  std::atomic<std::size_t> numItems_;
};

/* static */ std::unique_ptr<EDFThreadPoolSemaphore>
EDFThreadPoolExecutor::makeDefaultSemaphore() {
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

void EDFThreadPoolExecutor::add(Func f, std::size_t total, uint64_t deadline) {
  if (FOLLY_UNLIKELY(isJoin_.load(std::memory_order_relaxed) || total == 0)) {
    return;
  }

  taskQueue_->push(std::make_shared<Task>(std::move(f), total, deadline));

  auto numIdleThreads = numIdleThreads_.load(std::memory_order_seq_cst);
  if (numIdleThreads > 0) {
    // If idle threads are available notify them, otherwise all worker threads
    // are running and will get around to this task in time.
    sem_->post(std::min(total, numIdleThreads));
  }
}

void EDFThreadPoolExecutor::add(std::vector<Func> fs, uint64_t deadline) {
  if (FOLLY_UNLIKELY(fs.empty())) {
    return;
  }

  auto total = fs.size();
  taskQueue_->push(std::make_shared<Task>(std::move(fs), deadline));

  auto numIdleThreads = numIdleThreads_.load(std::memory_order_seq_cst);
  if (numIdleThreads > 0) {
    // If idle threads are available notify them, otherwise all worker threads
    // are running and will get around to this task in time.
    sem_->post(std::min(total, numIdleThreads));
  }
}

void EDFThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();
  ExecutorBlockingGuard guard{
      ExecutorBlockingGuard::TrackTag{}, this, getName()};

  thread->startupBaton.post();
  for (;;) {
    auto task = take();

    // Handle thread stopping
    if (FOLLY_UNLIKELY(!task)) {
      // Actually remove the thread from the list.
      std::unique_lock w{threadListLock_};
      for (auto& o : observers_) {
        o->threadStopped(thread.get());
      }
      threadList_.remove(thread);
      stoppedThreads_.add(thread);
      return;
    }

    int iter = task->next();
    if (FOLLY_UNLIKELY(iter < 0)) {
      // This task is already finished
      continue;
    }

    thread->idle.store(false, std::memory_order_relaxed);
    auto startTime = std::chrono::steady_clock::now();
    TaskStats stats;
    stats.enqueueTime = task->enqueueTime_;
    if (task->context_) {
      stats.requestId = task->context_->getRootId();
    }

    stats.waitTime = startTime - stats.enqueueTime;
    invokeCatchingExns("EDFThreadPoolExecutor: func", [&] {
      std::exchange(task, {})->run(iter);
    });
    stats.runTime = std::chrono::steady_clock::now() - startTime;
    thread->idle.store(true, std::memory_order_relaxed);
    thread->lastActiveTime.store(
        std::chrono::steady_clock::now(), std::memory_order_relaxed);
    auto& inCallback = *thread->taskStatsCallbacks->inCallback;
    thread->taskStatsCallbacks->callbackList.withRLock([&](auto& callbacks) {
      inCallback = true;
      SCOPE_EXIT { inCallback = false; };
      invokeCatchingExns("EDFThreadPoolExecutor: stats callback", [&] {
        for (auto& callback : callbacks) {
          callback(stats);
        }
      });
    });
  }
}

// threadListLock_ is writelocked.
void EDFThreadPoolExecutor::stopThreads(std::size_t numThreads) {
  threadsToStop_.fetch_add(numThreads, std::memory_order_relaxed);
  sem_->post(numThreads);
}

// threadListLock_ is read (or write) locked.
std::size_t EDFThreadPoolExecutor::getPendingTaskCountImpl() const {
  return taskQueue_->size();
}

bool EDFThreadPoolExecutor::shouldStop() {
  // in normal cases, only do a read (prevents cache line bounces)
  if (threadsToStop_.load(std::memory_order_relaxed) <= 0 ||
      isJoin_.load(std::memory_order_relaxed)) {
    return false;
  }
  // modify only if needed
  if (threadsToStop_.fetch_sub(1, std::memory_order_relaxed) > 0) {
    return true;
  } else {
    threadsToStop_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
}

std::shared_ptr<EDFThreadPoolExecutor::Task> EDFThreadPoolExecutor::take() {
  if (FOLLY_UNLIKELY(shouldStop())) {
    return nullptr;
  }

  if (auto task = taskQueue_->pop()) {
    return task;
  }

  if (FOLLY_UNLIKELY(isJoin_.load(std::memory_order_relaxed))) {
    return nullptr;
  }

  // No tasks on the horizon, so go sleep
  numIdleThreads_.fetch_add(1, std::memory_order_seq_cst);

  SCOPE_EXIT { numIdleThreads_.fetch_sub(1, std::memory_order_seq_cst); };

  for (;;) {
    if (FOLLY_UNLIKELY(shouldStop())) {
      return nullptr;
    }

    if (auto task = taskQueue_->pop()) {
      // It's possible to return a finished task here, in which case
      // the worker will call this function again.
      return task;
    }

    if (FOLLY_UNLIKELY(isJoin_.load(std::memory_order_relaxed))) {
      return nullptr;
    }

    sem_->wait();
  }
}

} // namespace folly
