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
#include <folly/detail/PerfScoped.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <algorithm>

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

void doBaseline() {
  TestClock::advance(std::chrono::nanoseconds(1));
}

struct BenchmarkingStateForTests : BenchmarkingState<TestClock> {
#if FOLLY_PERF_IS_SUPPORTED
  std::function<PerfScoped(const std::vector<std::string>&)> perfSetup;
  PerfScoped doSetUpPerfScoped(
      const std::vector<std::string>& args) const override {
    if (!perfSetup) {
      return BenchmarkingState<TestClock>::doSetUpPerfScoped(args);
    }
    return perfSetup(args);
  }
#endif
};

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

  BenchmarkingStateForTests state;
  gflags::FlagSaver flagSaver;
};

TEST_F(BenchmarkingStateTest, Basic) {
  state.addBenchmark(__FILE__, "a1ns", [&] {
    doBaseline();
    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  state.addBenchmark(__FILE__, "b2ns", [&] {
    doBaseline();
    TestClock::advance(std::chrono::nanoseconds(2));
    return 1;
  });

  {
    const std::vector<BenchmarkResult> expected{
        {__FILE__, "a1ns", 1, {}},
        {__FILE__, "b2ns", 2, {}},
    };

    EXPECT_EQ(expected, state.runBenchmarksWithResults());
  }

  // --bm_regex full match
  {
    gflags::FlagSaver _;
    gflags::SetCommandLineOption("bm_regex", "a1.*");

    const std::vector<BenchmarkResult> expected{
        {__FILE__, "a1ns", 1, {}},
    };

    EXPECT_EQ(expected, state.runBenchmarksWithResults());
  }

  // --bm_regex part match
  {
    gflags::FlagSaver _;
    gflags::SetCommandLineOption("bm_regex", "1.");

    const std::vector<BenchmarkResult> expected{
        {__FILE__, "a1ns", 1, {}},
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

TEST_F(BenchmarkingStateTest, DiscardLines) {
  state.addBenchmark(__FILE__, "1ns", [&] {
    doBaseline();
    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  // DRAW_LINE adds a "-" benchmark
  state.addBenchmark(__FILE__, "-", [&] { return 0; });

  const std::vector<BenchmarkResult> expected{
      {__FILE__, "1ns", 1, {}},
  };

  EXPECT_EQ(expected, state.runBenchmarksWithResults());
}

TEST_F(BenchmarkingStateTest, PerfBasic) {
  int setUpPerfCalled = 0;
  std::vector<std::string> expectedArgs;

  state.perfSetup = [&](const std::vector<std::string>& args) {
    ++setUpPerfCalled;
    EXPECT_EQ(expectedArgs, args);
    return PerfScoped{};
  };

  {
    (void)state.runBenchmarksWithResults();
    EXPECT_EQ(0, setUpPerfCalled);
  }

  {
    gflags::FlagSaver _;
    gflags::SetCommandLineOption(
        "bm_perf_args", "stat -e cache-misses,cache-references");

    setUpPerfCalled = 0;
    expectedArgs = {"stat", "-e", "cache-misses,cache-references"};
    (void)state.runBenchmarksWithResults();
  }
}

TEST_F(BenchmarkingStateTest, PerfSkipsAnIteration) {
  bool firstTimeSetUpDone = false;
  bool perfIsCalled = false;

  state.addBenchmark(__FILE__, "a", [&] {
    doBaseline();
    firstTimeSetUpDone = true;
    TestClock::advance(std::chrono::nanoseconds(1));
    return 1;
  });

  state.perfSetup = [&](const std::vector<std::string>&) {
    EXPECT_TRUE(firstTimeSetUpDone);
    perfIsCalled = true;
    return PerfScoped{};
  };

  gflags::FlagSaver _;
  gflags::SetCommandLineOption("bm_perf_args", "stat");
  (void)state.runBenchmarksWithResults();

  EXPECT_TRUE(perfIsCalled);
}

#if FOLLY_PERF_IS_SUPPORTED
TEST_F(BenchmarkingStateTest, PerfIntegration) {
  std::vector<int> in(1000, 0);

  state.addBenchmark(__FILE__, "a", [&](unsigned n) {
    for (unsigned i = n; i; --i) {
      std::reverse(in.begin(), in.end());
    }
    TestClock::advance(std::chrono::microseconds(1));
    return n;
  });

  std::string perfOuptut;

  state.perfSetup = [&](const auto& args) {
    return PerfScoped(args, &perfOuptut);
  };

  gflags::FlagSaver _;
  gflags::SetCommandLineOption("bm_perf_args", "stat");
  gflags::SetCommandLineOption("bm_profile", "true");
  gflags::SetCommandLineOption("bm_profile_iters", "1000000");
  (void)state.runBenchmarksWithResults();

  ASSERT_THAT(
      perfOuptut,
      ::testing::HasSubstr("Performance counter stats for process id"));
}

#endif // FOLLY_PERF_IS_SUPPORTED

TEST_F(BenchmarkingStateTest, SkipWarmUp) {
  std::vector<unsigned> iterNumPassed;
  state.addBenchmark(__FILE__, "a", [&](unsigned iters) {
    iterNumPassed.push_back(iters);
    TestClock::advance(std::chrono::microseconds(1));
    return iters;
  });

  gflags::FlagSaver _;
  gflags::SetCommandLineOption("bm_profile", "true");
  gflags::SetCommandLineOption("bm_profile_iters", "1000");

  // Testing that warm up iteration is off by default and that
  // we get exactly the number of iterations passed in once.
  // A lot of places rely on this behaviour and changing it
  // will break them.
  {
    (void)state.runBenchmarksWithResults();
    ASSERT_THAT(iterNumPassed, ::testing::ElementsAre(1000));
  }

  iterNumPassed.clear();

  gflags::SetCommandLineOption("bm_warm_up_iteration", "true");

  {
    (void)state.runBenchmarksWithResults();
    ASSERT_THAT(iterNumPassed, ::testing::ElementsAre(1, 1000));
  }
}

} // namespace
} // namespace detail
} // namespace folly
