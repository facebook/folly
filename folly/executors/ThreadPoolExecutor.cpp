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

#include <folly/executors/ThreadPoolExecutor.h>

#include <folly/executors/GlobalThreadPoolList.h>

namespace folly {

ThreadPoolExecutor::ThreadPoolExecutor(
    size_t /* numThreads */,
    std::shared_ptr<ThreadFactory> threadFactory,
    bool isWaitForAll)
    : threadFactory_(std::move(threadFactory)),
      isWaitForAll_(isWaitForAll),
      taskStatsCallbacks_(std::make_shared<TaskStatsCallbackRegistry>()),
      threadPoolHook_("Wangle::ThreadPoolExecutor") {}

ThreadPoolExecutor::~ThreadPoolExecutor() {
  CHECK_EQ(0, threadList_.get().size());
}

ThreadPoolExecutor::Task::Task(
    Func&& func,
    std::chrono::milliseconds expiration,
    Func&& expireCallback)
    : func_(std::move(func)),
      expiration_(expiration),
      expireCallback_(std::move(expireCallback)),
      context_(folly::RequestContext::saveContext()) {
  // Assume that the task in enqueued on creation
  enqueueTime_ = std::chrono::steady_clock::now();
}

void ThreadPoolExecutor::runTask(const ThreadPtr& thread, Task&& task) {
  thread->idle = false;
  auto startTime = std::chrono::steady_clock::now();
  task.stats_.waitTime = startTime - task.enqueueTime_;
  if (task.expiration_ > std::chrono::milliseconds(0) &&
      task.stats_.waitTime >= task.expiration_) {
    task.stats_.expired = true;
    if (task.expireCallback_ != nullptr) {
      task.expireCallback_();
    }
  } else {
    folly::RequestContextScopeGuard rctx(task.context_);
    try {
      task.func_();
    } catch (const std::exception& e) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled "
                 << typeid(e).name() << " exception: " << e.what();
    } catch (...) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled non-exception "
                    "object";
    }
    task.stats_.runTime = std::chrono::steady_clock::now() - startTime;
  }
  thread->idle = true;
  thread->lastActiveTime = std::chrono::steady_clock::now();
  thread->taskStatsCallbacks->callbackList.withRLock([&](auto& callbacks) {
    *thread->taskStatsCallbacks->inCallback = true;
    SCOPE_EXIT {
      *thread->taskStatsCallbacks->inCallback = false;
    };
    try {
      for (auto& callback : callbacks) {
        callback(task.stats_);
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "ThreadPoolExecutor: task stats callback threw "
                    "unhandled "
                 << typeid(e).name() << " exception: " << e.what();
    } catch (...) {
      LOG(ERROR) << "ThreadPoolExecutor: task stats callback threw "
                    "unhandled non-exception object";
    }
  });
}

size_t ThreadPoolExecutor::numThreads() {
  RWSpinLock::ReadHolder r{&threadListLock_};
  return threadList_.get().size();
}

void ThreadPoolExecutor::setNumThreads(size_t n) {
  size_t numThreadsToJoin = 0;
  {
    RWSpinLock::WriteHolder w{&threadListLock_};
    const auto current = threadList_.get().size();
    if (n > current) {
      addThreads(n - current);
    } else if (n < current) {
      numThreadsToJoin = current - n;
      removeThreads(numThreadsToJoin, true);
    }
  }
  joinStoppedThreads(numThreadsToJoin);
  CHECK_EQ(n, threadList_.get().size());
  CHECK_EQ(0, stoppedThreads_.size());
}

// threadListLock_ is writelocked
void ThreadPoolExecutor::addThreads(size_t n) {
  std::vector<ThreadPtr> newThreads;
  for (size_t i = 0; i < n; i++) {
    newThreads.push_back(makeThread());
  }
  for (auto& thread : newThreads) {
    // TODO need a notion of failing to create the thread
    // and then handling for that case
    thread->handle = threadFactory_->newThread(
        std::bind(&ThreadPoolExecutor::threadRun, this, thread));
    threadList_.add(thread);
  }
  for (auto& thread : newThreads) {
    thread->startupBaton.wait();
  }
  for (auto& o : observers_) {
    for (auto& thread : newThreads) {
      o->threadStarted(thread.get());
    }
  }
}

