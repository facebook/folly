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

#include <folly/Optional.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/DelayedDestruction.h>

#include <boost/intrusive/list.hpp>
#include <glog/logging.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <list>

namespace folly {

/**
 * Hashed Hierarchical Wheel Timer
 *
 * Comparison:
 * AsyncTimeout - a single timeout.
 * HHWheelTimer - a set of efficient timeouts with different interval,
 *    but timeouts are not exact.
 *
 * All of the above are O(1) in insertion, tick update and cancel

 * This implementation ticks once every 10ms.
 * We model timers as the number of ticks until the next
 * due event.  We allow 32-bits of space to track this
 * due interval, and break that into 4 regions of 8 bits.
 * Each region indexes into a bucket of 256 lists.
 *
 * Bucket 0 represents those events that are due the soonest.
 * Each tick causes us to look at the next list in a bucket.
 * The 0th list in a bucket is special; it means that it is time to
 * flush the timers from the next higher bucket and schedule them
 * into a different bucket.
 *
 * This technique results in a very cheap mechanism for
 * maintaining time and timers, provided that we can maintain
 * a consistent rate of ticks.
 */
class HHWheelTimer : private folly::AsyncTimeout,
                     public folly::DelayedDestruction {
 public:
  // This type has always been a misnomer, because it is not a unique pointer.
  using UniquePtr = std::unique_ptr<HHWheelTimer, Destructor>;
  using SharedPtr = std::shared_ptr<HHWheelTimer>;

  template <typename... Args>
  static UniquePtr newTimer(Args&&... args) {
    return UniquePtr(new HHWheelTimer(std::forward<Args>(args)...));
  }

  /**
   * A callback to be notified when a timeout has expired.
   */
  class Callback {
   public:
    Callback()
      : wheel_(nullptr)
      , expiration_(0) {}

    virtual ~Callback();

    /**
     * timeoutExpired() is invoked when the timeout has expired.
     */
    virtual void timeoutExpired() noexcept = 0;

    /// This callback was canceled. The default implementation is to just
    /// proxy to `timeoutExpired` but if you care about the difference between
    /// the timeout finishing or being canceled you can override this.
    virtual void callbackCanceled() noexcept {
      timeoutExpired();
    }

    /**
     * Cancel the timeout, if it is running.
     *
     * If the timeout is not scheduled, cancelTimeout() does nothing.
     */
    void cancelTimeout() {
      if (wheel_ == nullptr) {
        // We're not scheduled, so there's nothing to do.
        return;
      }
      cancelTimeoutImpl();
    }

    /**
     * Return true if this timeout is currently scheduled, and false otherwise.
     */
    bool isScheduled() const {
      return wheel_ != nullptr;
    }

   protected:
    /**
     * Don't override this unless you're doing a test. This is mainly here so
     * that we can override it to simulate lag in steady_clock.
     */
    virtual std::chrono::milliseconds getCurTime() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    }

   private:
    // Get the time remaining until this timeout expires
    std::chrono::milliseconds getTimeRemaining(
          std::chrono::milliseconds now) const {
      if (now >= expiration_) {
        return std::chrono::milliseconds(0);
      }
      return expiration_ - now;
    }

    void setScheduled(HHWheelTimer* wheel,
                      std::chrono::milliseconds);
    void cancelTimeoutImpl();

    HHWheelTimer* wheel_;
    folly::Optional<DestructorGuard> wheelGuard_;
    std::chrono::milliseconds expiration_;

    typedef boost::intrusive::list_member_hook<
      boost::intrusive::link_mode<boost::intrusive::auto_unlink> > ListHook;

    ListHook hook_;

    typedef boost::intrusive::list<
      Callback,
      boost::intrusive::member_hook<Callback, ListHook, &Callback::hook_>,
      boost::intrusive::constant_time_size<false> > List;

    std::shared_ptr<RequestContext> context_;

    // Give HHWheelTimer direct access to our members so it can take care
    // of scheduling/cancelling.
    friend class HHWheelTimer;
  };

  /**
   * Create a new HHWheelTimer with the specified interval and the
   * default timeout value set.
   *
   * Objects created using this version of constructor can be used
   * to schedule both variable interval timeouts using
   * scheduleTimeout(callback, timeout) method, and default
   * interval timeouts using scheduleTimeout(callback) method.
   */
  static int DEFAULT_TICK_INTERVAL;
  explicit HHWheelTimer(
      folly::TimeoutManager* timeoutManager,
      std::chrono::milliseconds intervalMS =
          std::chrono::milliseconds(DEFAULT_TICK_INTERVAL),
      AsyncTimeout::InternalEnum internal = AsyncTimeout::InternalEnum::NORMAL,
      std::chrono::milliseconds defaultTimeoutMS =
          std::chrono::milliseconds(-1));

