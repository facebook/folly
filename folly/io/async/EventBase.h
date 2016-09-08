/*
 * Copyright 2016 Facebook, Inc.
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

#pragma once

#include <atomic>
#include <cstdlib>
#include <errno.h>
#include <functional>
#include <list>
#include <math.h>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/intrusive/list.hpp>
#include <boost/utility.hpp>

#include <folly/Executor.h>
#include <folly/Function.h>
#include <folly/Portability.h>
#include <folly/experimental/ExecutionObserver.h>
#include <folly/futures/DrivableExecutor.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/Request.h>
#include <folly/io/async/TimeoutManager.h>
#include <folly/portability/PThread.h>
#include <glog/logging.h>

#include <event.h>  // libevent

namespace folly {

using Cob = Func; // defined in folly/Executor.h
template <typename MessageT>
class NotificationQueue;

namespace detail {
class EventBaseLocalBase;

class EventBaseLocalBaseBase {
 public:
  virtual void onEventBaseDestruction(EventBase& evb) = 0;
  virtual ~EventBaseLocalBaseBase() = default;
};
}
template <typename T>
class EventBaseLocal;

class EventBaseObserver {
 public:
  virtual ~EventBaseObserver() = default;

  virtual uint32_t getSampleRate() const = 0;

  virtual void loopSample(
    int64_t busyTime, int64_t idleTime) = 0;
};

// Helper class that sets and retrieves the EventBase associated with a given
// request via RequestContext. See Request.h for that mechanism.
class RequestEventBase : public RequestData {
 public:
  static EventBase* get() {
    auto data = dynamic_cast<RequestEventBase*>(
        RequestContext::get()->getContextData(kContextDataName));
    if (!data) {
      return nullptr;
    }
    return data->eb_;
  }

  static void set(EventBase* eb) {
    RequestContext::get()->setContextData(
        kContextDataName,
        std::unique_ptr<RequestEventBase>(new RequestEventBase(eb)));
  }

 private:
  explicit RequestEventBase(EventBase* eb) : eb_(eb) {}
  EventBase* eb_;
  static constexpr const char* kContextDataName{"EventBase"};
};

/**
 * This class is a wrapper for all asynchronous I/O processing functionality
 *
 * EventBase provides a main loop that notifies EventHandler callback objects
 * when I/O is ready on a file descriptor, and notifies AsyncTimeout objects
 * when a specified timeout has expired.  More complex, higher-level callback
 * mechanisms can then be built on top of EventHandler and AsyncTimeout.
 *
 * A EventBase object can only drive an event loop for a single thread.  To
 * take advantage of multiple CPU cores, most asynchronous I/O servers have one
 * thread per CPU, and use a separate EventBase for each thread.
 *
 * In general, most EventBase methods may only be called from the thread
 * running the EventBase's loop.  There are a few exceptions to this rule, for
 * methods that are explicitly intended to allow communication with a
 * EventBase from other threads.  When it is safe to call a method from
 * another thread it is explicitly listed in the method comments.
 */
