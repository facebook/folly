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

#include <folly/chrono/Clock.h>

#include <folly/portability/GTest.h>

using namespace std::chrono;
using namespace std::chrono_literals;

TEST(ClockTest, SteadyClockSanity) {
  std::unique_ptr<folly::chrono::SteadyClock> clock =
      std::make_unique<folly::chrono::SteadyClockImpl>();

  // stclock = system clock
  // rstclock = reference system clock
  const auto stclockTp1 = clock->now();
  const auto rstclockTp1 = std::chrono::steady_clock::now();
  const auto stclockTp2 = clock->now();
  const auto rstclockTp2 = std::chrono::steady_clock::now();

  EXPECT_LE(stclockTp1, rstclockTp1);
  EXPECT_LE(rstclockTp1, stclockTp2);
  EXPECT_LE(stclockTp2, rstclockTp2);

  EXPECT_GE(rstclockTp2, stclockTp2);
  EXPECT_GE(stclockTp2, rstclockTp1);
  EXPECT_GE(rstclockTp1, stclockTp1);
}

TEST(ClockTest, SystemClockSanity) {
  std::unique_ptr<folly::chrono::SystemClock> clock =
      std::make_unique<folly::chrono::SystemClockImpl>();

  // sclock = system clock
  // rsclock = reference system clock
  const auto syclockTp1 = clock->now();
  const auto rsyclockTp1 = std::chrono::system_clock::now();
  const auto syclockTp2 = clock->now();
  const auto rsyclockTp2 = std::chrono::system_clock::now();

  /**
   * if system clock goes backwards during a test due to NTP correction, the
   * following may fail — but we expect that to be a very rare occurrence; rare
   * enough to not make this a problematic (flaky) test
   */

  EXPECT_LE(syclockTp1, rsyclockTp1);
  EXPECT_LE(rsyclockTp1, syclockTp2);
  EXPECT_LE(syclockTp2, rsyclockTp2);

  EXPECT_GE(rsyclockTp2, syclockTp2);
  EXPECT_GE(syclockTp2, rsyclockTp1);
  EXPECT_GE(rsyclockTp1, syclockTp1);
}