  /**
   * Destroy the HHWheelTimer.
   *
   * A HHWheelTimer should only be destroyed when there are no more
   * callbacks pending in the set. (If it helps you may use cancelAll() to
   * cancel all pending timeouts explicitly before calling this.)
   *
   * However, it is OK to invoke this function (or via UniquePtr dtor) while
   * there are outstanding DestructorGuard's or HHWheelTimer::SharedPtr's.
   */
  virtual void destroy();

  /**
   * Cancel all outstanding timeouts
   *
   * @returns the number of timeouts that were cancelled.
   */
  size_t cancelAll();

  /**
   * Get the tick interval for this HHWheelTimer.
   *
   * Returns the tick interval in milliseconds.
   */
  std::chrono::milliseconds getTickInterval() const {
    return interval_;
  }

  /**
   * Get the default timeout interval for this HHWheelTimer.
   *
   * Returns the timeout interval in milliseconds.
   */
  std::chrono::milliseconds getDefaultTimeout() const {
    return defaultTimeout_;
  }

  /**
   * Schedule the specified Callback to be invoked after the
   * specified timeout interval.
   *
   * If the callback is already scheduled, this cancels the existing timeout
   * before scheduling the new timeout.
   */
  void scheduleTimeout(Callback* callback,
                       std::chrono::milliseconds timeout);
  void scheduleTimeoutImpl(Callback* callback,
                       std::chrono::milliseconds timeout);

  /**
   * Schedule the specified Callback to be invoked after the
   * fefault timeout interval.
   *
   * If the callback is already scheduled, this cancels the existing timeout
   * before scheduling the new timeout.
   *
   * This method uses CHECK() to make sure that the default timeout was
   * specified on the object initialization.
   */
  void scheduleTimeout(Callback* callback);

  template <class F>
  void scheduleTimeoutFn(F fn, std::chrono::milliseconds timeout) {
    struct Wrapper : Callback {
      Wrapper(F f) : fn_(std::move(f)) {}
      void timeoutExpired() noexcept override {
        try {
          fn_();
        } catch (std::exception const& e) {
          LOG(ERROR) << "HHWheelTimer timeout callback threw an exception: "
            << e.what();
        } catch (...) {
          LOG(ERROR) << "HHWheelTimer timeout callback threw a non-exception.";
        }
        delete this;
      }
      F fn_;
    };
    Wrapper* w = new Wrapper(std::move(fn));
    scheduleTimeout(w, timeout);
  }

  /**
   * Return the number of currently pending timeouts
   */
  uint64_t count() const {
    return count_;
  }

  /**
   * This turns on more exact timing.  By default the wheel timer
   * increments its cached time only once everyN (default) ticks.
   *
   * With catchupEveryN at 1, timeouts will only be delayed until the
   * next tick, at which point all overdue timeouts are called.  The
   * wheel timer is approximately 2x slower with this set to 1.
   *
   * Load testing in opt mode showed skew was about 1% with no catchup.
   */
  void setCatchupEveryN(uint32_t everyN) {
    catchupEveryN_ = everyN;
  }

  bool isDetachable() const {
    return !folly::AsyncTimeout::isScheduled();
  }

  using folly::AsyncTimeout::attachEventBase;
  using folly::AsyncTimeout::detachEventBase;
  using folly::AsyncTimeout::getTimeoutManager;

 protected:
  /**
   * Protected destructor.
   *
   * Use destroy() instead.  See the comments in DelayedDestruction for more
   * details.
   */
  virtual ~HHWheelTimer();

 private:
  // Forbidden copy constructor and assignment operator
  HHWheelTimer(HHWheelTimer const &) = delete;
  HHWheelTimer& operator=(HHWheelTimer const &) = delete;

  // Methods inherited from AsyncTimeout
  virtual void timeoutExpired() noexcept;

  std::chrono::milliseconds interval_;
  std::chrono::milliseconds defaultTimeout_;

  static constexpr int WHEEL_BUCKETS = 4;
  static constexpr int WHEEL_BITS = 8;
  static constexpr unsigned int WHEEL_SIZE = (1 << WHEEL_BITS);
  static constexpr unsigned int WHEEL_MASK = (WHEEL_SIZE - 1);
  static constexpr uint32_t LARGEST_SLOT = 0xffffffffUL;

  typedef Callback::List CallbackList;
  CallbackList buckets_[WHEEL_BUCKETS][WHEEL_SIZE];

  int64_t timeToWheelTicks(std::chrono::milliseconds t) {
    return t.count() / interval_.count();
  }

  bool cascadeTimers(int bucket, int tick);
  int64_t nextTick_;
  uint64_t count_;
  std::chrono::milliseconds now_;

  static constexpr uint32_t DEFAULT_CATCHUP_EVERY_N = 10;

  uint32_t catchupEveryN_;
  uint32_t expirationsSinceCatchup_;
  bool processingCallbacksGuard_;
};

} // folly
