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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <folly/io/async/EventBase.h>

#include <fcntl.h>

#include <memory>
#include <mutex>
#include <thread>

#include <folly/ExceptionString.h>
#include <folly/Memory.h>
#include <folly/String.h>
#include <folly/experimental/EventCount.h>
#include <folly/io/async/EventBaseAtomicNotificationQueue.h>
#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/io/async/EventBaseLocal.h>
#include <folly/io/async/VirtualEventBase.h>
#include <folly/portability/Unistd.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/ThreadId.h>
#include <folly/system/ThreadName.h>

#if defined(__linux__) && !FOLLY_MOBILE
#define FOLLY_USE_EPOLLET

#include <sys/epoll.h>

struct event_base {
  void* evsel;
  void* evbase;
};

struct epollop {
  void* fds;
  int nfds;
  void* events;
  int nevents;
  int epfd;
};
#endif

namespace {

class EventBaseBackend : public folly::EventBaseBackendBase {
 public:
  EventBaseBackend();
  explicit EventBaseBackend(event_base* evb);
  ~EventBaseBackend() override;

  event_base* getEventBase() override { return evb_; }

  int eb_event_base_loop(int flags) override;
  int eb_event_base_loopbreak() override;

  int eb_event_add(Event& event, const struct timeval* timeout) override;
  int eb_event_del(EventBaseBackendBase::Event& event) override;

  bool eb_event_active(Event& event, int res) override;

  bool setEdgeTriggered(Event& event) override;

 private:
  event_base* evb_;
};

EventBaseBackend::EventBaseBackend() {
  evb_ = event_base_new();
}

EventBaseBackend::EventBaseBackend(event_base* evb) : evb_(evb) {
  if (FOLLY_UNLIKELY(evb_ == nullptr)) {
    LOG(ERROR) << "EventBase(): Pass nullptr as event base.";
    throw std::invalid_argument("EventBase(): event base cannot be nullptr");
  }
}

int EventBaseBackend::eb_event_base_loop(int flags) {
  return event_base_loop(evb_, flags);
}

int EventBaseBackend::eb_event_base_loopbreak() {
  return event_base_loopbreak(evb_);
}

int EventBaseBackend::eb_event_add(
    Event& event, const struct timeval* timeout) {
  return event_add(event.getEvent(), timeout);
}

int EventBaseBackend::eb_event_del(EventBaseBackendBase::Event& event) {
  return event_del(event.getEvent());
}

bool EventBaseBackend::eb_event_active(Event& event, int res) {
  event_active(event.getEvent(), res, 1);
  return true;
}

bool EventBaseBackend::setEdgeTriggered(Event& event) {
#ifdef FOLLY_USE_EPOLLET
  // Until v2 libevent doesn't expose API to set edge-triggered flag for events.
  // If epoll backend is used by libevent, we can enable it though epoll_ctl
  // directly.
  // Note that this code depends on internal event_base and epollop layout, so
  // we have to validate libevent version.
  static const bool supportedVersion =
      !strcmp(event_get_version(), "1.4.14b-stable");
  if (!supportedVersion || strcmp(event_base_get_method(evb_), "epoll")) {
    return false;
  }

  auto epfd = static_cast<epollop*>(evb_->evbase)->epfd;
  epoll_event epev = {0, {nullptr}};
  epev.data.fd = event.eb_ev_fd();
  epev.events = EPOLLET;
  if (event.eb_ev_events() & EV_READ) {
    epev.events |= EPOLLIN;
  }
  if (event.eb_ev_events() & EV_WRITE) {
    epev.events |= EPOLLOUT;
  }
  if (::epoll_ctl(epfd, EPOLL_CTL_MOD, event.eb_ev_fd(), &epev) == -1) {
    LOG(DFATAL) << "epoll_ctl failed: " << errno;
    return false;
  }
  return true;
#else
  (void)event;
  return false;
#endif
}

EventBaseBackend::~EventBaseBackend() {
  event_base_free(evb_);
}

class ExecutionObserverScopeGuard {
 public:
  ExecutionObserverScopeGuard(
      folly::ExecutionObserver::List* observerList, void* id)
      : observerList_(observerList), id_{reinterpret_cast<uintptr_t>(id)} {
    if (!observerList_->empty()) {
      for (auto& observer : *observerList_) {
        observer.starting(id_);
      }
    }
  }

