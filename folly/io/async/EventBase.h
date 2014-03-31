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

#pragma once

#include <glog/logging.h>
#include "folly/io/async/AsyncTimeout.h"
#include "folly/io/async/TimeoutManager.h"
#include <memory>
#include <stack>
#include <list>
#include <queue>
#include <cstdlib>
#include <set>
#include <utility>
#include <boost/intrusive/list.hpp>
#include <boost/utility.hpp>
#include <functional>
#include <event.h>  // libevent
#include <errno.h>
#include <math.h>
#include <atomic>

namespace folly {

typedef std::function<void()> Cob;
template <typename MessageT>
class NotificationQueue;

class EventBaseObserver {
 public:
  virtual ~EventBaseObserver() {}

  virtual uint32_t getSampleRate() const = 0;

  virtual void loopSample(
    int64_t busyTime, int64_t idleTime) = 0;
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
class EventBase : private boost::noncopyable, public TimeoutManager {
 public:
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
  class LoopCallback {
   public:
    virtual ~LoopCallback() {}

    virtual void runLoopCallback() noexcept = 0;
    void cancelLoopCallback() {
      hook_.unlink();
    }

    bool isLoopCallbackScheduled() const {
      return hook_.is_linked();
    }

   private:
    typedef boost::intrusive::list_member_hook<
      boost::intrusive::link_mode<boost::intrusive::auto_unlink> > ListHook;

    ListHook hook_;

    typedef boost::intrusive::list<
      LoopCallback,
      boost::intrusive::member_hook<LoopCallback, ListHook,
                                    &LoopCallback::hook_>,
      boost::intrusive::constant_time_size<false> > List;

    // EventBase needs access to LoopCallbackList (and therefore to hook_)
    friend class EventBase;
    std::shared_ptr<RequestContext> context_;
  };

  /**
   * Create a new EventBase object.
   */
  EventBase();

  /**
   * Create a new EventBase object that will use the specified libevent
   * event_base object to drive the event loop.
   *
   * The EventBase will take ownership of this event_base, and will call
   * event_base_free(evb) when the EventBase is destroyed.
   */
  explicit EventBase(event_base* evb);
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
   * This is useful for callers that want to run the loop manually.
   *
   * Returns the same result as loop().
   */
  bool loopOnce();

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
   * cause the loop to exit after the callback returns.)
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
   * Convenience function to call runInLoop() with a std::function.
   *
   * This creates a LoopCallback object to wrap the std::function, and invoke
   * the std::function when the loop callback fires.  This is slightly more
   * expensive than defining your own LoopCallback, but more convenient in
   * areas that aren't performance sensitive where you just want to use
   * std::bind.  (std::bind is fairly slow on even by itself.)
   *
   * This method may only be called from the EventBase's thread.  This
   * essentially allows an event handler to schedule an additional callback to
   * be invoked after it returns.
   *
   * Use runInEventBaseThread() to schedule functions from another thread.
   */
  void runInLoop(const Cob& c, bool thisIteration = false);

  void runInLoop(Cob&& c, bool thisIteration = false);

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
   * function, the function may never be run at all.  The caller is responsible
   * for handling this situation correctly if they may terminate the loop with
   * outstanding runInEventBaseThread() calls pending.
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
  template<typename T>
  bool runInEventBaseThread(void (*fn)(T*), T* arg) {
    return runInEventBaseThread(reinterpret_cast<void (*)(void*)>(fn),
                                reinterpret_cast<void*>(arg));
  }

  bool runInEventBaseThread(void (*fn)(void*), void* arg);

  /**
   * Run the specified function in the EventBase's thread
   *
   * This version of runInEventBaseThread() takes a std::function object.
   * Note that this is less efficient than the version that takes a plain
   * function pointer and void* argument, as it has to allocate memory to copy
   * the std::function object.
   *
   * If the EventBase loop is terminated before it has a chance to run this
   * function, the allocated memory will be leaked.  The caller is responsible
   * for ensuring that the EventBase loop is not terminated before this
   * function can run.
   *
   * The function must not throw any exceptions.
   */
  bool runInEventBaseThread(const Cob& fn);

  /**
   * Runs the given Cob at some time after the specified number of
   * milliseconds.  (No guarantees exactly when.)
   *
   * @return  true iff the cob was successfully registered.
   */
  bool runAfterDelay(
      const Cob& c,
      int milliseconds,
      TimeoutManager::InternalEnum = TimeoutManager::InternalEnum::NORMAL);

