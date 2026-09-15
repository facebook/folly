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

#include <folly/Chrono.h>

#include <folly/portability/GTest.h>

using namespace std::chrono;
using namespace folly::chrono;

namespace {
struct UnrelatedClock {};
} // namespace

static_assert( //
    std::is_same_v<
        clock_traits<steady_clock>::spec,
        clock_traits<coarse_steady_clock>::spec>);
static_assert( //
    std::is_same_v<
        steady_clock::time_point::duration,
        coarse_steady_clock::time_point::duration>);

static_assert( //
    std::is_same_v<
        clock_traits<system_clock>::spec,
        clock_traits<coarse_system_clock>::spec>);
static_assert( //
    std::is_same_v<
        system_clock::time_point::duration,
        coarse_system_clock::time_point::duration>);

static_assert(granularity_v<steady_clock> == clock_granularity::fine);
static_assert(granularity_v<coarse_steady_clock> == clock_granularity::coarse);
static_assert(granularity_v<system_clock> == clock_granularity::fine);
static_assert(granularity_v<coarse_system_clock> == clock_granularity::coarse);
static_assert(granularity_v<UnrelatedClock> == clock_granularity::unknown);

static_assert( //
    std::is_same_v<clock_traits<steady_clock>::spec::fine_clock, steady_clock>);
static_assert( //
    std::is_same_v<
        clock_traits<steady_clock>::spec::coarse_clock,
        coarse_steady_clock>);
static_assert( //
    std::is_same_v<clock_traits<system_clock>::spec::fine_clock, system_clock>);
static_assert( //
    std::is_same_v<
        clock_traits<system_clock>::spec::coarse_clock,
        coarse_system_clock>);

namespace {

class ChronoTest : public testing::Test {};
} // namespace

TEST_F(ChronoTest, abs_duration) {
  EXPECT_EQ(seconds(7), abs(seconds(7)));
  EXPECT_EQ(seconds(7), abs(seconds(-7)));
}

TEST_F(ChronoTest, ceil_duration) {
  EXPECT_EQ(seconds(7), ceil<seconds>(seconds(7)));
  EXPECT_EQ(seconds(7), ceil<seconds>(milliseconds(7000)));
  EXPECT_EQ(seconds(7), ceil<seconds>(milliseconds(6200)));
}

TEST_F(ChronoTest, ceil_time_point) {
  auto const point = steady_clock::time_point{};
  EXPECT_EQ(point + seconds(7), ceil<seconds>(point + seconds(7)));
  EXPECT_EQ(point + seconds(7), ceil<seconds>(point + milliseconds(7000)));
  EXPECT_EQ(point + seconds(7), ceil<seconds>(point + milliseconds(6200)));
}

TEST_F(ChronoTest, floor_duration) {
  EXPECT_EQ(seconds(7), floor<seconds>(seconds(7)));
  EXPECT_EQ(seconds(7), floor<seconds>(milliseconds(7000)));
  EXPECT_EQ(seconds(7), floor<seconds>(milliseconds(7800)));
}

TEST_F(ChronoTest, floor_time_point) {
  auto const point = steady_clock::time_point{};
  EXPECT_EQ(point + seconds(7), floor<seconds>(point + seconds(7)));
  EXPECT_EQ(point + seconds(7), floor<seconds>(point + milliseconds(7000)));
  EXPECT_EQ(point + seconds(7), floor<seconds>(point + milliseconds(7800)));
}

TEST_F(ChronoTest, round_duration) {
  EXPECT_EQ(seconds(7), round<seconds>(seconds(7)));
  EXPECT_EQ(seconds(6), round<seconds>(milliseconds(6200)));
  EXPECT_EQ(seconds(6), round<seconds>(milliseconds(6500)));
  EXPECT_EQ(seconds(7), round<seconds>(milliseconds(6800)));
  EXPECT_EQ(seconds(7), round<seconds>(milliseconds(7000)));
  EXPECT_EQ(seconds(7), round<seconds>(milliseconds(7200)));
  EXPECT_EQ(seconds(8), round<seconds>(milliseconds(7500)));
  EXPECT_EQ(seconds(8), round<seconds>(milliseconds(7800)));
}

TEST_F(ChronoTest, round_time_point) {
  auto const point = steady_clock::time_point{};
  EXPECT_EQ(point + seconds(7), round<seconds>(point + seconds(7)));
  EXPECT_EQ(point + seconds(6), round<seconds>(point + milliseconds(6200)));
  EXPECT_EQ(point + seconds(6), round<seconds>(point + milliseconds(6500)));
  EXPECT_EQ(point + seconds(7), round<seconds>(point + milliseconds(6800)));
  EXPECT_EQ(point + seconds(7), round<seconds>(point + milliseconds(7000)));
  EXPECT_EQ(point + seconds(7), round<seconds>(point + milliseconds(7200)));
  EXPECT_EQ(point + seconds(8), round<seconds>(point + milliseconds(7500)));
  EXPECT_EQ(point + seconds(8), round<seconds>(point + milliseconds(7800)));
}

TEST_F(ChronoTest, to_coarse_time_point_steady) {
  auto const tp = steady_clock::time_point{} + seconds(7);
  auto const coarse = to_coarse_time_point(tp);
  static_assert(
      std::is_same_v<decltype(coarse), coarse_steady_clock::time_point const>);
  EXPECT_EQ(tp.time_since_epoch(), coarse.time_since_epoch());
}

TEST_F(ChronoTest, to_fine_time_point_steady) {
  auto const tp = coarse_steady_clock::time_point{} + seconds(7);
  auto const fine = to_fine_time_point(tp);
  static_assert(std::is_same_v<decltype(fine), steady_clock::time_point const>);
  EXPECT_EQ(tp.time_since_epoch(), fine.time_since_epoch());
}

TEST_F(ChronoTest, to_coarse_time_point_system) {
  auto const tp = system_clock::time_point{} + seconds(7);
  auto const coarse = to_coarse_time_point(tp);
  static_assert(
      std::is_same_v<decltype(coarse), coarse_system_clock::time_point const>);
  EXPECT_EQ(tp.time_since_epoch(), coarse.time_since_epoch());
}

TEST_F(ChronoTest, to_fine_time_point_system) {
  auto const tp = coarse_system_clock::time_point{} + seconds(7);
  auto const fine = to_fine_time_point(tp);
  static_assert(std::is_same_v<decltype(fine), system_clock::time_point const>);
  EXPECT_EQ(tp.time_since_epoch(), fine.time_since_epoch());
}
