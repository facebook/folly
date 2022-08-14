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
#include <folly/synchronization/Baton.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/WaitOptions.h>
#include <folly/synchronization/detail/Spin.h>

namespace folly {

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
    std::chrono::nanoseconds wakeUpInterval = std::chrono::microseconds(100);
  };

  // Setting initialValue is equivalent to calling post(initialValue)
  // immediately after construction.
  explicit ThrottledLifoSem(uint32_t initialValue = 0)
      : ThrottledLifoSem(Options{}, initialValue) {}
  explicit ThrottledLifoSem(const Options& options, uint32_t initialValue = 0)
      : options_(options), value_(initialValue) {}

  ~ThrottledLifoSem() {
    DCHECK(!waking_);
    DCHECK_EQ(waiters_.size(), 0);
  }

  // Returns true if there are enough waiters to consume the updated value, even
  // though they may not be awoken immediately.
  // Silently saturates if value is already 2^32-1
  bool post(uint32_t n = 1) {
    auto oldValue = value_.load(std::memory_order_relaxed);
    while (true) {
      auto value = static_cast<uint32_t>(std::min<uint64_t>(
          static_cast<uint64_t>(oldValue) + n,
          std::numeric_limits<uint32_t>::max()));
      if (value_.compare_exchange_weak(
              oldValue,
              value,
              std::memory_order_seq_cst,
              std::memory_order_relaxed)) {
        break;
      }
    }

    auto ret = mutex_.lock_combine([&]() {
      // Reload, by the time the mutex is acquired the value may be zero.
      auto value = value_.load(std::memory_order_relaxed);
      auto enoughWaiters = value <= waiters_.size() + waking_;

      Waiter* w = nullptr;
      if (!waking_ && !waiters_.empty() && value > 0) {
        waking_ = true;
        w = &waiters_.back();
        waiters_.pop_back();
      }
      return std::make_pair(w, enoughWaiters);
    });
    if (ret.first) {
      ret.first->baton.post();
    }
    return ret.second;
  }

  bool try_wait() {
    auto oldValue = value_.load(std::memory_order_relaxed);
    while (oldValue > 0) {
      if (value_.compare_exchange_weak(
              oldValue,
              oldValue - 1,
              std::memory_order_seq_cst,
              std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

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
    switch (detail::spin_pause_until(
        deadline, opt, [this] { return try_wait(); })) {
      case detail::spin_result::success:
        return true;
      case detail::spin_result::timeout:
        return false;
      case detail::spin_result::advance:
        break;
    }

    for (bool first = true;; first = false) {
      Optional<Waiter> waiter;
      mutex_.lock_combine([&] {
        if (!first) {
          // If this is not the first iteration, we must be the waking
          // thread and had nothing to consume.
          DCHECK(waking_);
          waking_ = false;
        }
        if (!waking_ && value_.load(std::memory_order_relaxed) > 0) {
          // Either there is no waking thread yet, or we are the waking thread
          // and a post() happened after we failed the try_wait().
          waking_ = true;
          return;
        }
        waiter.emplace();
        waiters_.push_back(*waiter);
      });

      if (!waiter) {
        // We won the waking state immediately.
      } else if (!waiter->baton.try_wait_until(
                     deadline,
                     // Since the wake-ups are throttled, it is almost never
                     // convenient to spin-wait (the default 2us) for a wake-up.
                     WaitOptions{}.spin_max({}))) {
        if (mutex_.lock_combine([&] {
              if (!waiter->hook.is_linked()) {
                // We lost the race with post(), so we cannot interrupt, we need
                // to wait for the wakeup which should be arriving imminently.
                return false;
              }
              waiters_.erase(waiters_.iterator_to(*waiter));
              return true;
            })) {
          return false;
        } else {
          // Here we do want to spin, use the default WaitOptions.
          waiter->baton.wait();
        }
      }

      DCHECK(mutex_.lock_combine([&] { return waking_; }));

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

      const bool success = try_wait();
      if (success || Clock::now() >= deadline) {
        // We are the waking thread, ensure we pass the waking state to another
        // thread if the value is still > 0 before returning control.
        auto wakeup = mutex_.lock_combine([&]() {
          Waiter* w = nullptr;
          if (waiters_.empty() || value_.load(std::memory_order_relaxed) == 0) {
            waking_ = false;
          } else {
            w = &waiters_.back();
            waiters_.pop_back();
          }
          return w;
        });
        if (wakeup) {
          wakeup->baton.post();
        }

        return success;
      }

      // Go back to sleep.
    }
  }

  uint32_t valueGuess() const { return value_.load(); }

  // Should only be used for testing.
  size_t numWaiters() const {
    return mutex_.lock_combine([&]() { return waiters_.size() + waking_; });
  }

 private:
  struct Waiter {
    Baton<> baton;
    SafeIntrusiveListHook hook;
  };

  const Options options_;

  // Protects waking_ and waiters_.
  alignas(cacheline_align_v) mutable DistributedMutex mutex_;
  bool waking_ = false;
  CountedIntrusiveList<Waiter, &Waiter::hook> waiters_;

  std::atomic<uint32_t> value_;

  // Only accessed by the waking thread.
  alignas(cacheline_align_v) std::chrono::steady_clock::time_point
      lastWakeup_ = {};
};

} // namespace folly
