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
#include <chrono>
#include <cstdint>
#include <type_traits>

#include <folly/Chrono.h>

namespace folly {
namespace logging {

/**
 * Rate limiter allowing up to N events per fixed window of M milliseconds.
 *
 * Unlike a token bucket, this does not smooth events across the window: a
 * burst of N is accepted as fast as it arrives at the start of each interval,
 * after which all calls are rejected until the next interval begins.
 */
class IntervalRateLimiter {
 public:
  using clock = chrono::coarse_steady_clock;

  constexpr IntervalRateLimiter(
      uint64_t maxPerInterval, clock::duration interval)
      : maxPerInterval_{maxPerInterval}, interval_{interval} {}

  bool check() {
    // Fast path: when the current interval is saturated, skip the contended
    // RMW on count_.
    auto now = clock::now().time_since_epoch().count();
    auto intervalEnd = timestamp_.load(std::memory_order_acquire);
    if (now < intervalEnd &&
        count_.load(std::memory_order_relaxed) >= maxPerInterval_) {
      return false;
    }
    auto origCount = count_.fetch_add(1, std::memory_order_acq_rel);
    if (origCount < maxPerInterval_) {
      return true;
    }
    return checkSlow(now);
  }

 private:
  // Sentinel for "no interval has started yet"; any real `now` exceeds it,
  // so the first check() falls through to checkSlow() to initialize state.
  static_assert(
      std::is_signed<clock::rep>::value,
      "Need signed time point to represent initial time");
  constexpr static auto kInitialTimestamp =
      std::numeric_limits<clock::rep>::min();

  bool checkSlow(clock::rep now);

  const uint64_t maxPerInterval_;
  const clock::time_point::duration interval_;

  // Initialized to the max representable value so the first check() wraps
  // on fetch_add and falls into checkSlow() to set up the initial interval.
  std::atomic<uint64_t> count_{std::numeric_limits<uint64_t>::max()};
  // End of the current interval. clock::rep rather than time_point because
  // time_point's constructor is not noexcept.
  std::atomic<clock::rep> timestamp_{kInitialTimestamp};
};

} // namespace logging
} // namespace folly
