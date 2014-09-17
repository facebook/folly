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

#include <folly/experimental/wangle/concurrent/CPUThreadPoolExecutor.h>

namespace folly { namespace wangle {

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 18;

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<BlockingQueue<Task>> taskQueue,
    std::unique_ptr<ThreadFactory> threadFactory)
    : ThreadPoolExecutor(numThreads, std::move(threadFactory)),
      taskQueue_(std::move(taskQueue)) {
  addThreads(numThreads);
  CHECK(threadList_.get().size() == numThreads);
}

CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  stop();
  CHECK(threadsToStop_ == 0);
}

void CPUThreadPoolExecutor::add(Func func) {
  // TODO handle enqueue failure, here and in other add() callsites
  taskQueue_->add(Task(std::move(func)));
}

void CPUThreadPoolExecutor::threadRun(std::shared_ptr<Thread> thread) {
  while (1) {
    // TODO expiration / codel
    auto t = taskQueue_->take();
    if (UNLIKELY(t.poison)) {
      CHECK(threadsToStop_-- > 0);
      stoppedThreads_.add(thread);
      return;
    } else {
      thread->idle = false;
      try {
        t.func();
      } catch (const std::exception& e) {
        LOG(ERROR) << "CPUThreadPoolExecutor: func threw unhandled " <<
                      typeid(e).name() << " exception: " << e.what();
      } catch (...) {
        LOG(ERROR) << "CPUThreadPoolExecutor: func threw unhandled non-exception "
                      "object";
      }
      thread->idle = true;
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
  for (int i = 0; i < n; i++) {
    taskQueue_->add(Task());
  }
}

}} // folly::wangle
