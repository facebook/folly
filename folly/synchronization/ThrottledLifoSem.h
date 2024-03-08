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
#include <limits>

#include <folly/GLog.h>
#include <folly/IntrusiveList.h>
#include <folly/Optional.h>
#include <folly/lang/Align.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/SaturatingSemaphore.h>
#include <folly/synchronization/WaitOptions.h>
#include <folly/synchronization/detail/Spin.h>

namespace folly {

class ThrottledLifoSemTestHelper;

/**
 * ThrottledLifoSem is a semaphore that can wait up to a configurable
 * wakeUpInterval before waking up a sleeping waiter. This gives an opportunity
 * to new waiters to consume the posted values, avoiding the overhead of waking
 * up a thread when the already active threads can consume values fast enough,
 * effectively allowing to batch the work. The semaphore is "throttled" because
 * sleeping waiters can be awoken at most once every wakeUpInterval.
 *
 * The motivating example is the task queue of a thread pool, where if a burst
 * of very small tasks (order of hundreds of nanoseconds each) is enqueued, it
 * may be beneficial to have a single thread process all the tasks sequentially,
 * rather than pay for the cost of waking up all the idle threads. However, if
 * nothing consumes the posted value, more waiters are awoken, each after a
 * wakeUpInterval delay. Sleeping waiters are awoken in LIFO order, consistently
 * with LifoSem.
 *
 * This is realized by having at most one sleeping waiter being in a "waking"
 * state: when such waiter is awoken, it immediately goes to sleep until last
 * wakeup time + wakeUpInterval to allow more value to accumulate and other
 * threads to consume it. If at the end of the sleep no value is left, as it was
 * consumed by other thread, the waking waiter can go back to sleep. Otherwise,
 * a new waiter becomes waking (if necessary) and control is returned to the
 * caller.
 *
 * Note that since wakeUpInterval is relative to the last awake time, in the
 * regime where post()s are spaced at least wakeUpInterval apart the waiters are
 * always awoken immediately, so there is no added latency in this case. Also,
 * when there is more outstanding value than waiters (for example, the thread
 * pool is saturated), there is no added latency compared to LifoSem.
 *
 * The interface is a subset of LifoSem, with semantics compatible with it. Only
 * the minimal subset needed to support task queues is currently implemented,
 * but the interface can be extended as needed.
 */
class ThrottledLifoSem {
 public:
  struct Options {
    std::chrono::nanoseconds wakeUpInterval = {};
  };

  // Setting initialValue is equivalent to calling post(initialValue)
  // immediately after construction.
  explicit ThrottledLifoSem(uint32_t initialValue = 0)
      : ThrottledLifoSem(Options{}, initialValue) {}
  explicit ThrottledLifoSem(const Options& options, uint32_t initialValue = 0)
      : options_(options), state_(initialValue) {}

  ~ThrottledLifoSem() {
    DCHECK(!(state_.load() & kWakingBit));
    DCHECK_EQ(state_.load() >> kNumWaitersShift, 0);
    DCHECK_EQ(waiters_.size(), 0);
  }

  // Returns true if there are enough waiters to consume the updated value, even
  // though they may not be awoken immediately. Silently saturates if value is
  // already 2^32-1.
  bool post(uint32_t n = 1) {
    uint32_t newValue;
    uint64_t oldState = state_.load(std::memory_order_relaxed);
    uint64_t newState;
    while (true) {
      uint64_t oldValue = oldState & kValueMask;
      newValue = static_cast<uint32_t>(std::min<uint64_t>(
          oldValue + n, std::numeric_limits<uint32_t>::max()));
      newState = (oldState & ~kValueMask) | newValue;
      if (casState(oldState, newState)) {
        break;
      }
    }

    // Avoid trying to wake up a waiter if there is nothing to wake up, or if
    // there is already an active waking chain. The waking thread will never
    // release the bit unless the value is 0 (or there is nothing to wake up).
    const auto numWaiters = newState >> kNumWaitersShift;
    if (numWaiters > 0 && !(newState & kWakingBit)) {
      maybeStartWakingChain();
    }

    return newValue <= numWaiters;
  }

  bool try_wait() { return tryWaitImpl<DecrNumWaiters::Never>(); }

