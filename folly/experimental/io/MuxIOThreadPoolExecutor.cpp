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

#include <folly/experimental/io/Epoll.h> // @manual

#if FOLLY_HAS_EPOLL

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <boost/polymorphic_cast.hpp>
#include <folly/String.h>
#include <folly/experimental/io/EpollBackend.h>
#include <folly/experimental/io/MuxIOThreadPoolExecutor.h>
#include <folly/system/ThreadName.h>

#include <glog/logging.h>

namespace folly {

void MuxIOThreadPoolExecutor::Stats::update(
    int numEvents, std::chrono::microseconds wait) {
  minNumEvents = std::min(minNumEvents, numEvents);
  maxNumEvents = std::max(maxNumEvents, numEvents);
  totalNumEvents += static_cast<size_t>(numEvents);
  totalWakeups += 1;
  totalWait += wait;
  minWait = std::min(minWait, wait);
  maxWait = std::max(maxWait, wait);
}

MuxIOThreadPoolExecutor::EvbHandler::EvbHandler(folly::EventBase* e) : evb(e) {
  fd = evb->getBackend()->getPollableFd();
}

void MuxIOThreadPoolExecutor::EvbHandler::handle(
    MuxIOThreadPoolExecutor* /*parent*/) {
  evb->loopPollSetup();
  do {
    evb->loopPoll();
  } while (evb->getNumLoopCallbacks() > 0);
  evb->loopPollCleanup();
}

MuxIOThreadPoolExecutor::EventFdHandler::EventFdHandler() {
  fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (fd < 0) {
    throw std::runtime_error(folly::errnoStr(errno));
  }
}

MuxIOThreadPoolExecutor::EventFdHandler::~EventFdHandler() {
  ::close(fd);
}

void MuxIOThreadPoolExecutor::EventFdHandler::notifyFd() {
  uint64_t val = 1;
  auto ret = ::write(fd, &val, sizeof(val));
  CHECK_EQ(ret, sizeof(val));
}

void MuxIOThreadPoolExecutor::EventFdHandler::handle(
    MuxIOThreadPoolExecutor* parent) {
  // No need to drain the epFd_, it is edge-triggered.
  parent->handleDequeue();
}

template <class T>
bool MuxIOThreadPoolExecutor::Queue<T>::insert(T* t) {
  DCHECK(t->next == nullptr);

  auto oldHead = head_.load(std::memory_order_relaxed);
  bool ret;
  do {
    t->next = (ret = (oldHead == kQueueArmedTag())) ? nullptr : oldHead;
  } while (!head_.compare_exchange_weak(
      oldHead, t, std::memory_order_release, std::memory_order_relaxed));
  return ret;
}

template <class T>
T* MuxIOThreadPoolExecutor::Queue<T>::arm() {
  T* oldHead = head_.load(std::memory_order_relaxed);
  T* newHead;
  T* ret;
  do {
    if (oldHead == nullptr || oldHead == kQueueArmedTag()) {
      newHead = kQueueArmedTag();
      ret = nullptr;
    } else {
      newHead = nullptr;
      ret = oldHead;
    }
  } while (!head_.compare_exchange_weak(
      oldHead, newHead, std::memory_order_acq_rel, std::memory_order_relaxed));

  return ret;
}

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
  epFd_ = ::epoll_create1(EPOLL_CLOEXEC);

  if (epFd_ < 0) {
    throw std::runtime_error(folly::errnoStr(errno));
  }

  returnQueue_.arm();
  addHandler(&returnEvfd_, AddHandlerType::kPersist, Triggering::kEdge);

  auto numEventBases =
      options_.numEventBases == 0 ? numThreads : options_.numEventBases;

  evbs_.reserve(numEventBases);
  for (size_t i = 0; i < numEventBases; ++i) {
    auto& evb = evbs_.emplace_back(makeEventBase());
    // To simulate EventBase::loopForever(), acquire a keepalive for the whole
    // lifetime of the executor. This will make The EventBase's notification
    // queue a non-internal event, so we can poll it even when there are no
    // other events.
    keepAlives_.emplace_back(evb.get());
    auto& handler =
        handlers_.emplace_back(std::make_unique<EvbHandler>(evb.get()));
    // Run the first iteration to set up internal handlers.
    handler->handle(this);
    addHandler(handler.get(), AddHandlerType::kOneShot);
  }

  setNumThreads(numThreads);
  registerThreadPoolExecutor(this);
  if (options_.enableThreadIdCollection) {
    threadIdCollector_ = std::make_unique<ThreadIdWorkerProvider>();
  }

