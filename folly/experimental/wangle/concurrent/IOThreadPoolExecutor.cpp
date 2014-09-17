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

#include <folly/experimental/wangle/concurrent/IOThreadPoolExecutor.h>

#include <folly/MoveWrapper.h>
#include <glog/logging.h>

namespace folly { namespace wangle {

IOThreadPoolExecutor::IOThreadPoolExecutor(
    size_t numThreads,
    std::unique_ptr<ThreadFactory> threadFactory)
  : ThreadPoolExecutor(numThreads, std::move(threadFactory)),
    nextThread_(0) {
  addThreads(numThreads);
  CHECK(threadList_.get().size() == numThreads);
}

IOThreadPoolExecutor::~IOThreadPoolExecutor() {
  stop();
}

void IOThreadPoolExecutor::add(Func func) {
  RWSpinLock::ReadHolder{&threadListLock_};
  if (threadList_.get().empty()) {
    throw std::runtime_error("No threads available");
  }
  auto thread = threadList_.get()[nextThread_++ % threadList_.get().size()];
  auto ioThread = std::static_pointer_cast<IOThread>(thread);

  auto moveFunc = folly::makeMoveWrapper(std::move(func));
  auto wrappedFunc = [moveFunc, ioThread] () {
    (*moveFunc)();
    ioThread->outstandingTasks--;
  };

  ioThread->outstandingTasks++;
  if (!ioThread->eventBase.runInEventBaseThread(std::move(wrappedFunc))) {
    ioThread->outstandingTasks--;
    throw std::runtime_error("Unable to run func in event base thread");
  }
}

std::shared_ptr<ThreadPoolExecutor::Thread>
IOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>();
}

void IOThreadPoolExecutor::threadRun(ThreadPtr thread) {
  const auto ioThread = std::static_pointer_cast<IOThread>(thread);
  while (ioThread->shouldRun) {
    ioThread->eventBase.loopForever();
  }
  if (isJoin_) {
    while (ioThread->outstandingTasks > 0) {
      ioThread->eventBase.loopOnce();
    }
  }
  stoppedThreads_.add(ioThread);
}

void IOThreadPoolExecutor::stopThreads(size_t n) {
  for (int i = 0; i < n; i++) {
    const auto ioThread = std::static_pointer_cast<IOThread>(
        threadList_.get()[i]);
    ioThread->shouldRun = false;
    ioThread->eventBase.terminateLoopSoon();
  }
}

}} // folly::wangle
