/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/experimental/logging/RateLimiter.h>

namespace folly {
namespace logging {
IntervalRateLimiter::IntervalRateLimiter(
    uint64_t maxPerInterval,
    std::chrono::steady_clock::duration interval)
    : maxPerInterval_{maxPerInterval},
      interval_{interval},
      timestamp_{std::chrono::steady_clock::now().time_since_epoch().count()} {}

bool IntervalRateLimiter::checkSlow() {
  auto ts = timestamp_.load();
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  if (now < (ts + interval_.count())) {
    return false;
  }

  if (!timestamp_.compare_exchange_strong(ts, now)) {
    // We raced with another thread that reset the timestamp.
    // We treat this as if we fell into the previous interval, and so we
    // rate-limit ourself.
    return false;
  }

  // In the future, if we wanted to return the number of dropped events we
  // could use (count_.exchange(0) - maxPerInterval_) here.
  count_.store(1, std::memory_order_release);
  return true;
}
} // namespace logging
} // namespace folly
