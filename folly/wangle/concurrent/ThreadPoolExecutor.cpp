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

#include <folly/wangle/concurrent/ThreadPoolExecutor.h>

namespace folly { namespace wangle {

ThreadPoolExecutor::ThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory)
    : threadFactory_(std::move(threadFactory)),
      taskStatsSubject_(std::make_shared<Subject<TaskStats>>()) {}

ThreadPoolExecutor::~ThreadPoolExecutor() {
  CHECK(threadList_.get().size() == 0);
}

ThreadPoolExecutor::Task::Task(
    Func&& func,
    std::chrono::milliseconds expiration,
    Func&& expireCallback)
    : func_(std::move(func)),
      expiration_(expiration),
      expireCallback_(std::move(expireCallback)) {
  // Assume that the task in enqueued on creation
  enqueueTime_ = std::chrono::steady_clock::now();
}

void ThreadPoolExecutor::runTask(
    const ThreadPtr& thread,
    Task&& task) {
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
    try {
      task.func_();
    } catch (const std::exception& e) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled " <<
                    typeid(e).name() << " exception: " << e.what();
    } catch (...) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled non-exception "
                    "object";
    }
    task.stats_.runTime = std::chrono::steady_clock::now() - startTime;
  }
  thread->idle = true;
  thread->taskStatsSubject->onNext(std::move(task.stats_));
}

size_t ThreadPoolExecutor::numThreads() {
  RWSpinLock::ReadHolder{&threadListLock_};
  return threadList_.get().size();
}

void ThreadPoolExecutor::setNumThreads(size_t n) {
  RWSpinLock::WriteHolder{&threadListLock_};
  const auto current = threadList_.get().size();
  if (n > current ) {
    addThreads(n - current);
  } else if (n < current) {
    removeThreads(current - n, true);
  }
  CHECK(threadList_.get().size() == n);
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
  CHECK(n <= threadList_.get().size());
  CHECK(stoppedThreads_.size() == 0);
  isJoin_ = isJoin;
  stopThreads(n);
  for (size_t i = 0; i < n; i++) {
    auto thread = stoppedThreads_.take();
    thread->handle.join();
    threadList_.remove(thread);
  }
  CHECK(stoppedThreads_.size() == 0);
}

void ThreadPoolExecutor::stop() {
  RWSpinLock::WriteHolder{&threadListLock_};
  removeThreads(threadList_.get().size(), false);
  CHECK(threadList_.get().size() == 0);
}

void ThreadPoolExecutor::join() {
  RWSpinLock::WriteHolder{&threadListLock_};
  removeThreads(threadList_.get().size(), true);
  CHECK(threadList_.get().size() == 0);
}

ThreadPoolExecutor::PoolStats ThreadPoolExecutor::getPoolStats() {
  RWSpinLock::ReadHolder{&threadListLock_};
  ThreadPoolExecutor::PoolStats stats;
  stats.threadCount = threadList_.get().size();
  for (auto thread : threadList_.get()) {
    if (thread->idle) {
      stats.idleThreadCount++;
    } else {
      stats.activeThreadCount++;
    }
  }
  stats.pendingTaskCount = getPendingTaskCount();
  stats.totalTaskCount = stats.pendingTaskCount + stats.activeThreadCount;
  return stats;
}

std::atomic<uint64_t> ThreadPoolExecutor::Thread::nextId(0);

void ThreadPoolExecutor::StoppedThreadQueue::add(
    ThreadPoolExecutor::ThreadPtr item) {
  std::lock_guard<std::mutex> guard(mutex_);
  queue_.push(std::move(item));
  sem_.post();
}

ThreadPoolExecutor::ThreadPtr ThreadPoolExecutor::StoppedThreadQueue::take() {
  while(1) {
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
  RWSpinLock::ReadHolder{&threadListLock_};
  observers_.push_back(o);
  for (auto& thread : threadList_.get()) {
    o->threadPreviouslyStarted(thread.get());
  }
}

void ThreadPoolExecutor::removeObserver(std::shared_ptr<Observer> o) {
  RWSpinLock::ReadHolder{&threadListLock_};
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

}} // folly::wangle
