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

#include <folly/io/async/MuxIOThreadPoolExecutor.h>

#include <stdexcept>

#include <fmt/format.h>
#include <folly/io/async/EpollBackend.h>
#include <folly/lang/Align.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/Latch.h>

FOLLY_GFLAGS_DEFINE_string(
    folly_mux_io_thread_pool_executor_poller_backend,
    "epoll",
    "Default EventBasePoller backend: \"epoll\", \"io_uring\"");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_mux_io_thread_pool_executor_poller_spin_us,
    10,
    "Spin-wait for events up to this amount (us) before blocking wait");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_mux_io_thread_pool_executor_poller_sleep_us,
    0,
    "Sleep for this amount (us) before doing a blocking wait for events");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_mux_io_thread_pool_executor_poller_epoll_max_events,
    64,
    "Maximum number of events to process in one epoll_wait iteration");
FOLLY_GFLAGS_DEFINE_bool(
    folly_mux_io_thread_pool_executor_poller_epoll_rearm_inline,
    true,
    "When using epoll backend, re-arm events inline in handoff()");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_mux_io_thread_pool_executor_poller_io_uring_sq_entries,
    128,
    "Minimum number of io_uring submission queue entries");

namespace folly {

namespace {

ThrottledLifoSem::Options throttledLifoSemOptions(
    std::chrono::nanoseconds wakeUpInterval) {
  ThrottledLifoSem::Options opts;
  opts.wakeUpInterval = wakeUpInterval;
  return opts;
}

detail::EventBasePoller::Options pollerOptionsFromGFlags() {
  detail::EventBasePoller::Options opts;
  const auto& backend = FLAGS_folly_mux_io_thread_pool_executor_poller_backend;
  if (backend == "epoll") {
    opts.backend = detail::EventBasePoller::Options::Backend::kEpoll;
  } else if (backend == "io_uring") {
    opts.backend = detail::EventBasePoller::Options::Backend::kIoUring;
  } else {
    throw std::invalid_argument(
        fmt::format("Unsupported EventBasePoller backend: {}", backend));
  }
  opts.epollRearmInline =
      FLAGS_folly_mux_io_thread_pool_executor_poller_epoll_rearm_inline;
  opts.spinTimeout = std::chrono::microseconds{
      FLAGS_folly_mux_io_thread_pool_executor_poller_spin_us};
  opts.sleepBeforeBlock = std::chrono::microseconds{
      FLAGS_folly_mux_io_thread_pool_executor_poller_sleep_us};
  opts.epollMaxEvents =
      FLAGS_folly_mux_io_thread_pool_executor_poller_epoll_max_events;
  opts.ioUringSqEntries =
      FLAGS_folly_mux_io_thread_pool_executor_poller_io_uring_sq_entries;
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
    static const auto options = EventBase::Options{}.setBackendFactory([] {
      return std::make_unique<EpollBackend>(EpollBackend::Options{});
    });
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
      readyQueueSem_(throttledLifoSemOptions(options_.wakeUpInterval)) {
  poller_ = EventBasePoller::create(
      options_.pollerOptions
          ? *options_.pollerOptions
          : pollerOptionsFromGFlags());

  setNumThreads(numThreads);

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
    evbState->handle = poller_->add(fd, evbState.get());
  }

  // Must be posted before allEvbsRunning.wait(): no thread polls until the
  // sentinel is dequeued.
  readyQueue_.enqueue(kWaitSentinel());
  readyQueueSem_.post();

  allEvbsRunning.wait();

  registerThreadPoolExecutor(this);
  if (options_.enableThreadIdCollection) {
    threadIdCollector_ = std::make_unique<ThreadIdWorkerProvider>();
  }
}

MuxIOThreadPoolExecutor::~MuxIOThreadPoolExecutor() {
  deregisterThreadPoolExecutor(this);
  stop();
  destroyTaskObservers();
}

void MuxIOThreadPoolExecutor::add(Func func) {
  add(std::move(func), std::chrono::milliseconds(0));
}

void MuxIOThreadPoolExecutor::add(
    Func func, std::chrono::milliseconds expiration, Func expireCallback) {
  auto& evbState = pickEvbState();
  auto task = Task(
      std::move(func),
      folly::RequestContext::saveContext(),
      expiration,
      std::move(expireCallback));
  registerTaskEnqueue(task);
  auto wrappedFunc = [this, &evbState, task = std::move(task)]() mutable {
    const auto& ioThread = *thisThread_;
    runTask(ioThread, std::move(task));
    evbState.pendingTasks--;
  };

  evbState.pendingTasks++;
  evbState.evb.runInEventBaseThread(std::move(wrappedFunc));
}

void MuxIOThreadPoolExecutor::prepareSetNumThreads(size_t numThreads) {
  if (numThreads == 0 || numThreads > numEventBases_) {
    throw std::invalid_argument(
        fmt::format(
            "Unsupported number of threads: {} (with {} EventBases)",
            numThreads,
            numEventBases_));
  }
  // Threads may only be stopped at shutdown: with io_uring, the pending
  // operations a thread submitted are cancelled when it exits.
  // This runs under threadListLock_, so the check and the minThreads_ update
  // are atomic with setNumThreads()'s mutation: concurrent calls serialize, and
  // any that would reduce the count throws.
  const auto currentMax = maxThreads_.load(std::memory_order_relaxed);
  if (numThreads < currentMax) {
    throw std::invalid_argument(
        fmt::format(
            "Reducing the number of threads is not supported: {} < {}",
            numThreads,
            currentMax));
  }
  // Force minThreads_ == maxThreads_ so the pool's timeout machinery can never
  // reduce the thread count on its own (the base only ever lowers minThreads_).
  minThreads_.store(numThreads, std::memory_order_relaxed);
}

std::shared_ptr<ThreadPoolExecutor::Thread>
MuxIOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>();
}

void MuxIOThreadPoolExecutor::threadRun(ThreadPtr thread) {
  this->threadPoolHook_.registerThread();

  const auto& ioThread = *thisThread_ =
      std::static_pointer_cast<IOThread>(thread);

  auto tid = folly::getOSThreadID();
  if (threadIdCollector_) {
    threadIdCollector_->addTid(tid);
  }
  SCOPE_EXIT {
    if (threadIdCollector_) {
      threadIdCollector_->removeTid(tid);
    }
  };
  thread->initBaton.post();
  thread->readyBaton.wait();
  if (thread->cancelledBeforeReady) {
    return;
  }

  ExecutorBlockingGuard guard{
      ExecutorBlockingGuard::TrackTag{}, this, getName()};

  while (true) {
    readyQueueSem_.wait(WaitOptions{}.spin_max(options_.idleSpinMax));
    auto* handle = readyQueue_.dequeue();

    if (handle == nullptr) {
      break; // Shutdown poison.
    }

    if (handle == kWaitSentinel()) {
      auto readyHandles = poller_->wait();
      if (readyHandles.empty()) {
        // Interrupted by shutdown. Don't re-enqueue sentinel.
        continue;
      }
      // Process one handle (any would do) inline, after enqueuing the others
      // and the sentinel.
      handle = readyHandles.back();
      readyHandles.pop_back();
      for (auto* h : readyHandles) {
        readyQueue_.enqueue(h);
      }
      readyQueue_.enqueue(kWaitSentinel());
      readyQueueSem_.post(static_cast<uint32_t>(readyHandles.size() + 1));
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
  stoppedThreads_.add(std::move(thread));
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
  ThreadPoolExecutor::removeObserver(std::move(o));
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
    std::shared_lock lock{threadListLock_};
    for (const auto& o : observers_) {
      maybeUnregisterEventBases(o.get());
    }
  }

  // Release keepalives so the loops can complete and handles be reclaimed.
  for (auto& keepAlive : keepAlives_) {
    keepAlive.reset();
  }

  // Reclaim all handles (blocks until each handoff(true) completes).
  for (auto& evbState : evbStates_) {
    poller_->reclaim(std::move(evbState->handle));
  }

  // No handle can become ready anymore; wake up the thread in wait() so it can
  // consume a poison pill.
  poller_->shutdown();

  stopAndJoinAllThreads(/* isJoin */ true);

  poller_.reset();
  evbStates_.clear();
}

} // namespace folly
