/*
 * Copyright 2004-present Facebook, Inc.
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
#include <thread>

#include <folly/Conv.h>
#include <folly/experimental/logging/RateLimiter.h>
#include <folly/portability/GTest.h>

using folly::logging::IntervalRateLimiter;
using std::chrono::duration_cast;
using namespace std::literals::chrono_literals;

void intervalTest(
    uint64_t eventsPerInterval,
    std::chrono::steady_clock::duration interval) {
  SCOPED_TRACE(folly::to<std::string>(
      eventsPerInterval,
      " events every ",
      duration_cast<std::chrono::milliseconds>(interval).count(),
      "ms"));
  IntervalRateLimiter limiter{eventsPerInterval, interval};
  for (int iter = 0; iter < 4; ++iter) {
    if (iter != 0) {
      /* sleep override */
      std::this_thread::sleep_for(interval);
    }
    for (uint64_t n = 0; n < eventsPerInterval * 2; ++n) {
      if (n < eventsPerInterval) {
        EXPECT_TRUE(limiter.check())
            << "expected check success on loop " << iter << " event " << n;
      } else {
        EXPECT_FALSE(limiter.check())
            << "expected check failure on loop " << iter << " event " << n;
      }
    }
  }
}

TEST(RateLimiter, interval3per100ms) {
  intervalTest(3, 100ms);
}

TEST(RateLimiter, interval1per100ms) {
  intervalTest(1, 100ms);
}

TEST(RateLimiter, interval15per150ms) {
  intervalTest(15, 150ms);
}