  mainThread_ = std::make_unique<std::thread>([this]() { mainThreadFunc(); });
}

MuxIOThreadPoolExecutor::~MuxIOThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  stop();
}

void MuxIOThreadPoolExecutor::mainThreadFunc() {
  setThreadName("MuxIOTPExMain");

  std::vector<struct epoll_event> events(options_.maxEvents);
  while (!stop_.load()) {
    auto start = std::chrono::steady_clock::now();
    auto ret = ::epoll_wait(epFd_, events.data(), events.size(), -1);
    if (ret <= 0) {
      continue;
    }
    auto end = std::chrono::steady_clock::now();
    auto delta =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats_.update(ret, delta);
    size_t enqueued = 0;
    for (int i = 0; i < ret; ++i) {
      CHECK(events[i].data.ptr != nullptr);
      auto* handler = static_cast<Handler*>(events[i].data.ptr);

      if (handler->isEvbHandler()) {
        readyQueue_.enqueue(boost::polymorphic_downcast<EvbHandler*>(handler));
        ++enqueued;
      } else {
        handler->handle(this);
      }
    }
    if (enqueued > 0) {
      readyQueueSem_.post(enqueued);
    }
  }
}

void MuxIOThreadPoolExecutor::addHandler(
    Handler* handler, AddHandlerType type, Triggering triggering) {
  epoll_event event = {};
  event.data.ptr = handler;
  event.events = EPOLLIN;

  int op = EPOLL_CTL_ADD;
  switch (type) {
    case AddHandlerType::kPersist:
      break;
    case AddHandlerType::kOneShotRearm:
      op = EPOLL_CTL_MOD;
      FOLLY_FALLTHROUGH;
    case AddHandlerType::kOneShot:
      event.events |= EPOLLONESHOT;
      break;
  }

  switch (triggering) {
    case Triggering::kLevel:
      break;
    case Triggering::kEdge:
      event.events |= EPOLLET;
      break;
  }

  auto ret = ::epoll_ctl(epFd_, op, handler->fd, &event);
  CHECK_EQ(ret, 0);
}

void MuxIOThreadPoolExecutor::returnHandler(EvbHandler* handler) {
  if (returnQueue_.insert(handler)) {
    returnEvfd_.notifyFd();
  }
}

void MuxIOThreadPoolExecutor::handleDequeue() {
  while (auto* handler = returnQueue_.arm()) {
    while (handler) {
      auto* next = std::exchange(handler->next, nullptr);
      addHandler(handler, AddHandlerType::kOneShotRearm);
      handler = next;
    }
  }
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
    auto handler = readyQueue_.dequeue();
    if (handler->isPoison()) {
      delete handler;
      break;
    }
    auto* evb = CHECK_NOTNULL(handler->evb);

    ioThread->curEventBase = evb;
    eventBaseManager_->setEventBase(evb, false);
    handler->handle(this);
    eventBaseManager_->clearEventBase();
    ioThread->curEventBase = nullptr;
    returnHandler(handler);
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

  return evbs_[nextEvb_++ % evbs_.size()].get();
}

size_t MuxIOThreadPoolExecutor::getPendingTaskCountImpl() const {
  return pendingTasks_.load();
}

void MuxIOThreadPoolExecutor::addObserver(std::shared_ptr<Observer> o) {
  if (auto ioObserver = dynamic_cast<IOObserver*>(o.get())) {
    // All EventBases are created at construction time.
    for (const auto& evb : evbs_) {
      ioObserver->registerEventBase(*evb);
    }
  }
  ThreadPoolExecutor::addObserver(std::move(o));
}

void MuxIOThreadPoolExecutor::maybeUnregisterEventBases(Observer* o) {
  if (auto ioObserver = dynamic_cast<IOObserver*>(o)) {
    for (const auto& evb : evbs_) {
      ioObserver->unregisterEventBase(*evb);
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
    readyQueue_.enqueue(new EvbHandler); // Poison.
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

  keepAlives_.clear();

  stop_ = true;
  returnEvfd_.notifyFd();
  mainThread_->join();

  stopAndJoinAllThreads(/* isJoin */ true);
  ::close(epFd_);
}

std::unique_ptr<folly::EventBase> MuxIOThreadPoolExecutor::makeEventBase() {
  auto factory = [] {
    folly::EpollBackend::Options options;
    return std::make_unique<folly::EpollBackend>(options);
  };
  folly::EventBase::Options opts;
  return std::make_unique<folly::EventBase>(
      opts.setBackendFactory(std::move(factory)).setStrictLoopThread(true));
}

} // namespace folly
#endif