  ~ExecutionObserverScopeGuard() {
    if (!observerList_->empty()) {
      for (auto& observer : *observerList_) {
        observer.stopped(id_);
      }
    }
  }

 private:
  folly::ExecutionObserver::List* observerList_;
  uintptr_t id_;
};
} // namespace

namespace folly {

class EventBase::FuncRunner {
 public:
  void operator()(Func func) noexcept { func(); }
};

class EventBase::ThreadIdCollector : public WorkerProvider {
 public:
  IdsWithKeepAlive collectThreadIds() override {
    keepAlives_.fetch_add(1, std::memory_order_acq_rel);
    auto guard = std::make_unique<Guard>(*this);
    auto tid = tid_.load(std::memory_order_acquire);
    if (tid == 0) {
      return {};
    }
    return {std::move(guard), std::vector<pid_t>{tid}};
  }

  void setTid() { tid_.store(getOSThreadID(), std::memory_order_release); }

  void unsetTid() {
    tid_.store(0, std::memory_order_release);
    // Wait for any outstanding keepalives.
    wakeUp_.await(
        [&] { return keepAlives_.load(std::memory_order_acquire) == 0; });
  }

 private:
  class Guard : public KeepAlive {
   public:
    Guard(ThreadIdCollector& parent) : parent_(parent) {}

    ~Guard() override {
      if (parent_.keepAlives_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        parent_.wakeUp_.notifyAll();
      }
    }

   private:
    ThreadIdCollector& parent_;
  };

