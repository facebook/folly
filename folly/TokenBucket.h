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

#include <algorithm>
#include <atomic>
#include <limits>
#include <chrono>

#include <folly/Likely.h>

namespace folly {

/** Threadsafe TokenBucket implementation, based on the idea of
 * converting tokens into time and maintaining state as a timestamp relative to
 * now.  The number of tokens available is represented by the delta between now
 * and the timestamp, and the 'burst' is represented by the maximum delta.
 */
class TokenBucket {
 private:
  std::atomic<double> time_;
  std::atomic<double> secondsPerToken_;
  std::atomic<double> secondsPerBurst_;

 public:
  TokenBucket(double rate, double burst, double nowInSeconds) noexcept
      : time_(nowInSeconds) {
    reset(rate, burst, nowInSeconds);
  }

  void reset(double rate, double burst, double nowInSeconds) noexcept {
    double tokens = available(nowInSeconds);

    secondsPerToken_.store(
        1.0 / rate - std::numeric_limits<double>::epsilon(),
        std::memory_order_relaxed);

    secondsPerBurst_.store(
        burst / rate + std::numeric_limits<double>::epsilon(),
        std::memory_order_relaxed);

    set_capacity(tokens, nowInSeconds);
  }

  void set_capacity(double tokens, double nowInSeconds) noexcept {
    const double secondsPerToken = std::atomic_load_explicit(
        &secondsPerToken_, std::memory_order_relaxed);

    const double secondsPerBurst = std::atomic_load_explicit(
        &secondsPerBurst_, std::memory_order_relaxed);

    double newTime = nowInSeconds - std::min(
        tokens * secondsPerToken, secondsPerBurst);

    time_.store(newTime, std::memory_order_relaxed);
  }

  // If there are `tokens` avilable at `nowInSeconds`, consume them and
  // return true.  Otherwise, return false.
  //
  // This implementation is written in a lock-free manner using a
  // compare-and-exchange loop, with branch prediction optimized to minimize
  // time spent in the 'success' case which performs a write.
  bool consume(double tokens, double nowInSeconds) noexcept {
    const double secondsNeeded = tokens * std::atomic_load_explicit(
        &secondsPerToken_, std::memory_order_relaxed);

    const double minTime = nowInSeconds - std::atomic_load_explicit(
        &secondsPerBurst_, std::memory_order_relaxed);

    double oldTime =
      std::atomic_load_explicit(&time_, std::memory_order_relaxed);
    double newTime = oldTime;

    // Limit the number of available tokens to 'burst'.  We don't need to do
    // this inside the loop because if we iterate more than once another
    // caller will have performed an update that also covered this
    // calculation.  Also, tell the compiler to optimize branch prediction to
    // minimize time spent between reads and writes in the success case
    if (UNLIKELY(minTime > oldTime)) {
      newTime = minTime;
    }

    while (true) {
      newTime += secondsNeeded;

      // Optimize for the write-contention case, to minimize the impact of
      // branch misprediction on other threads
      if (UNLIKELY(newTime > nowInSeconds)) {
        return false;
      }

      // Optimize for the write-contention case, to minimize the impact of
      // branch misprediction on other threads
      if (LIKELY(std::atomic_compare_exchange_weak_explicit(
              &time_, &oldTime, newTime,
              std::memory_order_relaxed, std::memory_order_relaxed))) {
        return true;
      }

      newTime = oldTime;
    }

    return true;
  }

  // Similar to consume, but will always consume some number of tokens.
  double consumeOrDrain(double tokens, double nowInSeconds) noexcept {
    const double secondsPerToken = std::atomic_load_explicit(
        &secondsPerToken_, std::memory_order_relaxed);

    const double secondsNeeded = tokens * secondsPerToken;
    const double minTime = nowInSeconds - std::atomic_load_explicit(
        &secondsPerBurst_, std::memory_order_relaxed);

    double oldTime =
      std::atomic_load_explicit(&time_, std::memory_order_relaxed);
    double newTime = oldTime;


    // Limit the number of available tokens to 'burst'.
    // Also, tell the compiler to optimize branch prediction to
    // minimize time spent between reads and writes in the success case
    if (UNLIKELY(minTime > oldTime)) {
      newTime = minTime;
    }

    double consumed;

    newTime += secondsNeeded;

    consumed = (newTime - nowInSeconds) / secondsPerToken;
    time_.store(newTime, std::memory_order_relaxed);

    return consumed;
  }

  double available(double nowInSeconds = defaultClockNow()) const noexcept {
    double time =
      std::atomic_load_explicit(&time_, std::memory_order_relaxed);

    double deltaTime = std::min(
        std::atomic_load_explicit(&secondsPerBurst_,
                                  std::memory_order_relaxed),
        nowInSeconds - time);

    return std::max(0.0, deltaTime / std::atomic_load_explicit(
          &secondsPerToken_, std::memory_order_relaxed));
  }

  static double defaultClockNow() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
      ).count() / 1000000.0;
  }
};

}
