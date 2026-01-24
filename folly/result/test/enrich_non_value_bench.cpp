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
#include <folly/result/enrich_non_value.h>
#include <folly/result/immortal_rich_error.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

namespace folly {

TEST(EnrichNonValueBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

struct BenchErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<BenchErr>;
};

template <typename ExpectedOuterErr, typename EnrichFn>
FOLLY_ALWAYS_INLINE void bench_enrich_non_value(size_t iters, EnrichFn&& fn) {
  folly::BenchmarkSuspender suspender;
  size_t numOkay = 0;
  suspender.dismissing([&] {
    for (size_t i = 0; i < iters; ++i) {
      // An immortal avoids measuring the cost of dynamic `std::exception_ptr`
      // for the base error -- otherwise, the baseline would be closer to 50ns.
      auto enriched = fn(error_or_stopped{immortal_rich_error<BenchErr>});
      folly::compiler_must_not_predict(enriched);
      numOkay += nullptr !=
          std::move(enriched)
              .release_rich_exception_ptr()
              .template get_outer_exception<ExpectedOuterErr>();
    }
  });
  CHECK_EQ(iters, numOkay);
}

BENCHMARK(non_enriched_baseline, n) {
  bench_enrich_non_value<BenchErr>(n, [](error_or_stopped&& eos) {
    return std::move(eos);
  });
}

BENCHMARK(enrich_non_value_location_only, n) {
  bench_enrich_non_value<detail::enriched_non_value>(
      n,
      [](error_or_stopped&& eos) { return enrich_non_value(std::move(eos)); });
}

BENCHMARK(enrich_non_value_location_plus_static_message, n) {
  bench_enrich_non_value<detail::enriched_non_value>(
      n, [](error_or_stopped&& eos) {
        return enrich_non_value(std::move(eos), "context");
      });
}

BENCHMARK(enrich_non_value_location_plus_formatted_message, n) {
  int value = 42;
  bench_enrich_non_value<detail::enriched_non_value>(
      n, [&](error_or_stopped&& eos) {
        return enrich_non_value(std::move(eos), "value={}", value);
      });
}

} // namespace folly

#endif

int main(int argc, char** argv) {
  return folly::benchmarkMain(argc, argv);
}
