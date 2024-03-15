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

#include <folly/experimental/io/MuxIOThreadPoolExecutor.h>

#include <stdexcept>

#include <fmt/format.h>
#include <folly/container/Enumerate.h>
#include <folly/experimental/io/EpollBackend.h>
#include <folly/lang/Align.h>
#include <folly/synchronization/Latch.h>

namespace folly {

namespace {

ThrottledLifoSem::Options throttledLifoSemOptions(
    std::chrono::nanoseconds wakeUpInterval) {
  ThrottledLifoSem::Options opts;
  opts.wakeUpInterval = wakeUpInterval;
  return opts;
}

} // namespace

struct MuxIOThreadPoolExecutor::EvbState {
  EvbState() : evb(evbOptions()) {}

  EventBase evb;
  std::unique_ptr<EventBasePoller::Handle> handle;

  alignas(cacheline_align_v) std::atomic<size_t> pendingTasks = 0;

 private:
  static const EventBase::Options& evbOptions() {
#if FOLLY_HAS_EPOLL
    static const auto options = EventBase::Options{}.setBackendFactory(
        [] { return std::make_unique<EpollBackend>(EpollBackend::Options{}); });
    return options;
#else
    throw std::invalid_argument("EpollBackend not supported");
#endif
  }
};

MuxIOThreadPoolExecutor::MuxIOThreadPoolExecutor(
    size_t numThreads,
    Options options,
    std::shared_ptr<ThreadFactory> threadFactory,
    EventBaseManager* ebm)
    : IOThreadPoolExecutorBase(
          numThreads, numThreads, std::move(threadFactory)),
      options_(std::move(options)),
      numEventBases_(
          options_.numEventBases == 0 ? numThreads : options_.numEventBases),
      eventBaseManager_(ebm),
      readyQueueSem_(throttledLifoSemOptions(options.wakeUpInterval)) {
  setNumThreads(numThreads);

  fdGroup_ = EventBasePoller::get().makeFdGroup(
      [this](Range<EventBasePoller::Handle**> readyHandles) noexcept {
        for (auto* handle : readyHandles) {
          readyQueue_.enqueue(handle);
        }
        readyQueueSem_.post(readyHandles.size());
      });

  evbStates_.reserve(numEventBases_);
  Latch allEvbsRunning(numEventBases_);
  for (size_t i = 0; i < numEventBases_; ++i) {
    auto& evbState = evbStates_.emplace_back(std::make_unique<EvbState>());
    evbState->evb.setStrictLoopThread();
    evbState->evb.runInEventBaseThread([&] { allEvbsRunning.count_down(); });
    // Keep the loop running until shutdown.
    keepAlives_.emplace_back(&evbState->evb);
    auto fd = evbState->evb.getBackend()->getPollableFd();
    CHECK_GE(fd, 0);
    evbState->handle = fdGroup_->add(fd, evbState.get());
  }
  allEvbsRunning.wait();

  registerThreadPoolExecutor(this);
  if (options_.enableThreadIdCollection) {
    threadIdCollector_ = std::make_unique<ThreadIdWorkerProvider>();
  }
}

MuxIOThreadPoolExecutor::~MuxIOThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  stop();
}

void MuxIOThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void MuxIOThreadPoolExecutor::add(
    Func func, std::chrono::milliseconds expiration, Func expireCallback) {
  auto& evbState = pickEvbState();
  auto task = Task(std::move(func), expiration, std::move(expireCallback));
  registerTaskEnqueue(task);
  auto wrappedFunc = [this, &evbState, task = std::move(task)]() mutable {
    const auto& ioThread = *thisThread_;
    runTask(ioThread, std::move(task));
    evbState.pendingTasks--;
  };

  evbState.pendingTasks++;
  evbState.evb.runInEventBaseThread(std::move(wrappedFunc));
}

void MuxIOThreadPoolExecutor::validateNumThreads(size_t numThreads) {
  if (numThreads == 0 || numThreads > numEventBases_) {
    throw std::invalid_argument(fmt::format(
        "Unsupported number of threads: {} (with {} EventBases)",
        numThreads,
        numEventBases_));
  }
}

std::shared_ptr<ThreadPoolExecutor::Thread>
MuxIOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>();
}

void MuxIOThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();

  const auto ioThread = std::static_pointer_cast<IOThread>(thread);
  thisThread_.reset(new std::shared_ptr<IOThread>(ioThread));

  auto tid = folly::getOSThreadID();
  if (threadIdCollector_) {
    threadIdCollector_->addTid(tid);
  }
  SCOPE_EXIT {
    if (threadIdCollector_) {
      threadIdCollector_->removeTid(tid);
    }
  };
  thread->startupBaton.post();

  ExecutorBlockingGuard guard{
      ExecutorBlockingGuard::TrackTag{}, this, getName()};

  while (true) {
    readyQueueSem_.wait(WaitOptions{}.spin_max(options_.idleSpinMax));
    auto handle = readyQueue_.dequeue();
    if (handle == nullptr) {
      break;
    }
    auto* evbState = handle->getUserData<EvbState>();
    auto* evb = &evbState->evb;

    ioThread->curEvbState = evbState;
    eventBaseManager_->setEventBase(evb, false);

    auto status = evb->loopWithSuspension();
    CHECK(status != EventBase::LoopStatus::kError);

    eventBaseManager_->clearEventBase();
    ioThread->curEvbState = nullptr;

    handle->handoff(status == EventBase::LoopStatus::kDone);
  }

  std::unique_lock w{threadListLock_};
  for (auto& o : observers_) {
    o->threadStopped(thread.get());
  }
  threadList_.remove(thread);
  stoppedThreads_.add(thread);
}

MuxIOThreadPoolExecutor::EvbState& MuxIOThreadPoolExecutor::pickEvbState() {
  if (auto ioThread = thisThread_.get_existing()) {
    return *(*ioThread)->curEvbState;
  }

  return *evbStates_[nextEvb_++ % evbStates_.size()];
}

size_t MuxIOThreadPoolExecutor::getPendingTaskCountImpl() const {
  size_t ret = 0;
  for (const auto& evbState : evbStates_) {
    ret += evbState->pendingTasks.load();
  }
  return ret;
}

void MuxIOThreadPoolExecutor::addObserver(std::shared_ptr<Observer> o) {
  if (auto ioObserver = dynamic_cast<IOObserver*>(o.get())) {
    // All EventBases are created at construction time.
    for (const auto& evbState : evbStates_) {
      ioObserver->registerEventBase(evbState->evb);
    }
  }
  ThreadPoolExecutor::addObserver(std::move(o));
}

void MuxIOThreadPoolExecutor::maybeUnregisterEventBases(Observer* o) {
  if (auto ioObserver = dynamic_cast<IOObserver*>(o)) {
    for (const auto& evbState : evbStates_) {
      ioObserver->unregisterEventBase(evbState->evb);
    }
  }
}

void MuxIOThreadPoolExecutor::removeObserver(std::shared_ptr<Observer> o) {
  maybeUnregisterEventBases(o.get());
  ThreadPoolExecutor::addObserver(std::move(o));
}

std::vector<folly::Executor::KeepAlive<folly::EventBase>>
MuxIOThreadPoolExecutor::getAllEventBases() {
  return keepAlives_;
}

folly::EventBaseManager* MuxIOThreadPoolExecutor::getEventBaseManager() {
  return eventBaseManager_;
}

EventBase* MuxIOThreadPoolExecutor::getEventBase() {
  return &pickEvbState().evb;
}

void MuxIOThreadPoolExecutor::stopThreads(size_t n) {
  for (size_t i = 0; i < n; i++) {
    readyQueue_.enqueue(nullptr); // Poison.
  }
  readyQueueSem_.post(n);
}

void MuxIOThreadPoolExecutor::stop() {
  join();
}

void MuxIOThreadPoolExecutor::join() {
  if (!joinKeepAliveOnce()) {
    return; // Already called.
  }

  {
    std::shared_lock<folly::SharedMutex> lock{threadListLock_};
    for (const auto& o : observers_) {
      maybeUnregisterEventBases(o.get());
    }
  }

  for (auto&& [i, evbState] : folly::enumerate(evbStates_)) {
    // Release the keepalive so the loop can complete and the handle be
    // reclaimed.
    keepAlives_[i].reset();
    fdGroup_->reclaim(std::move(evbState->handle));
  }
  fdGroup_.reset();
  evbStates_.clear();

  stopAndJoinAllThreads(/* isJoin */ true);
}

} // namespace folly
