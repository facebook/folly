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

#pragma once

#include <sstream>
#include <string>

#include <folly/Benchmark.h>
#include <folly/portability/GTest.h>

namespace folly {

// Helper to run benchmarks as a smoke test with minimal iterations.
// A "benchmarks don't crash" test is meaningful (1) since the benchmarks
// actually run some basic assertions, (2) CI will run this under ASAN.
inline void runBenchmarksAsTest() {
  gflags::FlagSaver flagSaver;
  FLAGS_bm_min_iters = 5;
  FLAGS_bm_max_iters = 5;
  folly::runBenchmarks();
}

// Helper for benchmark main() that supports both test mode (default) and
// benchmark mode (with --benchmark flag).
// If registerBenchmarksFn is provided, it will be called before running.
inline int benchmarkMain(
    int argc,
    char** argv,
    const std::function<void()>& registerBenchmarksFn = nullptr) {
  testing::InitGoogleTest(&argc, argv);
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (registerBenchmarksFn) {
    registerBenchmarksFn();
  }
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
    return 0;
  }
  auto ret = RUN_ALL_TESTS();
  LOG(WARNING) << "Ran a few iterations of each benchmark as a smoke-test, "
               << "pass `--benchmark` to ACTUALLY run benchmarks";
  return ret;
}

} // namespace folly
