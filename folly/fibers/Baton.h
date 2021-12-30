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

#pragma once

#include <atomic>

#include <folly/Portability.h>
#include <folly/detail/Futex.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/io/async/HHWheelTimer.h>

namespace folly {
namespace fibers {

class Fiber;
class FiberManager;

/**
 * @class Baton
 *
 * Primitive which allows one to put current Fiber to sleep and wake it from
 * another Fiber/thread.
 */
class Baton {
 public:
  class TimeoutHandler;

  class Waiter {
   public:
    virtual void post() = 0;

    virtual ~Waiter() {}
  };

  Baton() noexcept;

  ~Baton() noexcept = default;

  bool ready() const {
    auto state = waiter_.load();
    return state == POSTED;
  }

  /**
   * Registers a waiter for the baton. The waiter will be notified when
   * the baton is posted.
   */
  void setWaiter(Waiter& waiter);

  /**
   * Puts active fiber to sleep. Returns when post is called.
   */
  void wait();

  /**
   * Put active fiber to sleep indefinitely. However, timeoutHandler may
   * be used elsewhere on the same thread in order to schedule a wakeup
   * for the active fiber.  Users of timeoutHandler must be on the same thread
   * as the active fiber and may only schedule one timeout, which must occur
   * after the active fiber calls wait.
   */
  void wait(TimeoutHandler& timeoutHandler);

  /**
   * Puts active fiber to sleep. Returns when post is called.
   *
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   */
  template <typename F>
  void wait(F&& mainContextFunc);

  /**
   * Checks if the baton has been posted without blocking.
   *
   * @return    true iff the baton has been posted.
   */
  bool try_wait();

  /**
   * Puts active fiber to sleep. Returns when post is called or the timeout
   * expires.
   *
   * @param timeout Baton will be automatically awaken if timeout expires
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename Rep, typename Period>
  bool try_wait_for(const std::chrono::duration<Rep, Period>& timeout) {
    return try_wait_for(timeout, [] {});
  }

  /**
   * Puts active fiber to sleep. Returns when post is called or the timeout
   * expires.
   *
   * @param timeout Baton will be automatically awaken if timeout expires
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename Rep, typename Period, typename F>
  bool try_wait_for(
      const std::chrono::duration<Rep, Period>& timeout, F&& mainContextFunc);

  /**
   * Puts active fiber to sleep. Returns when post is called or the deadline
   * expires.
   *
   * @param deadline Baton will be automatically awaken if deadline expires
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename Clock, typename Duration>
  bool try_wait_until(
      const std::chrono::time_point<Clock, Duration>& deadline) {
    return try_wait_until(deadline, [] {});
  }

  /**
   * Puts active fiber to sleep. Returns when post is called or the deadline
   * expires.
   *
   * @param deadline Baton will be automatically awaken if deadline expires
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename Clock, typename Duration, typename F>
  bool try_wait_until(
      const std::chrono::time_point<Clock, Duration>& deadline,
      F&& mainContextFunc);

  /**
   * Puts active fiber to sleep. Returns when post is called or the deadline
   * expires.
   *
   * @param deadline Baton will be automatically awaken if deadline expires
   * @param mainContextFunc this function is immediately executed on the main
   *        context.
   *
   * @return true if was posted, false if timeout expired
   */
  template <typename Clock, typename Duration, typename F>
  bool try_wait_for(
      const std::chrono::time_point<Clock, Duration>& deadline,
      F&& mainContextFunc);

  /// Alias to try_wait_for. Deprecated.
  template <typename Rep, typename Period>
  bool timed_wait(const std::chrono::duration<Rep, Period>& timeout) {
    return try_wait_for(timeout);
  }

  /// Alias to try_wait_for. Deprecated.
  template <typename Rep, typename Period, typename F>
  bool timed_wait(
      const std::chrono::duration<Rep, Period>& timeout, F&& mainContextFunc) {
    return try_wait_for(timeout, static_cast<F&&>(mainContextFunc));
  }

  /// Alias to try_wait_until. Deprecated.
  template <typename Clock, typename Duration>
  bool timed_wait(const std::chrono::time_point<Clock, Duration>& deadline) {
    return try_wait_until(deadline);
  }

  /// Alias to try_wait_until. Deprecated.
  template <typename Clock, typename Duration, typename F>
  bool timed_wait(
      const std::chrono::time_point<Clock, Duration>& deadline,
      F&& mainContextFunc) {
    return try_wait_until(deadline, static_cast<F&&>(mainContextFunc));
  }

  /**
   * Wakes up Fiber which was waiting on this Baton (or if no Fiber is waiting,
   * next wait() call will return immediately).
   */
  void post();

  /**
   * Reset's the baton (equivalent to destroying the object and constructing
   * another one in place).
   * Caller is responsible for making sure no one is waiting on/posting the
   * baton when reset() is called.
   */
  void reset();

  /**
   * Provides a way to schedule a wakeup for a wait()ing fiber.
   * A TimeoutHandler must be passed to Baton::wait(TimeoutHandler&)
   * before a timeout is scheduled. It is only safe to use the
   * TimeoutHandler on the same thread as the wait()ing fiber.
   * scheduleTimeout() may only be called once prior to the end of the
   * associated Baton's life.
   */
  class TimeoutHandler final : public HHWheelTimer::Callback {
   public:
    void scheduleTimeout(std::chrono::milliseconds timeout);

   private:
    friend class Baton;

    std::function<void()> timeoutFunc_{nullptr};
    FiberManager* fiberManager_{nullptr};

    void timeoutExpired() noexcept override {
      assert(timeoutFunc_ != nullptr);
      timeoutFunc_();
    }

    void callbackCanceled() noexcept override {}
  };

 private:
  class FiberWaiter;

  explicit Baton(intptr_t state) : waiter_(state) {}

  void postHelper(intptr_t new_value);
  void postThread();
  void waitThread();

  template <typename F>
  inline void waitFiber(FiberManager& fm, F&& mainContextFunc);

  template <typename Clock, typename Duration>
  bool timedWaitThread(
      const std::chrono::time_point<Clock, Duration>& deadline);

  static constexpr intptr_t NO_WAITER = 0;
  static constexpr intptr_t POSTED = -1;
  static constexpr intptr_t TIMEOUT = -2;
  static constexpr intptr_t THREAD_WAITING = -3;

  struct _futex_wrapper {
    folly::detail::Futex<> futex{};
    int32_t _unused_packing;
  };

  union {
    std::atomic<intptr_t> waiter_;
    struct _futex_wrapper futex_;
  };
};

#if FOLLY_HAS_COROUTINES
namespace detail {
class BatonAwaitableWaiter : public Baton::Waiter {
 public:
  explicit BatonAwaitableWaiter(Baton& baton) : baton_(baton) {}

  void post() override {
    assert(h_);
    h_();
  }

  bool await_ready() const { return baton_.ready(); }

  void await_resume() {}

  void await_suspend(coro::coroutine_handle<> h) {
    assert(!h_);
    h_ = std::move(h);
    baton_.setWaiter(*this);
  }

 private:
  coro::coroutine_handle<> h_;
  Baton& baton_;
};
} // namespace detail

inline detail::BatonAwaitableWaiter /* implicit */ operator co_await(
    Baton& baton) {
  return detail::BatonAwaitableWaiter(baton);
}
#endif
} // namespace fibers
} // namespace folly

#include <folly/fibers/Baton-inl.h>
