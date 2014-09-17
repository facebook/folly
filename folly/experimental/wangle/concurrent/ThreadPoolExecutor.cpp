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

#include <folly/experimental/wangle/concurrent/ThreadPoolExecutor.h>

namespace folly { namespace wangle {

ThreadPoolExecutor::ThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<ThreadFactory> threadFactory)
    : threadFactory_(std::move(threadFactory)) {}

ThreadPoolExecutor::~ThreadPoolExecutor() {
  CHECK(threadList_.get().size() == 0);
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

void ThreadPoolExecutor::addThreads(size_t n) {
  for (int i = 0; i < n; i++) {
    auto thread = makeThread();
    // TODO need a notion of failing to create the thread
    // and then handling for that case
    thread->handle = threadFactory_->newThread(
        std::bind(&ThreadPoolExecutor::threadRun, this, thread));
    threadList_.add(thread);
  }
}

void ThreadPoolExecutor::removeThreads(size_t n, bool isJoin) {
  CHECK(n <= threadList_.get().size());
  CHECK(stoppedThreads_.size() == 0);
  isJoin_ = isJoin;
  stopThreads(n);
  for (int i = 0; i < n; i++) {
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

}} // folly::wangle
