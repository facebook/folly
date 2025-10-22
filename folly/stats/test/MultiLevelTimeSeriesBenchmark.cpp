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
#include <folly/init/Init.h>
#include <folly/stats/MultiLevelTimeSeries.h>

template <typename CT>
void addValueEveryMillisecond(
    int iters,
    size_t numBuckets,
    std::initializer_list<typename CT::duration> durations,
    size_t stepSizeMs = 1) {
  folly::BenchmarkSuspender suspend;
  folly::MultiLevelTimeSeries<int64_t, CT> mlts(numBuckets, durations);
  std::chrono::seconds start(0);
  suspend.dismiss();

  for (int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(mlts);
    mlts.addValue(
        std::chrono::duration_cast<typename CT::duration>(
            start + std::chrono::milliseconds(i * stepSizeMs)) /* now */,
        42 /* value */);
  }
}

BENCHMARK(add_value_seconds_all_time_only, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters, 60, {std::chrono::seconds{0}});
}

BENCHMARK(add_value_milliseconds_all_time_only, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters, 60, {std::chrono::seconds{0}});
}

BENCHMARK_DRAW_LINE();

BENCHMARK(add_value_seconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters,
      60,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)});
}

BENCHMARK(add_value_milliseconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters,
      60,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)});
}

BENCHMARK_DRAW_LINE();

BENCHMARK(add_value_high_resolution_seconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters,
      20,
      {
          std::chrono::seconds(1),
          std::chrono::seconds(5),
          std::chrono::seconds(10),
          std::chrono::seconds(60),
          std::chrono::seconds(3600),
          std::chrono::seconds(0),
      });
}

BENCHMARK(add_value_high_resolution_milliseconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters,
      20,
      {
          std::chrono::seconds(1),
          std::chrono::seconds(5),
          std::chrono::seconds(10),
          std::chrono::seconds(60),
          std::chrono::seconds(3600),
          std::chrono::seconds(0),
      });
}

BENCHMARK_DRAW_LINE();
BENCHMARK(add_value_seconds_100ms_step_size, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters,
      60,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)},
      100);
}

BENCHMARK(add_value_milliseconds_100ms_step_size, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters,
      60,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)},
      100);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(add_value_high_resolution_seconds_100ms_step_size, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters,
      20,
      {
          std::chrono::seconds(1),
          std::chrono::seconds(5),
          std::chrono::seconds(10),
          std::chrono::seconds(60),
          std::chrono::seconds(3600),
          std::chrono::seconds(0),
      },
      100);
}

BENCHMARK(add_value_high_resolution_milliseconds_100ms_step_size, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters,
      20,
      {
          std::chrono::seconds(1),
          std::chrono::seconds(5),
          std::chrono::seconds(10),
          std::chrono::seconds(60),
          std::chrono::seconds(3600),
          std::chrono::seconds(0),
      },
      100);
}

int main(int argc, char* argv[]) {
  folly::Init init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}

#if 0
Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz
buck run @mode/opt fbcode//folly/stats/test:multi_level_time_series_benchmark -- --bm_min_usec 500000
============================================================================
[...]est/MultiLevelTimeSeriesBenchmark.cpp     relative  time/iter   iters/s
============================================================================
add_value_seconds_all_time_only                             1.66ns   601.35M
add_value_milliseconds_all_time_only                        1.72ns   579.83M
----------------------------------------------------------------------------
add_value_seconds                                           2.35ns   425.09M
add_value_milliseconds                                      1.92ns   521.98M
----------------------------------------------------------------------------
add_value_high_resolution_seconds                           2.44ns   409.52M
add_value_high_resolution_milliseconds                      4.24ns   235.76M
----------------------------------------------------------------------------
add_value_seconds_100ms_step_size                           9.83ns   101.71M
add_value_milliseconds_100ms_step_size                     10.38ns    96.34M
----------------------------------------------------------------------------
add_value_high_resolution_seconds_100ms_step_si            17.83ns    56.08M
add_value_high_resolution_milliseconds_100ms_st           131.18ns     7.62M
#endif
