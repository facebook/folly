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
#include <type_traits>

#include <folly/Benchmark.h>
#include <folly/Unit.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/rich_exception_ptr.h>

namespace folly::test {

consteval void check(bool cond) {
  if (!cond) {
    // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
    throw "check failed";
  }
}

void checkFormat(const auto& err, const std::string& re) {
  EXPECT_THAT(fmt::format("{}", err), ::testing::MatchesRegex(re));
  std::stringstream ss;
  ss << err;
  EXPECT_THAT(ss.str(), ::testing::MatchesRegex(re));
}

template <typename... Queries>
void checkFormatViaGet(const auto& container, const std::string& re) {
  (checkFormat(get_exception<Queries>(container), re), ...);
}

template <typename... Queries>
void checkFormatOfErrAndRep(const auto& err, const std::string& re) {
  checkFormat(err, re);
  checkFormatViaGet<Queries...>(rich_exception_ptr{err}, re);
}

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

} // namespace folly::test

#if FOLLY_HAS_RESULT

#include <folly/result/result.h>

namespace folly::test {

// Used for a non-predictable `result<T>` in micro-benchmarks where constant
// propagation would otherwise make it meaningless.
//
// Array of `result<T>` with mixed states -- errors at even indices, values at
// odd indices.  `next()` strides by 2 starting at `withValue` or `withError`.
// The result is always the same, but the compiler can't easily prove it.
template <auto V = unit>
struct BenchResult {
  using T = drop_unit_t<decltype(V)>;

  enum Start : size_t { withValue = 1, withError = 0 };

  static result<T> makeValue() {
    if constexpr (std::is_same_v<T, void>) {
      return {};
    } else {
      return result<T>{V};
    }
  }

  result<T> a[4] = {
      result<T>{error_or_stopped{std::runtime_error{"e"}}},
      makeValue(),
      result<T>{error_or_stopped{std::runtime_error{"e"}}},
      makeValue(),
  };
  size_t idx_;

  explicit BenchResult(Start start) : idx_(start) {}

  static FOLLY_ALWAYS_INLINE size_t advance(size_t idx) {
    return (idx + 2) & 0x3;
  }
  // Returns lref to current element and advances.
  FOLLY_ALWAYS_INLINE result<T>& next() {
    auto& r = a[idx_];
    idx_ = advance(idx_);
    return r;
  }
};

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