  std::atomic<pid_t> tid_ = 0;
  std::atomic<size_t> keepAlives_ = 0;
  EventCount wakeUp_;
};

/*
 * EventBase methods
 */

EventBase::EventBase(std::chrono::milliseconds tickInterval)
    : EventBase(Options().setTimerTickInterval(tickInterval)) {}

EventBase::EventBase(bool enableTimeMeasurement)
    : EventBase(Options().setSkipTimeMeasurement(!enableTimeMeasurement)) {}

// takes ownership of the event_base
EventBase::EventBase(event_base* evb, bool enableTimeMeasurement)
    : EventBase(Options()
                    .setBackendFactory([evb] {
                      return std::make_unique<EventBaseBackend>(evb);
                    })
                    .setSkipTimeMeasurement(!enableTimeMeasurement)) {}

EventBase::EventBase(Options options)
    : intervalDuration_(options.timerTickInterval),
      enableTimeMeasurement_(!options.skipTimeMeasurement),
      strictLoopThread_(options.strictLoopThread),
      loopThread_(),
      runOnceCallbacks_(nullptr),
      stop_(false),
      queue_(nullptr),
      maxLatency_(0),
      avgLoopTime_(std::chrono::seconds(2)),
      maxLatencyLoopTime_(avgLoopTime_),
      nextLoopCnt_(
          std::size_t(-40)) // Early wrap-around so bugs will manifest soon
      ,
      latestLoopCnt_(nextLoopCnt_),
      startWork_(),
      observer_(nullptr),
      observerSampleCount_(0),
      evb_(
          options.backendFactory ? options.backendFactory()
                                 : getDefaultBackend()),
      threadIdCollector_(std::make_unique<ThreadIdCollector>()) {
  initNotificationQueue();
}

EventBase::~EventBase() {
  // Relax strict mode to allow callbacks to run in the destructor outside of
  // the main loop. Note that any methods (including driving the loop) must be
  // called before the destructor starts, so it is safe to modify the variable.
  strictLoopThread_ = false;

  // Call all pre-destruction callbacks, before we start cleaning up our state
  // or apply any keepalives
  while (!preDestructionCallbacks_.rlock()->empty()) {
    OnDestructionCallback::List callbacks;
    preDestructionCallbacks_.swap(callbacks);
    while (!callbacks.empty()) {
      auto& callback = callbacks.front();
      callbacks.pop_front();
      callback.runCallback();
    }
  }

  std::future<void> virtualEventBaseDestroyFuture;
  if (virtualEventBase_) {
    virtualEventBaseDestroyFuture = virtualEventBase_->destroy();
  }

  // Keep looping until all keep-alive handles are released. Each keep-alive
  // handle signals that some external code will still schedule some work on
  // this EventBase (so it's not safe to destroy it).
  while (loopKeepAliveCount() > 0) {
    applyLoopKeepAlive();
    loopOnce();
  }

  if (virtualEventBaseDestroyFuture.valid()) {
    virtualEventBaseDestroyFuture.get();
  }

  // Call all destruction callbacks, before we start cleaning up our state.
  while (!onDestructionCallbacks_.rlock()->empty()) {
    OnDestructionCallback::List callbacks;
    onDestructionCallbacks_.swap(callbacks);
    while (!callbacks.empty()) {
      auto& callback = callbacks.front();
      callbacks.pop_front();
      callback.runCallback();
    }
  }

  clearCobTimeouts();

  DCHECK_EQ(0u, runBeforeLoopCallbacks_.size());

  runLoopCallbacks();

  queue_->drain();

  // Stop consumer before deleting NotificationQueue
  queue_->stopConsuming();

  // Remove self from all registered EventBaseLocal instances.
  // Notice that we could be racing with EventBaseLocal dtor similarly
  // deregistering itself from all registered EventBase instances. Because
  // both sides need to acquire two locks, but in inverse order, we retry if
  // inner lock acquisition fails to prevent lock inversion deadlock.
  while (true) {
    auto locked = localStorageToDtor_.wlock();
    if (locked->empty()) {
      break;
    }
    auto evbl = *locked->begin();
    if (evbl->tryDeregister(*this)) {
      locked->erase(evbl);
    }
  }

  executionObserverList_.clear();

  localStorage_.clear();

  evb_.reset();

  VLOG(5) << "EventBase(): Destroyed.";
}

bool EventBase::tryDeregister(detail::EventBaseLocalBase& evbl) {
  if (auto locked = localStorageToDtor_.tryWLock()) {
    locked->erase(&evbl);
    runInEventBaseThread([this, key = evbl.key_] { localStorage_.erase(key); });
    return true;
  }
  return false;
}

std::unique_ptr<EventBaseBackendBase> EventBase::getDefaultBackend() {
  return std::make_unique<EventBaseBackend>();
}

size_t EventBase::getNotificationQueueSize() const {
  return queue_->size();
}

size_t EventBase::getNumLoopCallbacks() const {
  dcheckIsInEventBaseThread();
  return loopCallbacks_.size();
}

void EventBase::setMaxReadAtOnce(uint32_t maxAtOnce) {
  queue_->setMaxReadAtOnce(maxAtOnce);
}

void EventBase::checkIsInEventBaseThread() const {
  auto evbTid = loopThread_.load(std::memory_order_relaxed);
  if (!strictLoopThread_ && evbTid == std::thread::id()) {
    return;
  }

  // Using getThreadName(evbTid) instead of name_ will work also if
  // the thread name is set outside of EventBase (and name_ is empty).
  auto curTid = std::this_thread::get_id();
  CHECK_EQ(evbTid, curTid)
      << "This logic must be executed in the event base thread. "
      << "Event base thread name: \""
      << folly::getThreadName(evbTid).value_or("")
      << "\", current thread name: \""
      << folly::getThreadName(curTid).value_or("") << "\"";
}

// Set smoothing coefficient for loop load average; input is # of milliseconds
// for exp(-1) decay.
void EventBase::setLoadAvgMsec(std::chrono::milliseconds ms) {
  assert(enableTimeMeasurement_);
  std::chrono::microseconds us = std::chrono::milliseconds(ms);
  if (ms > std::chrono::milliseconds::zero()) {
    maxLatencyLoopTime_.setTimeInterval(us);
    avgLoopTime_.setTimeInterval(us);
  } else {
    LOG(ERROR) << "non-positive arg to setLoadAvgMsec()";
  }
}

void EventBase::resetLoadAvg(double value) {
  assert(enableTimeMeasurement_);
  avgLoopTime_.reset(value);
  maxLatencyLoopTime_.reset(value);
}

static std::chrono::milliseconds getTimeDelta(
    std::chrono::steady_clock::time_point* prev) {
  auto result = std::chrono::steady_clock::now() - *prev;
  *prev = std::chrono::steady_clock::now();

  return std::chrono::duration_cast<std::chrono::milliseconds>(result);
}

void EventBase::waitUntilRunning() {
  while (loopThread_.load(std::memory_order_acquire) == std::thread::id()) {
    std::this_thread::yield();
  }
}

// enters the event_base loop -- will only exit when forced to
bool EventBase::loop() {
  // Enforce blocking tracking and if we have a name override any previous one
  ExecutorBlockingGuard guard{ExecutorBlockingGuard::TrackTag{}, this, name_};
  return loopBody();
}

bool EventBase::loopIgnoreKeepAlive() {
  if (loopKeepAliveActive_) {
    // Make sure NotificationQueue is not counted as one of the readers
    // (otherwise loopBody won't return until terminateLoopSoon is called).
    queue_->stopConsuming();
    queue_->startConsumingInternal(this);
    loopKeepAliveActive_ = false;
  }
  return loopBody(0, true);
}

bool EventBase::loopOnce(int flags) {
  return loopBody(flags | EVLOOP_ONCE);
}

bool EventBase::loopBody(int flags, bool ignoreKeepAlive) {
  loopMainSetup();
  SCOPE_EXIT { loopMainCleanup(); };
  return loopMain(flags, ignoreKeepAlive);
}

void EventBase::loopPollSetup() {
  loopMainSetup();
}

bool EventBase::loopPoll() {
  return loopMain(EVLOOP_ONCE | EVLOOP_NONBLOCK, false);
}

void EventBase::loopPollCleanup() {
  loopMainCleanup();
}

void EventBase::loopMainSetup() {
  VLOG(5) << "EventBase(): Starting loop.";

  auto const prevLoopThread = loopThread_.exchange(
      std::this_thread::get_id(), std::memory_order_release);
  // NOTE: This also fatals on reentrancy, which is not supported by old
  // versions of libevent.
  CHECK_EQ(std::thread::id(), prevLoopThread)
      << "Driving an EventBase (in thread " << std::this_thread::get_id()
      << ") while it is already being driven (in thread " << prevLoopThread
      << ") is forbidden.";

  if (!name_.empty()) {
    setThreadName(name_);
  }

  threadIdCollector_->setTid();
}

bool EventBase::loopMain(int flags, bool ignoreKeepAlive) {
  int res = 0;
  bool ranLoopCallbacks;
  bool blocking = !(flags & EVLOOP_NONBLOCK);
  bool once = (flags & EVLOOP_ONCE);

  // time-measurement variables.
  std::chrono::steady_clock::time_point prev;
  std::chrono::steady_clock::time_point idleStart = {};
  std::chrono::microseconds busy;
  std::chrono::microseconds idle;

  if (enableTimeMeasurement_) {
    prev = std::chrono::steady_clock::now();
    idleStart = prev;
  }

  SCOPE_EXIT {
    // Consume the stop signal so that the loop can resume on the next call.
    stop_.store(false, std::memory_order_relaxed);
  };

  while (!stop_.load(std::memory_order_relaxed)) {
    if (!ignoreKeepAlive) {
      applyLoopKeepAlive();
    }
    ++nextLoopCnt_;

    // Run the before loop callbacks
    LoopCallbackList callbacks;
    callbacks.swap(runBeforeLoopCallbacks_);

    runLoopCallbacks(callbacks);

    // nobody can add loop callbacks from within this thread if
    // we don't have to handle anything to start with...
    if (blocking && loopCallbacks_.empty()) {
      res = evb_->eb_event_base_loop(EVLOOP_ONCE);
    } else {
      res = evb_->eb_event_base_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // libevent may return 1 early if there are no registered non-internal
    // events, so even if the queue is not empty it may not be processed, thus
    // we check that explicitly.
    //
    // Note that the queue was either not consumed, or it will be re-armed by a
    // loop callback scheduled by execute(), so if there is an enqueue after the
    // empty check here the queue's event will eventually be active.
    if (res != 0 && !queue_->empty()) {
      ExecutionObserverScopeGuard guard(&executionObserverList_, queue_.get());
      queue_->execute();
    }

    ranLoopCallbacks = runLoopCallbacks();

    if (enableTimeMeasurement_) {
      auto now = std::chrono::steady_clock::now();
      busy = std::chrono::duration_cast<std::chrono::microseconds>(
          now - startWork_);
      idle = std::chrono::duration_cast<std::chrono::microseconds>(
          startWork_ - idleStart);
      auto loop_time = busy + idle;

      avgLoopTime_.addSample(loop_time, busy);
      maxLatencyLoopTime_.addSample(loop_time, busy);

      if (observer_) {
        if (++observerSampleCount_ >= observer_->getSampleRate()) {
          observerSampleCount_ = 0;
          observer_->loopSample(busy.count(), idle.count());
        }
      }

      VLOG(11) << "EventBase " << this << " did not timeout "
               << " loop time guess: " << loop_time.count()
               << " idle time: " << idle.count()
               << " busy time: " << busy.count()
               << " avgLoopTime: " << avgLoopTime_.get()
               << " maxLatencyLoopTime: " << maxLatencyLoopTime_.get()
               << " maxLatency_: " << maxLatency_.count() << "us"
               << " notificationQueueSize: " << getNotificationQueueSize()
               << " nothingHandledYet(): " << nothingHandledYet();

      if (maxLatency_ > std::chrono::microseconds::zero()) {
        // see if our average loop time has exceeded our limit
        if (dampenMaxLatency_ &&
            (maxLatencyLoopTime_.get() > double(maxLatency_.count()))) {
          maxLatencyCob_();
          // back off temporarily -- don't keep spamming maxLatencyCob_
          // if we're only a bit over the limit
          maxLatencyLoopTime_.dampen(0.9);
        } else if (!dampenMaxLatency_ && busy > maxLatency_) {
          // If no damping, we compare the raw busy time
          maxLatencyCob_();
        }
      }

      // Our loop run did real work; reset the idle timer
      idleStart = now;
    } else {
      VLOG(11) << "EventBase " << this << " did not timeout";
    }

    if (enableTimeMeasurement_) {
      VLOG(11) << "EventBase " << this
               << " loop time: " << getTimeDelta(&prev).count();
    }

    if (once ||
        // Event loop indicated that there were are no more registered events
        // (except queue_ which is an internal event) and we didn't have any
        // loop callbacks to run, so there is nothing left to do.
        (res != 0 && !ranLoopCallbacks)) {
      break;
    }
  }

  if (res < 0) {
    LOG(ERROR) << "EventBase: -- error in event loop, res = " << res;
    return false;
  } else if (res == 1) {
    VLOG(5) << "EventBase: ran out of events (exiting loop)!";
  } else if (res > 1) {
    LOG(ERROR) << "EventBase: unknown event loop result = " << res;
    return false;
  }
  VLOG(5) << "EventBase(): Done with loop.";
  return true;
}

void EventBase::loopMainCleanup() {
  threadIdCollector_->unsetTid();
  loopThread_.store({}, std::memory_order_release);
}

size_t EventBase::loopKeepAliveCount() {
  return loopKeepAliveCount_.load(std::memory_order_relaxed);
}

void EventBase::applyLoopKeepAlive() {
  auto keepAliveCount = loopKeepAliveCount();
  // Make sure default VirtualEventBase won't hold EventBase::loop() forever.
  if (auto virtualEventBase = tryGetVirtualEventBase()) {
    if (virtualEventBase->keepAliveCount() == 1) {
      --keepAliveCount;
    }
  }

  if (loopKeepAliveActive_ && keepAliveCount == 0) {
    // Restore the notification queue internal flag
    queue_->stopConsuming();
    queue_->startConsumingInternal(this);
    loopKeepAliveActive_ = false;
  } else if (!loopKeepAliveActive_ && keepAliveCount > 0) {
    // Update the notification queue event to treat it as a normal
    // (non-internal) event.  The notification queue event always remains
    // installed, and the main loop won't exit with it installed.
    queue_->stopConsuming();
    queue_->startConsuming(this);
    loopKeepAliveActive_ = true;
  }
}

void EventBase::loopForever() {
  bool ret;
  {
    SCOPE_EXIT { applyLoopKeepAlive(); };
    // Make sure notification queue events are treated as normal events.
    // We can't use loopKeepAlive() here since LoopKeepAlive token can only be
    // released inside a loop.
    ++loopKeepAliveCount_;
    SCOPE_EXIT { --loopKeepAliveCount_; };
    ret = loop();
  }

  if (!ret) {
    folly::throwSystemError("error in EventBase::loopForever()");
  }
}

void EventBase::bumpHandlingTime() {
  if (!enableTimeMeasurement_) {
    return;
  }

  VLOG(11) << "EventBase " << this << " " << __PRETTY_FUNCTION__
           << " (loop) latest " << latestLoopCnt_ << " next " << nextLoopCnt_;
  if (nothingHandledYet()) {
    latestLoopCnt_ = nextLoopCnt_;
    // set the time
    startWork_ = std::chrono::steady_clock::now();

    VLOG(11) << "EventBase " << this << " " << __PRETTY_FUNCTION__
             << " (loop) startWork_ " << startWork_.time_since_epoch().count();
  }
}

void EventBase::terminateLoopSoon() {
  VLOG(5) << "EventBase(): Received terminateLoopSoon() command.";

  auto keepAlive = getKeepAliveToken(this);

  // Set stop to true, so the event loop will know to exit.
  stop_.store(true, std::memory_order_relaxed);

  // If terminateLoopSoon() is called from another thread,
  // the EventBase thread might be stuck waiting for events.
  // In this case, it won't wake up and notice that stop_ is set until it
  // receives another event.  Send an empty frame to the notification queue
  // so that the event loop will wake up even if there are no other events.
  queue_->putMessage([] {});
}

void EventBase::runInLoop(
    LoopCallback* callback,
    bool thisIteration,
    std::shared_ptr<RequestContext> rctx) {
  dcheckIsInEventBaseThread();
  callback->cancelLoopCallback();
  callback->context_ = std::move(rctx);
  if (runOnceCallbacks_ != nullptr && thisIteration) {
    runOnceCallbacks_->push_back(*callback);
  } else {
    loopCallbacks_.push_back(*callback);
  }
}

void EventBase::runInLoop(Func cob, bool thisIteration) {
  dcheckIsInEventBaseThread();
  auto wrapper = new FunctionLoopCallback(std::move(cob));
  wrapper->context_ = RequestContext::saveContext();
  if (runOnceCallbacks_ != nullptr && thisIteration) {
    runOnceCallbacks_->push_back(*wrapper);
  } else {
    loopCallbacks_.push_back(*wrapper);
  }
}

void EventBase::runOnDestruction(OnDestructionCallback& callback) {
  callback.schedule(
      [this](auto& cb) { onDestructionCallbacks_.wlock()->push_back(cb); },
      [this](auto& cb) {
        onDestructionCallbacks_.withWLock(
            [&](auto& list) { list.erase(list.iterator_to(cb)); });
      });
}

void EventBase::runOnDestruction(Func f) {
  auto* callback = new FunctionOnDestructionCallback(std::move(f));
  runOnDestruction(*callback);
}

void EventBase::runOnDestructionStart(OnDestructionCallback& callback) {
  callback.schedule(
      [this](auto& cb) { preDestructionCallbacks_.wlock()->push_back(cb); },
      [this](auto& cb) {
        preDestructionCallbacks_.withWLock(
            [&](auto& list) { list.erase(list.iterator_to(cb)); });
      });
}

void EventBase::runOnDestructionStart(Func f) {
  auto* callback = new FunctionOnDestructionCallback(std::move(f));
  runOnDestructionStart(*callback);
}

void EventBase::runBeforeLoop(LoopCallback* callback) {
  dcheckIsInEventBaseThread();
  callback->cancelLoopCallback();
  runBeforeLoopCallbacks_.push_back(*callback);
}

void EventBase::runInEventBaseThread(Func fn) noexcept {
  // Send the message.
  // It will be received by the FunctionRunner in the EventBase's thread.

  // We try not to schedule nullptr callbacks
  if (!fn) {
    DLOG(FATAL) << "EventBase " << this
                << ": Scheduling nullptr callbacks is not allowed";
    return;
  }

  // Short-circuit if we are already in our event base
  if (inRunningEventBaseThread()) {
    runInLoop(std::move(fn));
    return;
  }

  queue_->putMessage(std::move(fn));
}

void EventBase::runInEventBaseThreadAlwaysEnqueue(Func fn) noexcept {
  // Send the message.
  // It will be received by the FunctionRunner in the EventBase's thread.

  // We try not to schedule nullptr callbacks
  if (!fn) {
    LOG(DFATAL) << "EventBase " << this
                << ": Scheduling nullptr callbacks is not allowed";
    return;
  }

  queue_->putMessage(std::move(fn));
}

void EventBase::runInEventBaseThreadAndWait(Func fn) noexcept {
  if (inRunningEventBaseThread()) {
    LOG(DFATAL) << "EventBase " << this << ": Waiting in the event loop is not "
                << "allowed";
    return;
  }

  Baton<> ready;
  runInEventBaseThread([&ready, fn = std::move(fn)]() mutable {
    SCOPE_EXIT { ready.post(); };
    // A trick to force the stored functor to be executed and then destructed
    // before posting the baton and waking the waiting thread.
    copy(std::move(fn))();
  });
  ready.wait(folly::Baton<>::wait_options().logging_enabled(false));
}

void EventBase::runImmediatelyOrRunInEventBaseThreadAndWait(Func fn) noexcept {
  if (isInEventBaseThread()) {
    fn();
  } else {
    runInEventBaseThreadAndWait(std::move(fn));
  }
}

void EventBase::runImmediatelyOrRunInEventBaseThread(Func fn) noexcept {
  if (isInEventBaseThread()) {
    fn();
  } else {
    runInEventBaseThreadAlwaysEnqueue(std::move(fn));
  }
}

void EventBase::runLoopCallbacks(LoopCallbackList& currentCallbacks) {
  while (!currentCallbacks.empty()) {
    LoopCallback* callback = &currentCallbacks.front();
    currentCallbacks.pop_front();
    folly::RequestContextScopeGuard rctx(std::move(callback->context_));
    ExecutionObserverScopeGuard guard(&executionObserverList_, callback);
    callback->runLoopCallback();
  }
}

bool EventBase::runLoopCallbacks() {
  bumpHandlingTime();
  if (!loopCallbacks_.empty()) {
    // Swap the loopCallbacks_ list with a temporary list on our stack.
    // This way we will only run callbacks scheduled at the time
    // runLoopCallbacks() was invoked.
    //
    // If any of these callbacks in turn call runInLoop() to schedule more
    // callbacks, those new callbacks won't be run until the next iteration
    // around the event loop.  This prevents runInLoop() callbacks from being
    // able to start file descriptor and timeout based events.
    LoopCallbackList currentCallbacks;
    currentCallbacks.swap(loopCallbacks_);
    runOnceCallbacks_ = &currentCallbacks;

    runLoopCallbacks(currentCallbacks);

    runOnceCallbacks_ = nullptr;
    return true;
  }
  return false;
}

void EventBase::initNotificationQueue() {
  // Infinite size queue
  queue_ =
      std::make_unique<EventBaseAtomicNotificationQueue<Func, FuncRunner>>();

  // Mark this as an internal event, so event_base_loop() will return if
  // there are no other events besides this one installed.
  //
  // Most callers don't care about the internal notification queue used by
  // EventBase.  The queue is always installed, so if we did count the queue as
  // an active event, loop() would never exit with no more events to process.
  // Users can use loopForever() if they do care about the notification queue.
  // (This is useful for EventBase threads that do nothing but process
  // runInEventBaseThread() notifications.)
  queue_->startConsumingInternal(this);
}

void EventBase::SmoothLoopTime::setTimeInterval(
    std::chrono::microseconds timeInterval) {
  expCoeff_ = -1.0 / static_cast<double>(timeInterval.count());
  VLOG(11) << "expCoeff_ " << expCoeff_ << " " << __PRETTY_FUNCTION__;
}

void EventBase::SmoothLoopTime::reset(double value) {
  value_ = value;
}

void EventBase::SmoothLoopTime::addSample(
    std::chrono::microseconds total, std::chrono::microseconds busy) {
  if ((buffer_time_ + total) > buffer_interval_ && buffer_cnt_ > 0) {
    // See https://en.wikipedia.org/wiki/Exponential_smoothing for
    // more info on this calculation.
    double coeff = exp(static_cast<double>(buffer_time_.count()) * expCoeff_);
    value_ = value_ * coeff +
        (1.0 - coeff) *
            (static_cast<double>(busy_buffer_.count()) / buffer_cnt_);
    buffer_time_ = std::chrono::microseconds{0};
    busy_buffer_ = std::chrono::microseconds{0};
    buffer_cnt_ = 0;
  }
  buffer_time_ += total;
  busy_buffer_ += busy;
  buffer_cnt_++;
}

bool EventBase::nothingHandledYet() const noexcept {
  VLOG(11) << "latest " << latestLoopCnt_ << " next " << nextLoopCnt_;
  return (nextLoopCnt_ != latestLoopCnt_);
}

void EventBase::attachTimeoutManager(AsyncTimeout* obj, InternalEnum internal) {
  auto* ev = obj->getEvent();
  assert(ev->eb_ev_base() == nullptr);

  ev->eb_event_base_set(this);
  if (internal == AsyncTimeout::InternalEnum::INTERNAL) {
    // Set the EVLIST_INTERNAL flag
    event_ref_flags(ev->getEvent()) |= EVLIST_INTERNAL;
  }
}

void EventBase::detachTimeoutManager(AsyncTimeout* obj) {
  cancelTimeout(obj);
  auto* ev = obj->getEvent();
  ev->eb_ev_base(nullptr);
}

bool EventBase::scheduleTimeout(
    AsyncTimeout* obj, TimeoutManager::timeout_type timeout) {
  dcheckIsInEventBaseThread();
  // Set up the timeval and add the event
  struct timeval tv;
  tv.tv_sec = to_narrow(timeout.count() / 1000LL);
  tv.tv_usec = to_narrow((timeout.count() % 1000LL) * 1000LL);

  auto* ev = obj->getEvent();

  DCHECK(ev->eb_ev_base());

  if (ev->eb_event_add(&tv) < 0) {
    LOG(ERROR) << "EventBase: failed to schedule timeout: " << errnoStr(errno);
    return false;
  }

  return true;
}

void EventBase::cancelTimeout(AsyncTimeout* obj) {
  dcheckIsInEventBaseThread();
  auto* ev = obj->getEvent();
  if (ev->isEventRegistered()) {
    ev->eb_event_del();
  }
}

void EventBase::setName(const std::string& name) {
  dcheckIsInEventBaseThread();
  name_ = name;

  if (isRunning()) {
    setThreadName(loopThread_.load(std::memory_order_relaxed), name_);
  }
}

const std::string& EventBase::getName() {
  dcheckIsInEventBaseThread();
  return name_;
}

std::thread::id EventBase::getLoopThreadId() {
  return loopThread_.load(std::memory_order_relaxed);
}

void EventBase::scheduleAt(Func&& fn, TimePoint const& timeout) {
  auto duration = timeout - now();
  timer().scheduleTimeoutFn(
      std::move(fn),
      std::chrono::duration_cast<std::chrono::milliseconds>(duration));
}

event_base* EventBase::getLibeventBase() const {
  return evb_ ? (evb_->getEventBase()) : nullptr;
}

const char* EventBase::getLibeventVersion() {
  return event_get_version();
}
const char* EventBase::getLibeventMethod() {
  // event_base_method() would segv if there is no current_base so simulate it
  struct op {
    const char* name;
  };
  struct base {
    const op* evsel;
  };
  auto b = reinterpret_cast<base*>(getLibeventBase());
  return !b ? "" : b->evsel->name;
}

VirtualEventBase& EventBase::getVirtualEventBase() {
  folly::call_once(virtualEventBaseInitFlag_, [&] {
    virtualEventBase_ = std::make_unique<VirtualEventBase>(*this);
  });

  return *virtualEventBase_;
}

VirtualEventBase* EventBase::tryGetVirtualEventBase() {
  if (folly::test_once(virtualEventBaseInitFlag_)) {
    return virtualEventBase_.get();
  }
  return nullptr;
}

EventBase* EventBase::getEventBase() {
  return this;
}

WorkerProvider* EventBase::getThreadIdCollector() {
  return threadIdCollector_.get();
}

EventBase::OnDestructionCallback::~OnDestructionCallback() {
  if (*scheduled_.rlock()) {
    LOG(FATAL)
        << "OnDestructionCallback must be canceled if needed prior to destruction";
  }
}

void EventBase::OnDestructionCallback::runCallback() noexcept {
  scheduled_.withWLock([&](bool& scheduled) {
    CHECK(scheduled);
    scheduled = false;

    // run can only be called by EventBase and VirtualEventBase, and it's called
    // after the callback has been popped off the list.
    eraser_ = nullptr;

    // Note that the exclusive lock on shared state is held while the callback
    // runs. This ensures concurrent callers to cancel() block until the
    // callback finishes.
    onEventBaseDestruction();
  });
}

void EventBase::OnDestructionCallback::schedule(
    Function<void(OnDestructionCallback&)> linker,
    Function<void(OnDestructionCallback&)> eraser) {
  eraser_ = std::move(eraser);
  scheduled_.withWLock([](bool& scheduled) { scheduled = true; });
  linker(*this);
}

bool EventBase::OnDestructionCallback::cancel() {
  return scheduled_.withWLock([this](bool& scheduled) {
    const bool wasScheduled = std::exchange(scheduled, false);
    if (wasScheduled) {
      auto eraser = std::move(eraser_);
      CHECK(eraser);
      eraser(*this);
    }
    return wasScheduled;
  });
}

} // namespace folly