  /**
   * Set the maximum desired latency in us and provide a callback which will be
   * called when that latency is exceeded.
   */
  void setMaxLatency(int64_t maxLatency, const Cob& maxLatencyCob) {
    maxLatency_ = maxLatency;
    maxLatencyCob_ = maxLatencyCob;
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

  // --------- interface to underlying libevent base ------------
  // Avoid using these functions if possible.  These functions are not
  // guaranteed to always be present if we ever provide alternative EventBase
  // implementations that do not use libevent internally.
  event_base* getLibeventBase() const { return evb_; }
  static const char* getLibeventVersion() { return event_get_version(); }
  static const char* getLibeventMethod() { return event_get_method(); }

  /**
   * only EventHandler/AsyncTimeout subclasses and ourselves should
   * ever call this.
   *
   * This is used to mark the beginning of a new loop cycle by the
   * first handler fired within that cycle.
   *
   */
  bool bumpHandlingTime();

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

  void setObserver(
    const std::shared_ptr<EventBaseObserver>& observer) {
    observer_ = observer;
  }

  const std::shared_ptr<EventBaseObserver>& getObserver() {
    return observer_;
  }

  /**
   * Set the name of the thread that runs this event base.
   */
  void setName(const std::string& name);

  /**
   * Returns the name of the thread that runs this event base.
   */
  const std::string& getName();

 private:

  // TimeoutManager
  void attachTimeoutManager(AsyncTimeout* obj,
                            TimeoutManager::InternalEnum internal);

  void detachTimeoutManager(AsyncTimeout* obj);

  bool scheduleTimeout(AsyncTimeout* obj, std::chrono::milliseconds timeout);

  void cancelTimeout(AsyncTimeout* obj);

  bool isInTimeoutManagerThread() {
    return isInEventBaseThread();
  }

  // Helper class used to short circuit runInEventBaseThread
  class RunInLoopCallback : public LoopCallback {
   public:
    RunInLoopCallback(void (*fn)(void*), void* arg);
    void runLoopCallback() noexcept;

   private:
    void (*fn_)(void*);
    void* arg_;
  };

  /*
   * Helper function that tells us whether we have already handled
   * some event/timeout/callback in this loop iteration.
   */
  bool nothingHandledYet();

  // --------- libevent callbacks (not for client use) ------------

  static void runFunctionPtr(std::function<void()>* fn);

  // small object used as a callback arg with enough info to execute the
  // appropriate client-provided Cob
  class CobTimeout : public AsyncTimeout {
   public:
    CobTimeout(EventBase* b, const Cob& c, TimeoutManager::InternalEnum in)
        : AsyncTimeout(b, in), cob_(c) {}

    virtual void timeoutExpired() noexcept;

   private:
    Cob cob_;

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

  bool loopBody(bool once = false);

  // executes any callbacks queued by runInLoop(); returns false if none found
  bool runLoopCallbacks(bool setContext = true);

  void initNotificationQueue();

  CobTimeout::List pendingCobTimeouts_;

  LoopCallbackList loopCallbacks_;

  // This will be null most of the time, but point to currentCallbacks
  // if we are in the middle of running loop callbacks, such that
  // runInLoop(..., true) will always run in the current loop
  // iteration.
  LoopCallbackList* runOnceCallbacks_;

  // stop_ is set by terminateLoopSoon() and is used by the main loop
  // to determine if it should exit
  bool stop_;

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
  std::unique_ptr<NotificationQueue<std::pair<void (*)(void*), void*>>> queue_;
  std::unique_ptr<FunctionRunner> fnRunner_;

  // limit for latency in microseconds (0 disables)
  int64_t maxLatency_;

  // exponentially-smoothed average loop time for latency-limiting
  SmoothLoopTime avgLoopTime_;

  // smoothed loop time used to invoke latency callbacks; differs from
  // avgLoopTime_ in that it's scaled down after triggering a callback
  // to reduce spamminess
  SmoothLoopTime maxLatencyLoopTime_;

  // callback called when latency limit is exceeded
  Cob maxLatencyCob_;

  // we'll wait this long before running deferred callbacks if the event
  // loop is idle.
  static const int kDEFAULT_IDLE_WAIT_USEC = 20000; // 20ms

  // Wrap-around loop counter to detect beginning of each loop
  uint64_t nextLoopCnt_;
  uint64_t latestLoopCnt_;
  uint64_t startWork_;

  // Observer to export counters
  std::shared_ptr<EventBaseObserver> observer_;
  uint32_t observerSampleCount_;

  // Name of the thread running this EventBase
  std::string name_;
};

} // folly
