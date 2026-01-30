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
#include <folly/result/epitaph.h>
#include <folly/result/immortal_rich_error.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

namespace folly {

TEST(EpitaphBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

struct BenchErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<BenchErr>;
};

template <typename ExpectedOuterErr, typename EpitaphsFn>
FOLLY_ALWAYS_INLINE void bench_epitaph(size_t iters, EpitaphsFn&& fn) {
  folly::BenchmarkSuspender suspender;
  size_t numOkay = 0;
  suspender.dismissing([&] {
    for (size_t i = 0; i < iters; ++i) {
      // An immortal avoids measuring the cost of dynamic `std::exception_ptr`
      // for the base error -- otherwise, the baseline would be closer to 50ns.
      auto withEpitaphs = fn(error_or_stopped{immortal_rich_error<BenchErr>});
      folly::compiler_must_not_predict(withEpitaphs);
      numOkay += nullptr !=
          std::move(withEpitaphs)
              .release_rich_exception_ptr()
              .template get_outer_exception<ExpectedOuterErr>();
    }
  });
  CHECK_EQ(iters, numOkay);
}

BENCHMARK(no_epitaphs_baseline, n) {
  bench_epitaph<BenchErr>(n, [](error_or_stopped&& eos) {
    return std::move(eos);
  });
}

BENCHMARK(epitaph_location_only, n) {
  bench_epitaph<detail::epitaph_non_value>(n, [](error_or_stopped&& eos) {
    return epitaph(std::move(eos));
  });
}

BENCHMARK(epitaph_location_plus_static_message, n) {
  bench_epitaph<detail::epitaph_non_value>(n, [](error_or_stopped&& eos) {
    return epitaph(std::move(eos), "context");
  });
}

BENCHMARK(epitaph_location_plus_formatted_message, n) {
  int value = 42;
  bench_epitaph<detail::epitaph_non_value>(n, [&](error_or_stopped&& eos) {
    return epitaph(std::move(eos), "value={}", value);
  });
}

} // namespace folly

#endif

int main(int argc, char** argv) {
  return folly::benchmarkMain(argc, argv);
}
