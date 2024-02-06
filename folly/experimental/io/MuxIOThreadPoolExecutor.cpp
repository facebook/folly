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

#include <folly/experimental/io/EpollBackend.h>

namespace folly {

struct MuxIOThreadPoolExecutor::EvbState {
  EvbState() : evb(evbOptions()) {}

  EventBase evb;
  std::atomic<bool> done{false};
  std::unique_ptr<EventBasePoller::Handle> handle;

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
      eventBaseManager_(ebm),
      readyQueueSem_(
          ThrottledLifoSem::Options{.wakeUpInterval = options.wakeUpInterval}) {
  auto numEventBases =
      options_.numEventBases == 0 ? numThreads : options_.numEventBases;

  setNumThreads(numThreads);

  fdGroup_ = EventBasePoller::get().makeFdGroup(
      [this](Range<EventBasePoller::Handle**> readyHandles) noexcept {
        for (auto* handle : readyHandles) {
          readyQueue_.enqueue(handle);
        }
        readyQueueSem_.post(readyHandles.size());
      });

  evbStates_.reserve(numEventBases);
  for (size_t i = 0; i < numEventBases; ++i) {
    auto& evbState = evbStates_.emplace_back(std::make_unique<EvbState>());
    evbState->evb.setStrictLoopThread();
    // To simulate EventBase::loopForever(), acquire a keepalive for the whole
    // lifetime of the executor. This will make The EventBase's notification
    // queue a non-internal event, so we can poll it even when there are no
    // other events.
    keepAlives_.emplace_back(&evbState->evb);
    auto fd = evbState->evb.getBackend()->getPollableFd();
    CHECK_GE(fd, 0);
    evbState->handle = fdGroup_->add(fd, evbState.get());
  }

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
  auto evb = pickEvb();
  auto task = Task(std::move(func), expiration, std::move(expireCallback));
  auto wrappedFunc = [this, task = std::move(task)]() mutable {
    const auto& ioThread = *thisThread_;
    runTask(ioThread, std::move(task));
    pendingTasks_--;
  };

  pendingTasks_++;
  evb->runInEventBaseThread(std::move(wrappedFunc));
}

std::shared_ptr<ThreadPoolExecutor::Thread>
MuxIOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>(this);
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

  while (true) {
    readyQueueSem_.wait();
    auto handle = readyQueue_.dequeue();
    if (handle == nullptr) {
      break;
    }
    auto* evbState = handle->getUserData<EvbState>();
    auto* evb = &evbState->evb;

    ioThread->curEventBase = evb;
    eventBaseManager_->setEventBase(evb, false);

    evb->loopPollSetup();
    do {
      evb->loopPoll();
    } while (evb->getNumLoopCallbacks() > 0);
    evb->loopPollCleanup();

    eventBaseManager_->clearEventBase();
    ioThread->curEventBase = nullptr;

    handle->handoff(evbState->done.load());
  };

  std::unique_lock w{threadListLock_};
  for (auto& o : observers_) {
    o->threadStopped(thread.get());
  }
  threadList_.remove(thread);
  stoppedThreads_.add(thread);
}

folly::EventBase* MuxIOThreadPoolExecutor::pickEvb() {
  if (auto ioThread = thisThread_.get_existing()) {
    return (*ioThread)->curEventBase;
  }

  return &evbStates_[nextEvb_++ % evbStates_.size()]->evb;
}

size_t MuxIOThreadPoolExecutor::getPendingTaskCountImpl() const {
  return pendingTasks_.load();
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
  return pickEvb();
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

  for (auto& evbState : evbStates_) {
    evbState->done = true;
    // Unblock the evb so it can be handed off.
    evbState->evb.runInEventBaseThread([] {});
    fdGroup_->reclaim(std::move(evbState->handle));
  }
  fdGroup_.reset();
  keepAlives_.clear();
  evbStates_.clear();

  stopAndJoinAllThreads(/* isJoin */ true);
}

} // namespace folly
