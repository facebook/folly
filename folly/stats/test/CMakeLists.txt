# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Test definitions for folly/stats/test/

folly_define_tests(
  DIRECTORY stats/test/
      TEST stats_buffered_stat_test SOURCES BufferedStatTest.cpp
      BENCHMARK stats_digest_builder_benchmark
        SOURCES DigestBuilderBenchmark.cpp
      TEST stats_digest_builder_test SOURCES DigestBuilderTest.cpp
      BENCHMARK stats_histogram_benchmark SOURCES HistogramBenchmark.cpp
      TEST stats_histogram_test SOURCES HistogramTest.cpp
      BENCHMARK stats_quantile_histogram_benchmark
        SOURCES QuantileHistogramBenchmark.cpp
      TEST stats_quantile_estimator_test SOURCES QuantileEstimatorTest.cpp
      TEST stats_sliding_window_test SOURCES SlidingWindowTest.cpp
      BENCHMARK stats_tdigest_benchmark SOURCES TDigestBenchmark.cpp
      TEST stats_tdigest_test SOURCES TDigestTest.cpp
      TEST stats_timeseries_histogram_test SOURCES TimeseriesHistogramTest.cpp
      TEST stats_timeseries_test SOURCES TimeSeriesTest.cpp
)
