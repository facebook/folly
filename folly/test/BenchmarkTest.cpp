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

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace detail {
namespace {

struct TestClock {
  static std::chrono::high_resolution_clock::time_point value;
  static std::chrono::high_resolution_clock::time_point now() { return value; }

  template <typename Rep, typename Period>
  static void advance(std::chrono::duration<Rep, Period> diff) {
    value += diff;
  }
};

std::chrono::high_resolution_clock::time_point TestClock::value = {};

struct BenchmarkingStateTest : ::testing::Test {
  BenchmarkingStateTest() {
    state.addBenchmark(
        __FILE__,
        BenchmarkingState<TestClock>::getGlobalBaselineNameForTests(),
        [] {
          doBaseline();
          return 1;
        });
  }

  static void doBaseline() { TestClock::advance(std::chrono::nanoseconds(1)); }

  BenchmarkingState<TestClock> state;
  gflags::FlagSaver flagSaver;
};

TEST_F(BenchmarkingStateTest, Basic) {
  state.addBenchmark(__FILE__, "1ns", [&] {
    doBaseline();
    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  state.addBenchmark(__FILE__, "2ns", [&] {
    doBaseline();
    TestClock::advance(std::chrono::nanoseconds(2));
    return 1;
  });

  {
    const std::vector<BenchmarkResult> expected{
        {__FILE__, "1ns", 1, {}},
        {__FILE__, "2ns", 2, {}},
    };

    EXPECT_EQ(expected, state.runBenchmarksWithResults());
  }

  // --bm_regex
  {
    gflags::FlagSaver _;
    gflags::SetCommandLineOption("bm_regex", "1.*");

    const std::vector<BenchmarkResult> expected{
        {__FILE__, "1ns", 1, {}},
    };

    EXPECT_EQ(expected, state.runBenchmarksWithResults());
  }
}

TEST_F(BenchmarkingStateTest, Suspender) {
  state.addBenchmark(__FILE__, "1ns", [&] {
    doBaseline();
    {
      BenchmarkSuspender<TestClock> suspender;
      TestClock::advance(std::chrono::microseconds(1));
    }

    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  const std::vector<BenchmarkResult> expected{
      {__FILE__, "1ns", 1, {}},
  };

  EXPECT_EQ(expected, state.runBenchmarksWithResults());
}

TEST_F(BenchmarkingStateTest, DiscardOutlier) {
  int iterationToBeExpensive = 0;
  int currentIteration = 0;

  state.addBenchmark(__FILE__, "HasOutlier", [&] {
    doBaseline();
    if (currentIteration++ == iterationToBeExpensive) {
      TestClock::advance(std::chrono::microseconds(1));
    }

    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  const std::vector<BenchmarkResult> expected{
      {__FILE__, "HasOutlier", 1, {}},
  };

  for (int i = 0; i < 10; ++i) {
    iterationToBeExpensive = i;
    currentIteration = 0;

    EXPECT_EQ(expected, state.runBenchmarksWithResults());
  }
}

} // namespace
} // namespace detail
} // namespace folly