// threadListLock_ is writelocked
void ThreadPoolExecutor::removeThreads(size_t n, bool isJoin) {
  CHECK_LE(n, threadList_.get().size());
  isJoin_ = isJoin;
  stopThreads(n);
}

void ThreadPoolExecutor::joinStoppedThreads(size_t n) {
  for (size_t i = 0; i < n; i++) {
    auto thread = stoppedThreads_.take();
    thread->handle.join();
  }
}

void ThreadPoolExecutor::stop() {
  size_t n = 0;
  {
    RWSpinLock::WriteHolder w{&threadListLock_};
    n = threadList_.get().size();
    removeThreads(n, false);
  }
  joinStoppedThreads(n);
  CHECK_EQ(0, threadList_.get().size());
  CHECK_EQ(0, stoppedThreads_.size());
}

void ThreadPoolExecutor::join() {
  size_t n = 0;
  {
    RWSpinLock::WriteHolder w{&threadListLock_};
    n = threadList_.get().size();
    removeThreads(n, true);
  }
  joinStoppedThreads(n);
  CHECK_EQ(0, threadList_.get().size());
  CHECK_EQ(0, stoppedThreads_.size());
}

ThreadPoolExecutor::PoolStats ThreadPoolExecutor::getPoolStats() {
  const auto now = std::chrono::steady_clock::now();
  RWSpinLock::ReadHolder r{&threadListLock_};
  ThreadPoolExecutor::PoolStats stats;
  stats.threadCount = threadList_.get().size();
  for (auto thread : threadList_.get()) {
    if (thread->idle) {
      stats.idleThreadCount++;
      const std::chrono::nanoseconds idleTime = now - thread->lastActiveTime;
      stats.maxIdleTime = std::max(stats.maxIdleTime, idleTime);
    } else {
      stats.activeThreadCount++;
    }
  }
  stats.pendingTaskCount = getPendingTaskCountImpl(r);
  stats.totalTaskCount = stats.pendingTaskCount + stats.activeThreadCount;
  return stats;
}

uint64_t ThreadPoolExecutor::getPendingTaskCount() {
  RWSpinLock::ReadHolder r{&threadListLock_};
  return getPendingTaskCountImpl(r);
}

std::atomic<uint64_t> ThreadPoolExecutor::Thread::nextId(0);

void ThreadPoolExecutor::subscribeToTaskStats(TaskStatsCallback cb) {
  if (*taskStatsCallbacks_->inCallback) {
    throw std::runtime_error("cannot subscribe in task stats callback");
  }
  taskStatsCallbacks_->callbackList.wlock()->push_back(std::move(cb));
}

void ThreadPoolExecutor::StoppedThreadQueue::add(
    ThreadPoolExecutor::ThreadPtr item) {
  std::lock_guard<std::mutex> guard(mutex_);
  queue_.push(std::move(item));
  sem_.post();
}

ThreadPoolExecutor::ThreadPtr ThreadPoolExecutor::StoppedThreadQueue::take() {
  while (true) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (queue_.size() > 0) {
        auto item = std::move(queue_.front());
        queue_.pop();
        return item;
      }
    }
    sem_.wait();
  }
}

size_t ThreadPoolExecutor::StoppedThreadQueue::size() {
  std::lock_guard<std::mutex> guard(mutex_);
  return queue_.size();
}

void ThreadPoolExecutor::addObserver(std::shared_ptr<Observer> o) {
  RWSpinLock::ReadHolder r{&threadListLock_};
  observers_.push_back(o);
  for (auto& thread : threadList_.get()) {
    o->threadPreviouslyStarted(thread.get());
  }
}

void ThreadPoolExecutor::removeObserver(std::shared_ptr<Observer> o) {
  RWSpinLock::ReadHolder r{&threadListLock_};
  for (auto& thread : threadList_.get()) {
    o->threadNotYetStopped(thread.get());
  }

  for (auto it = observers_.begin(); it != observers_.end(); it++) {
    if (*it == o) {
      observers_.erase(it);
      return;
    }
  }
  DCHECK(false);
}

} // namespace folly
