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

#include <filesystem>
#include <thread>

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

} // namespace
} // namespace detail
} // namespace folly

#endif // defined(__linux__)
