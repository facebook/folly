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

#include <folly/detail/PerfScoped.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Unistd.h>
#include <folly/test/TestUtils.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

#if FOLLY_PERF_IS_SUPPORTED

namespace folly {
namespace detail {
namespace {

// This is obviously not amazing
// but if perf didn't start properly,
// we won't get any results.
template <typename Test>
void retryWithTimeOuts(Test test) {
  for (auto timeOut = std::chrono::seconds(1);
       timeOut != std::chrono::seconds(10);
       ++timeOut) {
    if (test(timeOut)) {
      return;
    }
  }
}

TEST(PerfScopedTest, Stat) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";
  std::string output;

  retryWithTimeOuts([&](auto timeOut) {
    {
      PerfScoped perf{{"stat"}, &output};
      std::this_thread::sleep_for(timeOut);
    }
    return !output.empty();
  });

  ASSERT_THAT(
      output, ::testing::HasSubstr("Performance counter stats for process id"));
}

TEST(PerfScopedTest, Move) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";
  std::string output;

  retryWithTimeOuts([&](auto timeOut) {
    {
      PerfScoped assignHere;
      {
        PerfScoped fromHere{{"stat"}, &output};
        assignHere = std::move(fromHere);
        std::this_thread::sleep_for(timeOut);
      }
      // because assign here is still alive, should not do anything.
      EXPECT_TRUE(output.empty());
    }
    std::this_thread::sleep_for(timeOut);

    // Now that all guards are off, should be fine.
    return !output.empty();
  });

  ASSERT_FALSE(output.empty());
}

TEST(PerfScopedTest, Record) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";
  std::string output;

  retryWithTimeOuts([&](auto timeOut) {
    {
      PerfScoped perf{{"record"}, &output};
      std::this_thread::sleep_for(timeOut);
    }
    return !output.empty();
  });

  ASSERT_FALSE(output.empty());
}

TEST(PerfScopedTest, StatNoOutput) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";
  // Just verifying that this doesn't crash.
  PerfScoped perf{{"stat"}};
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

// A few milliseconds of real work, returning a checksum so that it cannot be
// optimized away. perf reports "<not counted>" for a task that never gets
// scheduled, so the window cannot just be a sleep.
int burnCpu() {
  std::vector<int> data(1024);
  std::iota(data.begin(), data.end(), 0);
  for (int i = 0; i != 20000; ++i) {
    std::reverse(data.begin(), data.end());
  }
  return data.back();
}

TEST(PerfScopedTest, ShortWindowIsCounted) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";

  constexpr int kProbeIterations = 32;
  std::string probeOutput;
  int probeChecksum = 0;
  {
    PerfScoped perf{{"stat", "-e", "instructions"}, &probeOutput};
    for (int i = 0; i != kProbeIterations; ++i) {
      probeChecksum += burnCpu();
    }
  }
  EXPECT_EQ(1023 * kProbeIterations, probeChecksum);
  SKIP_IF(probeOutput.find("<not counted>") != std::string::npos)
      << "Hardware instructions counter is unavailable in this environment";

  // A sub-millisecond window can intermittently report "<not counted>" on
  // virtualized or restricted CI hosts even when the counter works, so use a
  // window of a few milliseconds (still well below the ~9.4ms of front-loaded
  // work the window-attach bug this test guards used to miss) and retry a few
  // times. A reintroduced attach regression fails every attempt.
  constexpr int kMeasureIterations = 4;
  constexpr int kMaxAttempts = 5;
  std::string output;
  int checksum = 0;
  for (int attempt = 0; attempt != kMaxAttempts; ++attempt) {
    {
      PerfScoped perf{{"stat", "-e", "instructions"}, &output};
      checksum = 0;
      for (int i = 0; i != kMeasureIterations; ++i) {
        checksum += burnCpu();
      }
    }
    if (output.find("instructions") != std::string::npos &&
        output.find("<not counted>") == std::string::npos) {
      break;
    }
  }

  EXPECT_EQ(1023 * kMeasureIterations, checksum);
  EXPECT_THAT(output, ::testing::HasSubstr("instructions"));
  EXPECT_THAT(output, ::testing::Not(::testing::HasSubstr("<not counted>")));
}

TEST(PerfScopedTest, ThrowsWhenPerfCannotStart) {
  SKIP_IF(!std::filesystem::exists(kPerfBinaryPath)) << "Missing perf binary";
  EXPECT_THROW(
      PerfScoped({"stat", "-e", "definitely-not-a-real-event"}),
      std::runtime_error);
}

TEST(PerfScopedTest, RejectsCallerSuppliedWindowArgs) {
  EXPECT_THROW(PerfScoped({"stat", "--delay=100"}), std::invalid_argument);
  EXPECT_THROW(
      PerfScoped({"stat", "--control=fifo:/tmp/a,/tmp/b"}),
      std::invalid_argument);
}

} // namespace
} // namespace detail
} // namespace folly

#endif // defined(__linux__)
