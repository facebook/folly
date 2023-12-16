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

#include <folly/String.h>
#include <folly/experimental/io/EpollBackend.h>
#include <folly/experimental/io/MuxIOThreadPoolExecutor.h>

#include <glog/logging.h>

namespace folly {
void MuxIOThreadPoolExecutor::Stats::update(
    int numEvents, std::chrono::microseconds wait) {
  if (numEvents < minNumEvents || minNumEvents == -1) {
    minNumEvents = numEvents;
  }

  if (numEvents > maxNumEvents) {
    maxNumEvents = numEvents;
  }

  totalNumEvents += static_cast<size_t>(numEvents);

  ++totalWakeups;

  totalWait += wait;

  if ((wait.count() < minWait.count()) || (minWait.count() == 0)) {
    minWait = wait;
  }

  if (wait.count() > maxWait.count()) {
    maxWait = wait;
  }
}

MuxIOThreadPoolExecutor::Handler::~Handler() {}

MuxIOThreadPoolExecutor::EvbHandler::EvbHandler(folly::EventBase* e)
    : Handler(false /*handleInline*/), evb(e) {
  fd = evb->getBackend()->getPollableFd();
}

MuxIOThreadPoolExecutor::EvbHandler::~EvbHandler() {}

void MuxIOThreadPoolExecutor::EvbHandler::handle(
    MuxIOThreadPoolExecutor* /*parent*/) {
  evb->loopPollSetup();
  do {
    evb->loopPoll();
  } while (evb->getNumLoopCallbacks() > 0);
  evb->loopPollCleanup();
}

MuxIOThreadPoolExecutor::EventFdHandler::EventFdHandler()
    : Handler(true /*handleInline*/) {
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
  if (ret != sizeof(val)) {
    throw std::runtime_error(folly::errnoStr(errno));
  }
}

void MuxIOThreadPoolExecutor::EventFdHandler::drainFd() {
  uint64_t data = 0;
  int ret;
  do {
    ret = ::read(fd, &data, sizeof(data));
  } while (ret < 0 && errno == EINTR);
}

void MuxIOThreadPoolExecutor::EventFdHandler::handle(
    MuxIOThreadPoolExecutor* parent) {
  parent->handleDequeue();
}

MuxIOThreadPoolExecutor::MuxIOThreadPoolExecutor(
    size_t numThreads,
    Options options,
    std::shared_ptr<ThreadFactory> threadFactory,
    EventBaseManager* ebm)
    : IOThreadPoolExecutorBase(
          numThreads, numThreads, std::move(threadFactory)),
      options_(options),
      nextEVB_(0),
      eventBaseManager_(ebm) {
  returnList_.arm();
  epFd_ = ::epoll_create1(EPOLL_CLOEXEC);

  if (epFd_ < 0) {
    throw std::runtime_error(folly::errnoStr(errno));
  }

  addHandler(&returnEvfd_, true /*first*/, true /*persist*/);

  if (options_.numEvbs == 0) {
    options_.numEvbs = numThreads;
  }

  folly::ThrottledLifoSem::Options opts;
  opts.wakeUpInterval = options_.wakeUpInterval;
  queue_ = std::make_unique<
      folly::UnboundedBlockingQueue<HandlerTask, folly::ThrottledLifoSem>>(
      opts);

  evbs_.reserve(options_.numEvbs);
  for (size_t i = 0; i < options_.numEvbs; ++i) {
    auto evb = makeEventBase();
    auto handler = std::make_unique<EvbHandler>(evb.get());
    evbs_.emplace_back(std::move(evb));
    auto* ptr = handler.get();
    ptr->evb->runInEventBaseThread([]() {});
    // Run the first iteration to set up internal handlers.
    handler->handle(this);

    handlers_.emplace_back(std::move(handler));
    addHandler(ptr, true /*first*/, false /*persist*/);
  }

  setNumThreads(numThreads);
  registerThreadPoolExecutor(this);
  if (options.enableThreadIdCollection) {
    threadIdCollector_ = std::make_unique<ThreadIdWorkerProvider>();
  }

  mainThread_ = std::make_unique<std::thread>([this]() { mainThreadFunc(); });
}

MuxIOThreadPoolExecutor::~MuxIOThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  {
    std::shared_lock<folly::SharedMutex> lock{threadListLock_};
    for (const auto& o : observers_) {
      maybeUnregisterEventBases(o.get());
    }
  }
  stop();
  stop_ = true;
  wakeup(1);
  mainThread_->join();
  queue_.reset();
  ::close(epFd_);
}