  void wait(const WaitOptions& opt = {}) {
    auto const deadline = std::chrono::steady_clock::time_point::max();
    auto res = try_wait_until(deadline, opt);
    FOLLY_SAFE_DCHECK(res, "infinity time has passed");
  }

  template <typename Rep, typename Period>
  bool try_wait_for(
      const std::chrono::duration<Rep, Period>& timeout,
      const WaitOptions& opt = {}) {
    return try_wait_until(timeout + std::chrono::steady_clock::now(), opt);
  }

  template <typename Clock, typename Duration>
  bool try_wait_until(
      const std::chrono::time_point<Clock, Duration>& deadline,
      const WaitOptions& opt = {}) {
    // Fast path, avoid incrementing num waiters if value is positive.
    if (try_wait()) {
      return true;
    }

    state_.fetch_add(kNumWaitersInc, std::memory_order_seq_cst);

    switch (detail::spin_pause_until(deadline, opt, [this] {
      return tryWaitImpl<DecrNumWaiters::OnSuccess>();
    })) {
      case detail::spin_result::success:
        return true;
      case detail::spin_result::timeout:
        return tryWaitOnTimeout();
      case detail::spin_result::advance:
        break;
    }

    return tryWaitUntilSlow(deadline);
  }

  uint32_t valueGuess() const {
    return static_cast<uint32_t>(state_.load() & kValueMask);
  }

 private:
  friend class ThrottledLifoSemTestHelper;

  struct Waiter {
    SaturatingSemaphore<> wakeup;
    SafeIntrusiveListHook hook;
  };

  bool casState(uint64_t& oldState, uint64_t newState) {
    return state_.compare_exchange_weak(
        oldState,
        newState,
        std::memory_order_seq_cst,
        std::memory_order_relaxed);
  }

  enum DecrNumWaiters { Never, OnSuccess, Always };

  template <DecrNumWaiters kDecrNumWaiters>
  bool tryWaitImpl() {
    auto oldState = state_.load(std::memory_order_relaxed);
    bool success = (oldState & kValueMask) > 0;

    while (success || kDecrNumWaiters == DecrNumWaiters::Always) {
      // bool success used to indicate decrement or not (true = 1)
      auto newState = oldState - success;
      if (kDecrNumWaiters != DecrNumWaiters::Never) {
        DCHECK_GT(newState >> kNumWaitersShift, 0);
        newState -= kNumWaitersInc;
      }
      // casState mutates oldstate on CAS failure.
      if (casState(oldState, newState)) {
        return success;
      }

      // recalculate success with new oldState
      success = (oldState & kValueMask) > 0;
    }
    return false;
  }

  // If timed out after incrementing the number of waiters, we may have promised
  // a waiting thread if post() returned true, so we need to give a last look.
  bool tryWaitOnTimeout() { return tryWaitImpl<DecrNumWaiters::Always>(); }

