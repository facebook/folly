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

#include <folly/logging/RateLimiter.h>

namespace folly {
namespace logging {

bool IntervalRateLimiter::checkSlow(clock::rep now) {
  auto intervalEnd = timestamp_.load(std::memory_order_acquire);
  if (now < intervalEnd) {
    return false;
  }

  auto newEnd = now + interval_.count();
  if (!timestamp_.compare_exchange_strong(
          intervalEnd,
          newEnd,
          std::memory_order_acq_rel,
          std::memory_order_relaxed)) {
    // Another thread already rolled the interval forward; rate-limit ourself.
    return false;
  }

  if (intervalEnd == kInitialTimestamp) {
    // Our increment in check() wrapped count_ to 0, so re-increment instead
    // of storing 1: other threads may already have incremented it further.
    auto origCount = count_.fetch_add(1, std::memory_order_acq_rel);
    return (origCount < maxPerInterval_);
  }

  // count_ is reset AFTER the CAS that publishes the new interval end. The
  // fast path in check() loads timestamp_ (acquire) before count_, so a
  // reader between the CAS and this store can see the new end alongside a
  // stale (saturated) count and falsely reject. That is acceptable: rate
  // limiting is unordered across threads, so a falsely-rejected call is
  // indistinguishable from one that arrived a few nanoseconds earlier, just
  // before the boundary. Resetting count_ before the CAS would avoid the
  // window but would let a losing CAS stomp the winner's count, admitting
  // up to maxPerInterval_ extra events.
  count_.store(1, std::memory_order_release);
  return true;
}
} // namespace logging
} // namespace folly
