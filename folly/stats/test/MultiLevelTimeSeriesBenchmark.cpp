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
    int iters, std::initializer_list<typename CT::duration> durations) {
  folly::BenchmarkSuspender suspend;
  folly::MultiLevelTimeSeries<int64_t, CT> mlts(60 /* numBuckets */, durations);
  std::chrono::seconds start(0);
  suspend.dismiss();

  for (int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(mlts);
    mlts.addValue(
        std::chrono::duration_cast<typename CT::duration>(
            start + std::chrono::milliseconds(i)) /* now */,
        42 /* value */);
  }
}

BENCHMARK(add_value_seconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)});
}

BENCHMARK(add_value_seconds_all_time_only, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::seconds>>(
      iters, {std::chrono::seconds{0}});
}

BENCHMARK(add_value_milliseconds, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters,
      {std::chrono::seconds(60),
       std::chrono::seconds(600),
       std::chrono::seconds(3600)});
}

BENCHMARK(add_value_milliseconds_all_time_only, iters) {
  addValueEveryMillisecond<folly::LegacyStatsClock<std::chrono::milliseconds>>(
      iters, {std::chrono::seconds{0}});
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
add_value_seconds                                           1.92ns   520.87M
add_value_seconds_all_time_only                             1.71ns   583.62M
add_value_milliseconds                                      1.84ns   544.45M
add_value_milliseconds_all_time_only                        1.78ns   562.22M
#endif