void MuxIOThreadPoolExecutor::mainThreadFunc() {
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
    for (int i = 0; i < ret; ++i) {
      CHECK(events[i].data.ptr != nullptr);
      auto* handler = static_cast<Handler*>(events[i].data.ptr);

      if (handler->handleInline()) {
        handler->handle(this);
      } else {
        queue_->add(HandlerTask(handler));
      }
    }
  }
}

void MuxIOThreadPoolExecutor::addHandler(
    MuxIOThreadPoolExecutor::Handler* handler, bool first, bool persist) {
  CHECK(!(persist && !first));

  epoll_event event = {};
  event.data.ptr = handler;
  event.events = (persist ? 0 : EPOLLONESHOT) | EPOLLIN;

  int fd = handler->fd;

  auto ret = first ? (::epoll_ctl(epFd_, EPOLL_CTL_ADD, fd, &event))
                   : (::epoll_ctl(epFd_, EPOLL_CTL_MOD, fd, &event));

  CHECK_EQ(ret, 0);
}

void MuxIOThreadPoolExecutor::enqueueHandler(Handler* handler) {
  if (returnList_.insertHeadArm(handler)) {
    notifyFd();
  }
}

void MuxIOThreadPoolExecutor::handleDequeue() {
  drainFd();

  while (auto* handler = returnList_.arm()) {
    while (handler) {
      auto* next = handler->next();
      handler->next() = nullptr;
      addHandler(handler, false /*first*/, false /*persist*/);
      handler = next;
    }
  }
}

void MuxIOThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void MuxIOThreadPoolExecutor::add(
    Func func, std::chrono::milliseconds expiration, Func expireCallback) {
  // ensureActiveThreads();
  auto evb = pickEVB();

  auto task = Task(std::move(func), expiration, std::move(expireCallback));
  auto wrappedFunc = [this, task = std::move(task)]() mutable {
    auto ioThread = *thisThread_;
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
  {
    // we need to pick an EVB here for the IOThreadPoolDeadlockDetectorObserver
    // TODO - fix the IOThreadPoolDeadlockDetectorObserver to support
    // the MuxIOThreadPoolExecutor too
    auto* evb = pickEVB();
    ioThread->eventBase = evb;
  }
  thisThread_.reset(new std::shared_ptr<IOThread>(ioThread));

  eventBaseManager_->clearEventBase();

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

  while (ioThread->shouldRun) {
    auto entry = queue_->take();
    auto* handler = entry.handler;
    auto* evb = handler->getEventBase();
    CHECK(!!evb);
    eventBaseManager_->setEventBase(evb, false);
    handler->handle(this);
    eventBaseManager_->clearEventBase();
    enqueueHandler(handler);
  };

  ioThread->eventBase = nullptr;
  eventBaseManager_->clearEventBase();
}

folly::EventBase* MuxIOThreadPoolExecutor::pickEVB() {
  auto& evb = evbs_[nextEVB_++ % evbs_.size()];

  return evb.get();
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
  std::vector<Executor::KeepAlive<EventBase>> evbs;
  for (auto& evb : evbs_) {
    evbs.emplace_back(evb.get());
  }
  return evbs;
}

EventBase* MuxIOThreadPoolExecutor::getEventBase() {
  return pickEVB();
}

void MuxIOThreadPoolExecutor::wakeup(size_t num) {
  for (size_t i = 0; i < num; ++i) {
    auto& evb = evbs_[i % evbs_.size()];
    evb->runInEventBaseThread([]() {});
  }
}

void MuxIOThreadPoolExecutor::stopThreads(size_t n) {
  std::vector<ThreadPtr> stoppedThreads;
  stoppedThreads.reserve(n);
  for (size_t i = 0; i < n; i++) {
    const auto ioThread =
        std::static_pointer_cast<IOThread>(threadList_.get()[i]);
    for (auto& o : observers_) {
      o->threadStopped(ioThread.get());
    }
    ioThread->shouldRun = false;
    stoppedThreads.push_back(ioThread);
  }
  wakeup(n);
  for (const auto& thread : stoppedThreads) {
    stoppedThreads_.add(thread);
    threadList_.remove(thread);
  }
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
