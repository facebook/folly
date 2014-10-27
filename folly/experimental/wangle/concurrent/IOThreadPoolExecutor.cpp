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
#include <folly/io/async/EventBaseManager.h>

#include <folly/detail/MemoryIdler.h>

namespace folly { namespace wangle {

using folly::detail::MemoryIdler;

/* Class that will free jemalloc caches and madvise the stack away
 * if the event loop is unused for some period of time
 */
class MemoryIdlerTimeout
    : public AsyncTimeout , public EventBase::LoopCallback {
 public:
  explicit MemoryIdlerTimeout(EventBase* b) : AsyncTimeout(b), base_(b) {}

  virtual void timeoutExpired() noexcept {
    idled = true;
  }

  virtual void runLoopCallback() noexcept {
    if (idled) {
      MemoryIdler::flushLocalMallocCaches();
      MemoryIdler::unmapUnusedStack(MemoryIdler::kDefaultStackToRetain);

      idled = false;
    } else {
      std::chrono::steady_clock::duration idleTimeout =
        MemoryIdler::defaultIdleTimeout.load(
          std::memory_order_acquire);

      idleTimeout = MemoryIdler::getVariationTimeout(idleTimeout);

      scheduleTimeout(std::chrono::duration_cast<std::chrono::milliseconds>(
                        idleTimeout).count());
    }

    // reschedule this callback for the next event loop.
    base_->runBeforeLoop(this);
  }
 private:
  EventBase* base_;
  bool idled{false};
} ;

IOThreadPoolExecutor::IOThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory)
  : ThreadPoolExecutor(numThreads, std::move(threadFactory)),
    nextThread_(0) {
  addThreads(numThreads);
  CHECK(threadList_.get().size() == numThreads);
}

IOThreadPoolExecutor::~IOThreadPoolExecutor() {
  stop();
}

void IOThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void IOThreadPoolExecutor::add(
    Func func,
    std::chrono::milliseconds expiration,
    Func expireCallback) {
  RWSpinLock::ReadHolder{&threadListLock_};
  if (threadList_.get().empty()) {
    throw std::runtime_error("No threads available");
  }
  auto thread = threadList_.get()[nextThread_++ % threadList_.get().size()];
  auto ioThread = std::static_pointer_cast<IOThread>(thread);

  auto moveTask = folly::makeMoveWrapper(
      Task(std::move(func), expiration, std::move(expireCallback)));
  auto wrappedFunc = [this, ioThread, moveTask] () mutable {
    runTask(ioThread, std::move(*moveTask));
    ioThread->pendingTasks--;
  };

  ioThread->pendingTasks++;
  if (!ioThread->eventBase->runInEventBaseThread(std::move(wrappedFunc))) {
    ioThread->pendingTasks--;
    throw std::runtime_error("Unable to run func in event base thread");
  }
}

std::shared_ptr<ThreadPoolExecutor::Thread>
IOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>();
}

void IOThreadPoolExecutor::threadRun(ThreadPtr thread) {
  const auto ioThread = std::static_pointer_cast<IOThread>(thread);
  ioThread->eventBase =
    folly::EventBaseManager::get()->getEventBase();

  auto idler = new MemoryIdlerTimeout(ioThread->eventBase);
  ioThread->eventBase->runBeforeLoop(idler);

  thread->startupBaton.post();
  while (ioThread->shouldRun) {
    ioThread->eventBase->loopForever();
  }
  if (isJoin_) {
    while (ioThread->pendingTasks > 0) {
      ioThread->eventBase->loopOnce();
    }
  }
  stoppedThreads_.add(ioThread);
}

// threadListLock_ is writelocked
void IOThreadPoolExecutor::stopThreads(size_t n) {
  for (int i = 0; i < n; i++) {
    const auto ioThread = std::static_pointer_cast<IOThread>(
        threadList_.get()[i]);
    ioThread->shouldRun = false;
    ioThread->eventBase->terminateLoopSoon();
  }
}

// threadListLock_ is readlocked
uint64_t IOThreadPoolExecutor::getPendingTaskCount() {
  uint64_t count = 0;
  for (const auto& thread : threadList_.get()) {
    auto ioThread = std::static_pointer_cast<IOThread>(thread);
    size_t pendingTasks = ioThread->pendingTasks;
    if (pendingTasks > 0 && !ioThread->idle) {
      pendingTasks--;
    }
    count += pendingTasks;
  }
  return count;
}

}} // folly::wangle