  template <typename Clock, typename Duration>
  FOLLY_NOINLINE bool tryWaitUntilSlow(
      const std::chrono::time_point<Clock, Duration>& deadline) {
    // After the first iteration we own the waking bit.
    for (bool ownWaking = false;; ownWaking = true) {
      Optional<Waiter> waiter;
      mutex_.lock_combine([&] {
        if ((ownWaking && !tryReleaseWakingBit()) ||
            (!ownWaking && tryAcquireWakingBit())) {
          return;
        }
        waiter.emplace();
        waiters_.push_back(*waiter);
      });

      if (!waiter) {
        // We won the waking state immediately.
      } else if (!waiter->wakeup.try_wait_until(
                     deadline,
                     // Since the wake-ups are throttled, it is almost never
                     // convenient to spin-wait (the default 2us) for a wake-up.
                     WaitOptions{}.spin_max({}))) {
        const auto eraseWaiter = [&] {
          if (!waiter->hook.is_linked()) {
            // We lost the race with post(), so we cannot interrupt, we need
            // to wait for the wakeup which should be arriving imminently.
            return false;
          }
          waiters_.erase(waiters_.iterator_to(*waiter));
          return true;
        };
        if (!mutex_.lock_combine(eraseWaiter)) {
          // Here we do want to spin, use the default WaitOptions.
          waiter->wakeup.wait();
        } else {
          return tryWaitOnTimeout();
        }
      }

      DCHECK(state_.load() & kWakingBit);

      {
        const auto now = Clock::now();
        const auto steadyNow =
            std::is_same<Clock, std::chrono::steady_clock>::value
            ? now
            : std::chrono::steady_clock::now();
        const auto nextWakeup = lastWakeup_ + options_.wakeUpInterval;
        const auto sleep = std::min(
            nextWakeup - steadyNow,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - now));
        if (sleep.count() > 0) {
          /* sleep override */ std::this_thread::sleep_for(sleep);
          // Update with the intended wake-up time instead of the current time,
          // so if sleep_for oversleeps we'll correct in the next sleep.
          lastWakeup_ = steadyNow + sleep;
        } else {
          lastWakeup_ = steadyNow;
        }
      }

      // Same as the other timeout, need to give last look at value.
      const bool timedout = Clock::now() >= deadline;
      const bool success = timedout ? tryWaitOnTimeout()
                                    : tryWaitImpl<DecrNumWaiters::OnSuccess>();
      if (success || timedout) {
        // We are the waking thread, ensure we pass the waking state to another
        // thread if the value is still > 0 before returning control.
        if (auto nextWaiter = mutex_.lock_combine([&]() {
              Waiter* w = nullptr;
              if (waiters_.empty()) {
                // Nothing we can do.
                state_.fetch_and(~kWakingBit, std::memory_order_seq_cst);
              } else if (!tryReleaseWakingBit()) {
                w = &waiters_.back();
                waiters_.pop_back();
              }
              return w;
            })) {
          nextWaiter->wakeup.post();
        }

        return success;
      }

      // Try to back to sleep.
    }
  }

  FOLLY_NOINLINE void maybeStartWakingChain() {
    if (auto waiter = mutex_.lock_combine([&]() {
          Waiter* w = nullptr;
          if (!waiters_.empty() && tryAcquireWakingBit()) {
            w = &waiters_.back();
            waiters_.pop_back();
          }
          return w;
        })) {
      waiter->wakeup.post();
    }
  }

  // To avoid unnecessary sleeps, we should acquire the waking bit only if the
  // value is > 0.
  bool tryAcquireWakingBit() {
    auto oldState = state_.load(std::memory_order_relaxed);
    while (!(oldState & kWakingBit) && (oldState & kValueMask) > 0) {
      if (casState(oldState, oldState ^ kWakingBit)) {
        return true;
      }
    }
    return false;
  }

  // We can release the waking bit only if the value is 0.
  bool tryReleaseWakingBit() {
    auto oldState = state_.load(std::memory_order_relaxed);
    while ((oldState & kValueMask) == 0) {
      DCHECK(oldState & kWakingBit);
      if (casState(oldState, oldState ^ kWakingBit)) {
        return true;
      }
    }
    return false;
  }

  // Should only be used for testing.
  size_t numWaiters() const {
    // Do not use the numWaiters in the state because it includes threads that
    // haven't gone to sleep yet.
    return mutex_.lock_combine([&]() {
      return waiters_.size() + ((state_.load() & kWakingBit) != 0);
    });
  }

  const Options options_;

  // State: [numWaiters (31 bits) | waking (1 bit) | value (32 bits)]
  // numWaiters includes the waking thread.
  static constexpr int kValueBits = 32;
  static constexpr auto kValueMask = (uint64_t(1) << kValueBits) - 1;
  static constexpr auto kWakingBit = uint64_t(1) << kValueBits;
  static constexpr int kNumWaitersShift = kValueBits + 1;
  static constexpr auto kNumWaitersInc = uint64_t(1) << kNumWaitersShift;
  alignas(cacheline_align_v) std::atomic<uint64_t> state_;

  // Protects waiters_ and serializes attempts to acquire the waking bit, which
  // need to know if there are waiters available.
  mutable DistributedMutex mutex_;
  CountedIntrusiveList<Waiter, &Waiter::hook> waiters_;

  // Only accessed by the waking thread.
  alignas(cacheline_align_v) std::chrono::steady_clock::time_point
      lastWakeup_ = {};
};

} // namespace folly
