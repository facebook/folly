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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <folly/io/async/EventBase.h>

#include <folly/ThreadName.h>
#include <folly/io/async/NotificationQueue.h>

#include <boost/static_assert.hpp>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

namespace {

using folly::Cob;
using folly::EventBase;

template <typename Callback>
class FunctionLoopCallback : public EventBase::LoopCallback {
 public:
  explicit FunctionLoopCallback(Cob&& function)
      : function_(std::move(function)) {}

  explicit FunctionLoopCallback(const Cob& function)
      : function_(function) {}

  virtual void runLoopCallback() noexcept {
    function_();
    delete this;
  }

 private:
  Callback function_;
};

}

namespace folly {

const int kNoFD = -1;

/*
 * EventBase::FunctionRunner
 */

class EventBase::FunctionRunner
    : public NotificationQueue<std::pair<void (*)(void*), void*>>::Consumer {
 public:
  void messageAvailable(std::pair<void (*)(void*), void*>&& msg) {

    // In libevent2, internal events do not break the loop.
    // Most users would expect loop(), followed by runInEventBaseThread(),
    // to break the loop and check if it should exit or not.
    // To have similar bejaviour to libevent1.4, tell the loop to break here.
    // Note that loop() may still continue to loop, but it will also check the
    // stop_ flag as well as runInLoop callbacks, etc.
    event_base_loopbreak(getEventBase()->evb_);

    if (msg.first == nullptr && msg.second == nullptr) {
      // terminateLoopSoon() sends a null message just to
      // wake up the loop.  We can ignore these messages.
      return;
    }

    // If function is nullptr, just log and move on
    if (!msg.first) {
      LOG(ERROR) << "nullptr callback registered to be run in "
                 << "event base thread";
      return;
    }

    // The function should never throw an exception, because we have no
    // way of knowing what sort of error handling to perform.
    //
    // If it does throw, log a message and abort the program.
    try {
      msg.first(msg.second);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "runInEventBaseThread() function threw a "
                 << typeid(ex).name() << " exception: " << ex.what();
      abort();
    } catch (...) {
      LOG(ERROR) << "runInEventBaseThread() function threw an exception";
      abort();
    }
  }
};

/*
 * EventBase::CobTimeout methods
 */

void EventBase::CobTimeout::timeoutExpired() noexcept {
  // For now, we just swallow any exceptions that the callback threw.
  try {
    cob_();
  } catch (const std::exception& ex) {
    LOG(ERROR) << "EventBase::runAfterDelay() callback threw "
               << typeid(ex).name() << " exception: " << ex.what();
  } catch (...) {
    LOG(ERROR) << "EventBase::runAfterDelay() callback threw non-exception "
               << "type";
  }

  // The CobTimeout object was allocated on the heap by runAfterDelay(),
  // so delete it now that the it has fired.
  delete this;
}

/*
 * EventBase methods
 */

EventBase::EventBase()
  : runOnceCallbacks_(nullptr)
  , stop_(false)
  , loopThread_(0)
  , evb_(static_cast<event_base*>(event_init()))
  , queue_(nullptr)
  , fnRunner_(nullptr)
  , maxLatency_(0)
  , avgLoopTime_(2000000)
  , maxLatencyLoopTime_(avgLoopTime_)
  , nextLoopCnt_(-40)       // Early wrap-around so bugs will manifest soon
  , latestLoopCnt_(nextLoopCnt_)
  , startWork_(0)
  , observer_(nullptr)
  , observerSampleCount_(0) {
  if (UNLIKELY(evb_ == nullptr)) {
    LOG(ERROR) << "EventBase(): Failed to init event base.";
    folly::throwSystemError("error in EventBase::EventBase()");
  }
  VLOG(5) << "EventBase(): Created.";
  initNotificationQueue();
  RequestContext::getStaticContext();
}

// takes ownership of the event_base
EventBase::EventBase(event_base* evb)
  : runOnceCallbacks_(nullptr)
  , stop_(false)
  , loopThread_(0)
  , evb_(evb)
  , queue_(nullptr)
  , fnRunner_(nullptr)
  , maxLatency_(0)
  , avgLoopTime_(2000000)
  , maxLatencyLoopTime_(avgLoopTime_)
  , nextLoopCnt_(-40)       // Early wrap-around so bugs will manifest soon
  , latestLoopCnt_(nextLoopCnt_)
  , startWork_(0)
  , observer_(nullptr)
  , observerSampleCount_(0) {
  if (UNLIKELY(evb_ == nullptr)) {
    LOG(ERROR) << "EventBase(): Pass nullptr as event base.";
    throw std::invalid_argument("EventBase(): event base cannot be nullptr");
  }
  initNotificationQueue();
  RequestContext::getStaticContext();
}

EventBase::~EventBase() {
  // Call all destruction callbacks, before we start cleaning up our state.
  while (!onDestructionCallbacks_.empty()) {
    LoopCallback* callback = &onDestructionCallbacks_.front();
    onDestructionCallbacks_.pop_front();
    callback->runLoopCallback();
  }

  // Delete any unfired CobTimeout objects, so that we don't leak memory
  // (Note that we don't fire them.  The caller is responsible for cleaning up
  // its own data structures if it destroys the EventBase with unfired events
  // remaining.)
  while (!pendingCobTimeouts_.empty()) {
    CobTimeout* timeout = &pendingCobTimeouts_.front();
    delete timeout;
  }

  (void) runLoopCallbacks(false);

  // Stop consumer before deleting NotificationQueue
  fnRunner_->stopConsuming();
  event_base_free(evb_);
  VLOG(5) << "EventBase(): Destroyed.";
}

int EventBase::getNotificationQueueSize() const {
  return queue_->size();
}

void EventBase::setMaxReadAtOnce(uint32_t maxAtOnce) {
  fnRunner_->setMaxReadAtOnce(maxAtOnce);
}

// Set smoothing coefficient for loop load average; input is # of milliseconds
// for exp(-1) decay.
void EventBase::setLoadAvgMsec(uint32_t ms) {
  uint64_t us = 1000 * ms;
  if (ms > 0) {
    maxLatencyLoopTime_.setTimeInterval(us);
    avgLoopTime_.setTimeInterval(us);
  } else {
    LOG(ERROR) << "non-positive arg to setLoadAvgMsec()";
  }
}

void EventBase::resetLoadAvg(double value) {
  avgLoopTime_.reset(value);
  maxLatencyLoopTime_.reset(value);
}

static std::chrono::milliseconds
getTimeDelta(std::chrono::steady_clock::time_point* prev) {
  auto result = std::chrono::steady_clock::now() - *prev;
  *prev = std::chrono::steady_clock::now();

  return std::chrono::duration_cast<std::chrono::milliseconds>(result);
}

void EventBase::waitUntilRunning() {
  while (!isRunning()) {
    sched_yield();
  }
}

// enters the event_base loop -- will only exit when forced to
bool EventBase::loop() {
  return loopBody();
}

bool EventBase::loopOnce(int flags) {
  return loopBody(flags | EVLOOP_ONCE);
}

bool EventBase::loopBody(int flags) {
  VLOG(5) << "EventBase(): Starting loop.";
  int res = 0;
  bool ranLoopCallbacks;
  bool blocking = !(flags & EVLOOP_NONBLOCK);
  bool once = (flags & EVLOOP_ONCE);

  loopThread_.store(pthread_self(), std::memory_order_release);

  if (!name_.empty()) {
    setThreadName(name_);
  }

  auto prev = std::chrono::steady_clock::now();
  int64_t idleStart = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();

  // TODO: Read stop_ atomically with an acquire barrier.
  while (!stop_) {
    ++nextLoopCnt_;

    // nobody can add loop callbacks from within this thread if
    // we don't have to handle anything to start with...
    if (blocking && loopCallbacks_.empty()) {
      res = event_base_loop(evb_, EVLOOP_ONCE);
    } else {
      res = event_base_loop(evb_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }
    ranLoopCallbacks = runLoopCallbacks();

    int64_t busy = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count() - startWork_;
    int64_t idle = startWork_ - idleStart;

    avgLoopTime_.addSample(idle, busy);
    maxLatencyLoopTime_.addSample(idle, busy);

    if (observer_) {
      if (observerSampleCount_++ == observer_->getSampleRate()) {
        observerSampleCount_ = 0;
        observer_->loopSample(busy, idle);
      }
    }

    VLOG(11) << "EventBase " << this         << " did not timeout "
     " loop time guess: "    << busy + idle  <<
     " idle time: "          << idle         <<
     " busy time: "          << busy         <<
     " avgLoopTime: "        << avgLoopTime_.get() <<
     " maxLatencyLoopTime: " << maxLatencyLoopTime_.get() <<
     " maxLatency_: "        << maxLatency_ <<
     " nothingHandledYet(): "<< nothingHandledYet();

    // see if our average loop time has exceeded our limit
    if ((maxLatency_ > 0) &&
        (maxLatencyLoopTime_.get() > double(maxLatency_))) {
      maxLatencyCob_();
      // back off temporarily -- don't keep spamming maxLatencyCob_
      // if we're only a bit over the limit
      maxLatencyLoopTime_.dampen(0.9);
    }

    // Our loop run did real work; reset the idle timer
    idleStart = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

    // If the event loop indicate that there were no more events, and
    // we also didn't have any loop callbacks to run, there is nothing left to
    // do.
    if (res != 0 && !ranLoopCallbacks) {
      // Since Notification Queue is marked 'internal' some events may not have
      // run.  Run them manually if so, and continue looping.
      //
      if (getNotificationQueueSize() > 0) {
        fnRunner_->handlerReady(0);
      } else {
        break;
      }
    }

    VLOG(5) << "EventBase " << this << " loop time: " <<
      getTimeDelta(&prev).count();

    if (once) {
      break;
    }
  }
  // Reset stop_ so loop() can be called again
  stop_ = false;

  if (res < 0) {
    LOG(ERROR) << "EventBase: -- error in event loop, res = " << res;
    return false;
  } else if (res == 1) {
    VLOG(5) << "EventBase: ran out of events (exiting loop)!";
  } else if (res > 1) {
    LOG(ERROR) << "EventBase: unknown event loop result = " << res;
    return false;
  }

  loopThread_.store(0, std::memory_order_release);

  VLOG(5) << "EventBase(): Done with loop.";
  return true;
}

void EventBase::loopForever() {
  // Update the notification queue event to treat it as a normal (non-internal)
  // event.  The notification queue event always remains installed, and the main
  // loop won't exit with it installed.
  fnRunner_->stopConsuming();
  fnRunner_->startConsuming(this, queue_.get());

  bool ret = loop();

  // Restore the notification queue internal flag
  fnRunner_->stopConsuming();
  fnRunner_->startConsumingInternal(this, queue_.get());

  if (!ret) {
    folly::throwSystemError("error in EventBase::loopForever()");
  }
}

bool EventBase::bumpHandlingTime() {
  VLOG(11) << "EventBase " << this << " " << __PRETTY_FUNCTION__ <<
    " (loop) latest " << latestLoopCnt_ << " next " << nextLoopCnt_;
  if(nothingHandledYet()) {
    latestLoopCnt_ = nextLoopCnt_;
    // set the time
    startWork_ = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

    VLOG(11) << "EventBase " << this << " " << __PRETTY_FUNCTION__ <<
      " (loop) startWork_ " << startWork_;
    return true;
  }
  return false;
}

void EventBase::terminateLoopSoon() {
  VLOG(5) << "EventBase(): Received terminateLoopSoon() command.";

  if (!isRunning()) {
    return;
  }

  // Set stop to true, so the event loop will know to exit.
  // TODO: We should really use an atomic operation here with a release
  // barrier.
  stop_ = true;

  // Call event_base_loopbreak() so that libevent will exit the next time
  // around the loop.
  event_base_loopbreak(evb_);

  // If terminateLoopSoon() is called from another thread,
  // the EventBase thread might be stuck waiting for events.
  // In this case, it won't wake up and notice that stop_ is set until it
  // receives another event.  Send an empty frame to the notification queue
  // so that the event loop will wake up even if there are no other events.
  //
  // We don't care about the return value of trySendFrame().  If it fails
  // this likely means the EventBase already has lots of events waiting
  // anyway.
  try {
    queue_->putMessage(std::make_pair(nullptr, nullptr));
  } catch (...) {
    // We don't care if putMessage() fails.  This likely means
    // the EventBase already has lots of events waiting anyway.
  }
}

void EventBase::runInLoop(LoopCallback* callback, bool thisIteration) {
  DCHECK(isInEventBaseThread());
  callback->cancelLoopCallback();
  callback->context_ = RequestContext::saveContext();
  if (runOnceCallbacks_ != nullptr && thisIteration) {
    runOnceCallbacks_->push_back(*callback);
  } else {
    loopCallbacks_.push_back(*callback);
  }
}

void EventBase::runInLoop(const Cob& cob, bool thisIteration) {
  DCHECK(isInEventBaseThread());
  auto wrapper = new FunctionLoopCallback<Cob>(cob);
  wrapper->context_ = RequestContext::saveContext();
  if (runOnceCallbacks_ != nullptr && thisIteration) {
    runOnceCallbacks_->push_back(*wrapper);
  } else {
    loopCallbacks_.push_back(*wrapper);
  }
}

void EventBase::runInLoop(Cob&& cob, bool thisIteration) {
  DCHECK(isInEventBaseThread());
  auto wrapper = new FunctionLoopCallback<Cob>(std::move(cob));
  wrapper->context_ = RequestContext::saveContext();
  if (runOnceCallbacks_ != nullptr && thisIteration) {
    runOnceCallbacks_->push_back(*wrapper);
  } else {
    loopCallbacks_.push_back(*wrapper);
  }
}

void EventBase::runOnDestruction(LoopCallback* callback) {
  DCHECK(isInEventBaseThread());
  callback->cancelLoopCallback();
  onDestructionCallbacks_.push_back(*callback);
}

bool EventBase::runInEventBaseThread(void (*fn)(void*), void* arg) {
  // Send the message.
  // It will be received by the FunctionRunner in the EventBase's thread.

  // We try not to schedule nullptr callbacks
  if (!fn) {
    LOG(ERROR) << "EventBase " << this
               << ": Scheduling nullptr callbacks is not allowed";
    return false;
  }

  // Short-circuit if we are already in our event base
  if (inRunningEventBaseThread()) {
    runInLoop(new RunInLoopCallback(fn, arg));
    return true;

  }

  try {
    queue_->putMessage(std::make_pair(fn, arg));
  } catch (const std::exception& ex) {
    LOG(ERROR) << "EventBase " << this << ": failed to schedule function "
               << fn << "for EventBase thread: " << ex.what();
    return false;
  }

  return true;
}

bool EventBase::runInEventBaseThread(const Cob& fn) {
  // Short-circuit if we are already in our event base
  if (inRunningEventBaseThread()) {
    runInLoop(fn);
    return true;
  }

  Cob* fnCopy;
  // Allocate a copy of the function so we can pass it to the other thread
  // The other thread will delete this copy once the function has been run
  try {
    fnCopy = new Cob(fn);
  } catch (const std::bad_alloc& ex) {
    LOG(ERROR) << "failed to allocate tr::function copy "
               << "for runInEventBaseThread()";
    return false;
  }

  if (!runInEventBaseThread(&EventBase::runFunctionPtr, fnCopy)) {
    delete fnCopy;
    return false;
  }

  return true;
}

bool EventBase::runAfterDelay(const Cob& cob,
                               int milliseconds,
                               TimeoutManager::InternalEnum in) {
  CobTimeout* timeout = new CobTimeout(this, cob, in);
  if (!timeout->scheduleTimeout(milliseconds)) {
    delete timeout;
    return false;
  }

  pendingCobTimeouts_.push_back(*timeout);
  return true;
}

bool EventBase::runLoopCallbacks(bool setContext) {
  if (!loopCallbacks_.empty()) {
    bumpHandlingTime();
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

    while (!currentCallbacks.empty()) {
      LoopCallback* callback = &currentCallbacks.front();
      currentCallbacks.pop_front();
      if (setContext) {
        RequestContext::setContext(callback->context_);
      }
      callback->runLoopCallback();
    }

    runOnceCallbacks_ = nullptr;
    return true;
  }
  return false;
}

void EventBase::initNotificationQueue() {
  // Infinite size queue
  queue_.reset(new NotificationQueue<std::pair<void (*)(void*), void*>>());

  // We allocate fnRunner_ separately, rather than declaring it directly
  // as a member of EventBase solely so that we don't need to include
  // NotificationQueue.h from EventBase.h
  fnRunner_.reset(new FunctionRunner());

  // Mark this as an internal event, so event_base_loop() will return if
  // there are no other events besides this one installed.
  //
  // Most callers don't care about the internal notification queue used by
  // EventBase.  The queue is always installed, so if we did count the queue as
  // an active event, loop() would never exit with no more events to process.
  // Users can use loopForever() if they do care about the notification queue.
  // (This is useful for EventBase threads that do nothing but process
  // runInEventBaseThread() notifications.)
  fnRunner_->startConsumingInternal(this, queue_.get());
}

void EventBase::SmoothLoopTime::setTimeInterval(uint64_t timeInterval) {
  expCoeff_ = -1.0/timeInterval;
  VLOG(11) << "expCoeff_ " << expCoeff_ << " " << __PRETTY_FUNCTION__;
}

void EventBase::SmoothLoopTime::reset(double value) {
  value_ = value;
}

void EventBase::SmoothLoopTime::addSample(int64_t idle, int64_t busy) {
    /*
     * Position at which the busy sample is considered to be taken.
     * (Allows to quickly skew our average without editing much code)
     */
    enum BusySamplePosition {
      RIGHT = 0,  // busy sample placed at the end of the iteration
      CENTER = 1, // busy sample placed at the middle point of the iteration
      LEFT = 2,   // busy sample placed at the beginning of the iteration
    };

  VLOG(11) << "idle " << idle << " oldBusyLeftover_ " << oldBusyLeftover_ <<
              " idle + oldBusyLeftover_ " << idle + oldBusyLeftover_ <<
              " busy " << busy << " " << __PRETTY_FUNCTION__;
  idle += oldBusyLeftover_ + busy;
  oldBusyLeftover_ = (busy * BusySamplePosition::CENTER) / 2;
  idle -= oldBusyLeftover_;

  double coeff = exp(idle * expCoeff_);
  value_ *= coeff;
  value_ += (1.0 - coeff) * busy;
}

bool EventBase::nothingHandledYet() {
  VLOG(11) << "latest " << latestLoopCnt_ << " next " << nextLoopCnt_;
  return (nextLoopCnt_ != latestLoopCnt_);
}

/* static */
void EventBase::runFunctionPtr(Cob* fn) {
  // The function should never throw an exception, because we have no
  // way of knowing what sort of error handling to perform.
  //
  // If it does throw, log a message and abort the program.
  try {
    (*fn)();
  } catch (const std::exception &ex) {
    LOG(ERROR) << "runInEventBaseThread() std::function threw a "
               << typeid(ex).name() << " exception: " << ex.what();
    abort();
  } catch (...) {
    LOG(ERROR) << "runInEventBaseThread() std::function threw an exception";
    abort();
  }

  // The function object was allocated by runInEventBaseThread().
  // Delete it once it has been run.
  delete fn;
}

EventBase::RunInLoopCallback::RunInLoopCallback(void (*fn)(void*), void* arg)
    : fn_(fn)
    , arg_(arg) {}

void EventBase::RunInLoopCallback::runLoopCallback() noexcept {
  fn_(arg_);
  delete this;
}

void EventBase::attachTimeoutManager(AsyncTimeout* obj,
                                      InternalEnum internal) {

  struct event* ev = obj->getEvent();
  assert(ev->ev_base == nullptr);

  event_base_set(getLibeventBase(), ev);
  if (internal == AsyncTimeout::InternalEnum::INTERNAL) {
    // Set the EVLIST_INTERNAL flag
    ev->ev_flags |= EVLIST_INTERNAL;
  }
}

void EventBase::detachTimeoutManager(AsyncTimeout* obj) {
  cancelTimeout(obj);
  struct event* ev = obj->getEvent();
  ev->ev_base = nullptr;
}

bool EventBase::scheduleTimeout(AsyncTimeout* obj,
                                 std::chrono::milliseconds timeout) {
  assert(isInEventBaseThread());
  // Set up the timeval and add the event
  struct timeval tv;
  tv.tv_sec = timeout.count() / 1000LL;
  tv.tv_usec = (timeout.count() % 1000LL) * 1000LL;

  struct event* ev = obj->getEvent();
  if (event_add(ev, &tv) < 0) {
    LOG(ERROR) << "EventBase: failed to schedule timeout: " << strerror(errno);
    return false;
  }

  return true;
}

void EventBase::cancelTimeout(AsyncTimeout* obj) {
  assert(isInEventBaseThread());
  struct event* ev = obj->getEvent();
  if (EventUtil::isEventRegistered(ev)) {
    event_del(ev);
  }
}

void EventBase::setName(const std::string& name) {
  assert(isInEventBaseThread());
  name_ = name;

  if (isRunning()) {
    setThreadName(loopThread_.load(std::memory_order_relaxed),
                  name_);
  }
}

const std::string& EventBase::getName() {
  assert(isInEventBaseThread());
  return name_;
}

} // folly
