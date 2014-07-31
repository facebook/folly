/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/Logging.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <folly/Benchmark.h>
#include <vector>

TEST(LogEveryMs, basic) {
  std::vector<std::chrono::steady_clock::time_point> hist;

  while (hist.size() < 10) {
    FB_LOG_EVERY_MS(INFO, 10)
      << "test msg " << (hist.push_back(std::chrono::steady_clock::now()),
                         hist.size());
  }

  bool atLeastOneIsGood = false;
  for (int i = 0; i < hist.size() - 1; ++i) {
    auto delta = hist[i + 1] - hist[i];
    if (delta > std::chrono::milliseconds(5) &&
        delta < std::chrono::milliseconds(15)) {
      atLeastOneIsGood = true;
    }
  }
  EXPECT_TRUE(atLeastOneIsGood);
}

TEST(LogEveryMs, zero) {
  int count = 0;

  for (int i = 0; i < 10; ++i) {
    FB_LOG_EVERY_MS(INFO, 0)
      << "test msg " << ++count;
  }

  EXPECT_EQ(10, count);
}

BENCHMARK(skip_overhead, iter) {
  auto prev = FLAGS_minloglevel;
  FLAGS_minloglevel = 2;

  for (unsigned i = 0; i < iter; ++i) {
    FB_LOG_EVERY_MS(INFO, 1000) << "every 1s";
  }

  FLAGS_minloglevel = prev;
}

BENCHMARK(dev_null_log_overhead, iter) {
  auto prev = FLAGS_minloglevel;
  FLAGS_minloglevel = 2;

  for (unsigned i = 0; i < iter; ++i) {
    FB_LOG_EVERY_MS(INFO, -1) << "every -1ms";
  }

  FLAGS_minloglevel = prev;
}

// ============================================================================
// folly/test/LoggingTest.cpp                      relative  time/iter  iters/s
// ============================================================================
// skip_overhead                                               36.37ns   27.49M
// dev_null_log_overhead                                        2.61us  382.57K
// ============================================================================

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto rv = RUN_ALL_TESTS();
  if (!rv && FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return rv;
}