class EventBase : private boost::noncopyable,
                  public TimeoutManager,
                  public DrivableExecutor {
 public:
  using Func = folly::Function<void()>;

  /**
   * A callback interface to use with runInLoop()
   *
   * Derive from this class if you need to delay some code execution until the
   * next iteration of the event loop.  This allows you to schedule code to be
   * invoked from the top-level of the loop, after your immediate callers have
   * returned.
   *
   * If a LoopCallback object is destroyed while it is scheduled to be run in
   * the next loop iteration, it will automatically be cancelled.
   */
  class LoopCallback
      : public boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
   public:
    virtual ~LoopCallback() = default;

    virtual void runLoopCallback() noexcept = 0;
    void cancelLoopCallback() {
      unlink();
    }

    bool isLoopCallbackScheduled() const {
      return is_linked();
    }

   private:
    typedef boost::intrusive::list<
      LoopCallback,
      boost::intrusive::constant_time_size<false> > List;

    // EventBase needs access to LoopCallbackList (and therefore to hook_)
    friend class EventBase;
    std::shared_ptr<RequestContext> context_;
  };

  /**
   * Create a new EventBase object.
   *
   * @param enableTimeMeasurement Informs whether this event base should measure
   *                              time. Disabling it would likely improve
   *                              performance, but will disable some features
   *                              that relies on time-measurement, including:
   *                              observer, max latency and avg loop time.
   */
  explicit EventBase(bool enableTimeMeasurement = true);

  /**
   * Create a new EventBase object that will use the specified libevent
   * event_base object to drive the event loop.
   *
   * The EventBase will take ownership of this event_base, and will call
   * event_base_free(evb) when the EventBase is destroyed.
   *
   * @param enableTimeMeasurement Informs whether this event base should measure
   *                              time. Disabling it would likely improve
   *                              performance, but will disable some features
   *                              that relies on time-measurement, including:
   *                              observer, max latency and avg loop time.
   */
  explicit EventBase(event_base* evb, bool enableTimeMeasurement = true);
  ~EventBase();

  /**
   * Runs the event loop.
   *
   * loop() will loop waiting for I/O or timeouts and invoking EventHandler
   * and AsyncTimeout callbacks as their events become ready.  loop() will
   * only return when there are no more events remaining to process, or after
   * terminateLoopSoon() has been called.
   *
   * loop() may be called again to restart event processing after a previous
   * call to loop() or loopForever() has returned.
   *
   * Returns true if the loop completed normally (if it processed all
   * outstanding requests, or if terminateLoopSoon() was called).  If an error
   * occurs waiting for events, false will be returned.
   */
  bool loop();

  /**
   * Wait for some events to become active, run them, then return.
   *
   * When EVLOOP_NONBLOCK is set in flags, the loop won't block if there
   * are not any events to process.
   *
   * This is useful for callers that want to run the loop manually.
   *
   * Returns the same result as loop().
   */
  bool loopOnce(int flags = 0);

  /**
   * Runs the event loop.
   *
   * loopForever() behaves like loop(), except that it keeps running even if
   * when there are no more user-supplied EventHandlers or AsyncTimeouts
   * registered.  It will only return after terminateLoopSoon() has been
   * called.
   *
   * This is useful for callers that want to wait for other threads to call
   * runInEventBaseThread(), even when there are no other scheduled events.
   *
   * loopForever() may be called again to restart event processing after a
   * previous call to loop() or loopForever() has returned.
   *
   * Throws a std::system_error if an error occurs.
   */
  void loopForever();

  /**
   * Causes the event loop to exit soon.
   *
   * This will cause an existing call to loop() or loopForever() to stop event
   * processing and return, even if there are still events remaining to be
   * processed.
   *
   * It is safe to call terminateLoopSoon() from another thread to cause loop()
   * to wake up and return in the EventBase loop thread.  terminateLoopSoon()
   * may also be called from the loop thread itself (for example, a
   * EventHandler or AsyncTimeout callback may call terminateLoopSoon() to
   * cause the loop to exit after the callback returns.)  If the loop is not
   * running, this will cause the next call to loop to terminate soon after
   * starting.  If a loop runs out of work (and so terminates on its own)
   * concurrently with a call to terminateLoopSoon(), this may cause a race
   * condition.
   *
   * Note that the caller is responsible for ensuring that cleanup of all event
   * callbacks occurs properly.  Since terminateLoopSoon() causes the loop to
   * exit even when there are pending events present, there may be remaining
   * callbacks present waiting to be invoked.  If the loop is later restarted
   * pending events will continue to be processed normally, however if the
   * EventBase is destroyed after calling terminateLoopSoon() it is the
   * caller's responsibility to ensure that cleanup happens properly even if
   * some outstanding events are never processed.
   */
  void terminateLoopSoon();

  /**
   * Adds the given callback to a queue of things run after the current pass
   * through the event loop completes.  Note that if this callback calls
   * runInLoop() the new callback won't be called until the main event loop
   * has gone through a cycle.
   *
   * This method may only be called from the EventBase's thread.  This
   * essentially allows an event handler to schedule an additional callback to
   * be invoked after it returns.
   *
   * Use runInEventBaseThread() to schedule functions from another thread.
   *
   * The thisIteration parameter makes this callback run in this loop
   * iteration, instead of the next one, even if called from a
   * runInLoop callback (normal io callbacks that call runInLoop will
   * always run in this iteration).  This was originally added to
   * support detachEventBase, as a user callback may have called
   * terminateLoopSoon(), but we want to make sure we detach.  Also,
   * detachEventBase almost always must be called from the base event
   * loop to ensure the stack is unwound, since most users of
   * EventBase are not thread safe.
   *
   * Ideally we would not need thisIteration, and instead just use
   * runInLoop with loop() (instead of terminateLoopSoon).
   */
  void runInLoop(LoopCallback* callback, bool thisIteration = false);

  /**
   * Convenience function to call runInLoop() with a folly::Function.
   *
   * This creates a LoopCallback object to wrap the folly::Function, and invoke
   * the folly::Function when the loop callback fires.  This is slightly more
   * expensive than defining your own LoopCallback, but more convenient in
   * areas that aren't too performance sensitive.
   *
   * This method may only be called from the EventBase's thread.  This
   * essentially allows an event handler to schedule an additional callback to
   * be invoked after it returns.
   *
   * Use runInEventBaseThread() to schedule functions from another thread.
   */
  void runInLoop(Func c, bool thisIteration = false);

  /**
   * Adds the given callback to a queue of things run before destruction
   * of current EventBase.
   *
   * This allows users of EventBase that run in it, but don't control it,
   * to be notified before EventBase gets destructed.
   *
   * Note: will be called from the thread that invoked EventBase destructor,
   *       before the final run of loop callbacks.
   */
  void runOnDestruction(LoopCallback* callback);

  /**
   * Adds a callback that will run immediately *before* the event loop.
   * This is very similar to runInLoop(), but will not cause the loop to break:
   * For example, this callback could be used to get loop times.
   */
  void runBeforeLoop(LoopCallback* callback);

  /**
   * Run the specified function in the EventBase's thread.
   *
   * This method is thread-safe, and may be called from another thread.
   *
   * If runInEventBaseThread() is called when the EventBase loop is not
   * running, the function call will be delayed until the next time the loop is
   * started.
   *
   * If runInEventBaseThread() returns true the function has successfully been
   * scheduled to run in the loop thread.  However, if the loop is terminated
   * (and never later restarted) before it has a chance to run the requested
   * function, the function will be run upon the EventBase's destruction.
   *
   * If two calls to runInEventBaseThread() are made from the same thread, the
   * functions will always be run in the order that they were scheduled.
   * Ordering between functions scheduled from separate threads is not
   * guaranteed.
   *
   * @param fn  The function to run.  The function must not throw any
   *     exceptions.
   * @param arg An argument to pass to the function.
   *
   * @return Returns true if the function was successfully scheduled, or false
   *         if there was an error scheduling the function.
   */
  template <typename T>
  bool runInEventBaseThread(void (*fn)(T*), T* arg);

  /**
   * Run the specified function in the EventBase's thread
   *
   * This version of runInEventBaseThread() takes a folly::Function object.
   * Note that this may be less efficient than the version that takes a plain
   * function pointer and void* argument, if moving the function is expensive
   * (e.g., if it wraps a lambda which captures some values with expensive move
   * constructors).
   *
   * If the loop is terminated (and never later restarted) before it has a
   * chance to run the requested function, the function will be run upon the
   * EventBase's destruction.
   *
   * The function must not throw any exceptions.
   */
  bool runInEventBaseThread(Func fn);

  /*
   * Like runInEventBaseThread, but the caller waits for the callback to be
   * executed.
   */
  template <typename T>
  bool runInEventBaseThreadAndWait(void (*fn)(T*), T* arg);

  /*
   * Like runInEventBaseThread, but the caller waits for the callback to be
   * executed.
   */
  bool runInEventBaseThreadAndWait(Func fn);

  /*
   * Like runInEventBaseThreadAndWait, except if the caller is already in the
   * event base thread, the functor is simply run inline.
   */
  template <typename T>
  bool runImmediatelyOrRunInEventBaseThreadAndWait(void (*fn)(T*), T* arg);

  /*
   * Like runInEventBaseThreadAndWait, except if the caller is already in the
   * event base thread, the functor is simply run inline.
   */
  bool runImmediatelyOrRunInEventBaseThreadAndWait(Func fn);

  /**
   * Runs the given Cob at some time after the specified number of
   * milliseconds.  (No guarantees exactly when.)
   *
   * Throws a std::system_error if an error occurs.
   */
  void runAfterDelay(
      Func c,
      uint32_t milliseconds,
      TimeoutManager::InternalEnum in = TimeoutManager::InternalEnum::NORMAL);

  /**
   * @see tryRunAfterDelay for more details
   *
   * @return  true iff the cob was successfully registered.
   *
   * */
  bool tryRunAfterDelay(
      Func cob,
      uint32_t milliseconds,
      TimeoutManager::InternalEnum in = TimeoutManager::InternalEnum::NORMAL);

  /**
   * Set the maximum desired latency in us and provide a callback which will be
   * called when that latency is exceeded.
   * OBS: This functionality depends on time-measurement.
   */
  void setMaxLatency(int64_t maxLatency, Func maxLatencyCob) {
    assert(enableTimeMeasurement_);
    maxLatency_ = maxLatency;
    maxLatencyCob_ = std::move(maxLatencyCob);
  }


  /**
   * Set smoothing coefficient for loop load average; # of milliseconds
   * for exp(-1) (1/2.71828...) decay.
   */
  void setLoadAvgMsec(uint32_t ms);

  /**
   * reset the load average to a desired value
   */
  void resetLoadAvg(double value = 0.0);

  /**
   * Get the average loop time in microseconds (an exponentially-smoothed ave)
   */
  double getAvgLoopTime() const {
    assert(enableTimeMeasurement_);
    return avgLoopTime_.get();
  }

  /**
    * check if the event base loop is running.
   */
  bool isRunning() const {
    return loopThread_.load(std::memory_order_relaxed) != 0;
  }

  /**
   * wait until the event loop starts (after starting the event loop thread).
   */
  void waitUntilRunning();

  int getNotificationQueueSize() const;

  void setMaxReadAtOnce(uint32_t maxAtOnce);

  /**
   * Verify that current thread is the EventBase thread, if the EventBase is
   * running.
   */
  bool isInEventBaseThread() const {
    auto tid = loopThread_.load(std::memory_order_relaxed);
    return tid == 0 || pthread_equal(tid, pthread_self());
  }

  bool inRunningEventBaseThread() const {
    return pthread_equal(
      loopThread_.load(std::memory_order_relaxed), pthread_self());
  }

  HHWheelTimer& timer() {
    if (!wheelTimer_) {
      wheelTimer_ = HHWheelTimer::newTimer(this);
    }
    return *wheelTimer_.get();
  }

  // --------- interface to underlying libevent base ------------
  // Avoid using these functions if possible.  These functions are not
  // guaranteed to always be present if we ever provide alternative EventBase
  // implementations that do not use libevent internally.
  event_base* getLibeventBase() const { return evb_; }
  static const char* getLibeventVersion();
  static const char* getLibeventMethod();

  /**
   * only EventHandler/AsyncTimeout subclasses and ourselves should
   * ever call this.
   *
   * This is used to mark the beginning of a new loop cycle by the
   * first handler fired within that cycle.
   *
   */
  void bumpHandlingTime() override final;

  class SmoothLoopTime {
   public:
    explicit SmoothLoopTime(uint64_t timeInterval)
      : expCoeff_(-1.0/timeInterval)
      , value_(0.0)
      , oldBusyLeftover_(0) {
      VLOG(11) << "expCoeff_ " << expCoeff_ << " " << __PRETTY_FUNCTION__;
    }

    void setTimeInterval(uint64_t timeInterval);
    void reset(double value = 0.0);

    void addSample(int64_t idle, int64_t busy);

    double get() const {
      return value_;
    }

    void dampen(double factor) {
      value_ *= factor;
    }

   private:
    double  expCoeff_;
    double  value_;
    int64_t oldBusyLeftover_;
  };

  void setObserver(const std::shared_ptr<EventBaseObserver>& observer) {
    assert(enableTimeMeasurement_);
    observer_ = observer;
  }

  const std::shared_ptr<EventBaseObserver>& getObserver() {
    return observer_;
  }

  /**
   * Setup execution observation/instrumentation for every EventHandler
   * executed in this EventBase.
   *
   * @param executionObserver   EventHandle's execution observer.
   */
  void setExecutionObserver(ExecutionObserver* observer) {
    executionObserver_ = observer;
  }

  /**
   * Gets the execution observer associated with this EventBase.
   */
  ExecutionObserver* getExecutionObserver() {
    return executionObserver_;
  }

  /**
   * Set the name of the thread that runs this event base.
   */
  void setName(const std::string& name);

  /**
   * Returns the name of the thread that runs this event base.
   */
  const std::string& getName();

  /// Implements the Executor interface
  void add(Cob fn) override {
    // runInEventBaseThread() takes a const&,
    // so no point in doing std::move here.
    runInEventBaseThread(std::move(fn));
  }

  /// Implements the DrivableExecutor interface
  void drive() override {
    auto keepAlive = loopKeepAlive();
    loopOnce();
  }

  struct LoopKeepAliveDeleter {
    void operator()(EventBase* evb) {
      DCHECK(evb->isInEventBaseThread());
      evb->loopKeepAliveCount_--;
    }
  };
  using LoopKeepAlive = std::unique_ptr<EventBase, LoopKeepAliveDeleter>;

  /// Returns you a handle which make loop() behave like loopForever() until
  /// destroyed. loop() will return to its original behavior only when all
  /// loop keep-alives are released. Loop holder is safe to release only from
  /// EventBase thread.
  ///
  /// May return no op LoopKeepAlive if loopForever() is already running.
  LoopKeepAlive loopKeepAlive() {
    DCHECK(isInEventBaseThread());
    loopKeepAliveCount_++;
    return LoopKeepAlive(this);
  }

 private:
  // TimeoutManager
  void attachTimeoutManager(AsyncTimeout* obj,
                            TimeoutManager::InternalEnum internal) override;

  void detachTimeoutManager(AsyncTimeout* obj) override;

  bool scheduleTimeout(AsyncTimeout* obj, TimeoutManager::timeout_type timeout)
    override;

  void cancelTimeout(AsyncTimeout* obj) override;

  bool isInTimeoutManagerThread() override final {
    return isInEventBaseThread();
  }

  void applyLoopKeepAlive();

  /*
   * Helper function that tells us whether we have already handled
   * some event/timeout/callback in this loop iteration.
   */
  bool nothingHandledYet() const noexcept;

  // small object used as a callback arg with enough info to execute the
  // appropriate client-provided Cob
  class CobTimeout : public AsyncTimeout {
   public:
    CobTimeout(EventBase* b, Func c, TimeoutManager::InternalEnum in)
        : AsyncTimeout(b, in), cob_(std::move(c)) {}

    virtual void timeoutExpired() noexcept;

   private:
    Func cob_;

   public:
    typedef boost::intrusive::list_member_hook<
      boost::intrusive::link_mode<boost::intrusive::auto_unlink> > ListHook;

    ListHook hook;

    typedef boost::intrusive::list<
      CobTimeout,
      boost::intrusive::member_hook<CobTimeout, ListHook, &CobTimeout::hook>,
      boost::intrusive::constant_time_size<false> > List;
  };

  typedef LoopCallback::List LoopCallbackList;
  class FunctionRunner;

  bool loopBody(int flags = 0);

  // executes any callbacks queued by runInLoop(); returns false if none found
  bool runLoopCallbacks();

  void initNotificationQueue();

  // should only be accessed through public getter
  HHWheelTimer::UniquePtr wheelTimer_;

  CobTimeout::List pendingCobTimeouts_;

  LoopCallbackList loopCallbacks_;
  LoopCallbackList runBeforeLoopCallbacks_;
  LoopCallbackList onDestructionCallbacks_;

  // This will be null most of the time, but point to currentCallbacks
  // if we are in the middle of running loop callbacks, such that
  // runInLoop(..., true) will always run in the current loop
  // iteration.
  LoopCallbackList* runOnceCallbacks_;

  // stop_ is set by terminateLoopSoon() and is used by the main loop
  // to determine if it should exit
  std::atomic<bool> stop_;

  // The ID of the thread running the main loop.
  // 0 if loop is not running.
  // Note: POSIX doesn't guarantee that 0 is an invalid pthread_t (or
  // even that atomic<pthread_t> is valid), but that's how it is
  // everywhere (at least on Linux, FreeBSD, and OSX).
  std::atomic<pthread_t> loopThread_;

  // pointer to underlying event_base class doing the heavy lifting
  event_base* evb_;

  // A notification queue for runInEventBaseThread() to use
  // to send function requests to the EventBase thread.
  std::unique_ptr<NotificationQueue<Func>> queue_;
  std::unique_ptr<FunctionRunner> fnRunner_;
  size_t loopKeepAliveCount_{0};
  bool loopKeepAliveActive_{false};

  // limit for latency in microseconds (0 disables)
  int64_t maxLatency_;

  // exponentially-smoothed average loop time for latency-limiting
  SmoothLoopTime avgLoopTime_;

  // smoothed loop time used to invoke latency callbacks; differs from
  // avgLoopTime_ in that it's scaled down after triggering a callback
  // to reduce spamminess
  SmoothLoopTime maxLatencyLoopTime_;

  // callback called when latency limit is exceeded
  Func maxLatencyCob_;

  // Enables/disables time measurements in loopBody(). if disabled, the
  // following functionality that relies on time-measurement, will not
  // be supported: avg loop time, observer and max latency.
  const bool enableTimeMeasurement_;

  // we'll wait this long before running deferred callbacks if the event
  // loop is idle.
  static const int kDEFAULT_IDLE_WAIT_USEC = 20000; // 20ms

  // Wrap-around loop counter to detect beginning of each loop
  uint64_t nextLoopCnt_;
  uint64_t latestLoopCnt_;
  uint64_t startWork_;
  // Prevent undefined behavior from invoking event_base_loop() reentrantly.
  // This is needed since many projects use libevent-1.4, which lacks commit
  // b557b175c00dc462c1fce25f6e7dd67121d2c001 from
  // https://github.com/libevent/libevent/.
  bool invokingLoop_{false};

  // Observer to export counters
  std::shared_ptr<EventBaseObserver> observer_;
  uint32_t observerSampleCount_;

  // EventHandler's execution observer.
  ExecutionObserver* executionObserver_;

  // Name of the thread running this EventBase
  std::string name_;

  // allow runOnDestruction() to be called from any threads
  std::mutex onDestructionCallbacksMutex_;

  // see EventBaseLocal
  friend class detail::EventBaseLocalBase;
  template <typename T> friend class EventBaseLocal;
  std::mutex localStorageMutex_;
  std::unordered_map<uint64_t, std::shared_ptr<void>> localStorage_;
  std::unordered_set<detail::EventBaseLocalBaseBase*> localStorageToDtor_;
};

template <typename T>
bool EventBase::runInEventBaseThread(void (*fn)(T*), T* arg) {
  return runInEventBaseThread([=] { fn(arg); });
}

template <typename T>
bool EventBase::runInEventBaseThreadAndWait(void (*fn)(T*), T* arg) {
  return runInEventBaseThreadAndWait([=] { fn(arg); });
}

template <typename T>
bool EventBase::runImmediatelyOrRunInEventBaseThreadAndWait(
    void (*fn)(T*),
    T* arg) {
  return runImmediatelyOrRunInEventBaseThreadAndWait([=] { fn(arg); });
}

} // folly
