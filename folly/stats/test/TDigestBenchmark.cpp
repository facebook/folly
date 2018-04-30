/*
 * Copyright 2012-present Facebook, Inc.
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
#include <folly/stats/TDigest.h>

#include <algorithm>
#include <chrono>
#include <random>

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>

using folly::TDigest;

void merge(unsigned int iters, size_t maxSize, size_t bufSize) {
  TDigest digest(maxSize);

  std::vector<std::vector<double>> buffers;

  BENCHMARK_SUSPEND {
    std::default_random_engine generator;
    generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

    std::lognormal_distribution<double> distribution(0.0, 1.0);

    for (size_t i = 0; i < iters; ++i) {
      std::vector<double> buffer;
      for (size_t j = 0; j < bufSize; ++j) {
        buffer.push_back(distribution(generator));
      }
      std::sort(buffer.begin(), buffer.end());
      buffers.push_back(std::move(buffer));
    }
  }

  for (const auto& buffer : buffers) {
    digest = digest.merge(buffer);
  }
}

void mergeDigests(unsigned int iters, size_t maxSize, size_t nDigests) {
  std::vector<TDigest> digests;
  BENCHMARK_SUSPEND {
    TDigest digest(maxSize);
    std::default_random_engine generator;
    generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

    std::lognormal_distribution<double> distribution(0.0, 1.0);

    for (size_t i = 0; i < nDigests; ++i) {
      std::vector<double> buffer;
      for (size_t j = 0; j < maxSize; ++j) {
        buffer.push_back(distribution(generator));
      }
      std::sort(buffer.begin(), buffer.end());
      digests.push_back(digest.merge(buffer));
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    TDigest::merge(digests);
  }
}

void estimateQuantile(unsigned int iters, size_t maxSize, double quantile) {
  TDigest digest(maxSize);

  size_t bufSize = maxSize * 10;
  BENCHMARK_SUSPEND {
    std::vector<double> values;

    std::default_random_engine generator;
    generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

    std::lognormal_distribution<double> distribution(0.0, 1.0);

    for (size_t i = 0; i < 50000; ++i) {
      values.push_back(distribution(generator));
    }

    for (size_t i = 0; i < 50000 / bufSize; ++i) {
      std::vector<double> buffer;
      for (size_t j = 0; j < bufSize; ++j) {
        buffer.push_back(values[i * bufSize + j]);
      }
      std::sort(buffer.begin(), buffer.end());
      digest = digest.merge(buffer);
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    digest.estimateQuantile(quantile);
  }
}

BENCHMARK_NAMED_PARAM(merge, 100x1, 100, 100)
BENCHMARK_RELATIVE_NAMED_PARAM(merge, 100x5, 100, 500)
BENCHMARK_RELATIVE_NAMED_PARAM(merge, 100x10, 100, 1000)
BENCHMARK_RELATIVE_NAMED_PARAM(merge, 1000x1, 1000, 1000)
BENCHMARK_RELATIVE_NAMED_PARAM(merge, 1000x5, 1000, 5000)
BENCHMARK_RELATIVE_NAMED_PARAM(merge, 1000x10, 1000, 10000)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(mergeDigests, 100x10, 100, 10)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeDigests, 100x30, 100, 30)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeDigests, 100x60, 100, 60)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeDigests, 1000x60, 1000, 60)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(estimateQuantile, 100x1_p001, 100, 0.001)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p01, 100, 0.01)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p25, 100, 0.25)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p50, 100, 0.5)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p75, 100, 0.75)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p99, 100, 0.99)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 100_p999, 100, 0.999)
BENCHMARK_DRAW_LINE();
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p001, 1000, 0.001)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p01, 1000, 0.01)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p25, 1000, 0.25)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p50, 1000, 0.5)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p75, 1000, 0.75)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p99, 1000, 0.99)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, 1000_p999, 1000, 0.999)

/*
 * ./tdigest_benchmark
 * ============================================================================
 * folly/stats/test/TDigestBenchmark.cpp           relative  time/iter  iters/s
 * ============================================================================
 * merge(100x1)                                                 2.19us  455.86K
 * merge(100x5)                                      58.77%     3.73us  267.92K
 * merge(100x10)                                     42.00%     5.22us  191.48K
 * merge(1000x1)                                     10.52%    20.86us   47.95K
 * merge(1000x5)                                      6.54%    33.54us   29.81K
 * merge(1000x10)                                     4.43%    49.54us   20.19K
 * ----------------------------------------------------------------------------
 * mergeDigests(100x10)                                        25.29us   39.55K
 * mergeDigests(100x30)                              21.71%   116.50us    8.58K
 * mergeDigests(100x60)                               9.22%   274.32us    3.65K
 * mergeDigests(1000x60)                              0.90%     2.81ms   356.45
 * ----------------------------------------------------------------------------
 * estimateQuantile(100x1_p001)                                 8.48ns  117.88M
 * estimateQuantile(100_p01)                         61.32%    13.83ns   72.29M
 * estimateQuantile(100_p25)                         11.66%    72.73ns   13.75M
 * estimateQuantile(100_p50)                          9.55%    88.79ns   11.26M
 * estimateQuantile(100_p75)                         13.88%    61.14ns   16.36M
 * estimateQuantile(100_p99)                         66.88%    12.68ns   78.83M
 * estimateQuantile(100_p999)                       110.57%     7.67ns  130.34M
 * ----------------------------------------------------------------------------
 * estimateQuantile(1000_p001)                       26.46%    32.06ns   31.19M
 * estimateQuantile(1000_p01)                         7.78%   108.97ns    9.18M
 * estimateQuantile(1000_p25)                         1.74%   488.35ns    2.05M
 * estimateQuantile(1000_p50)                         1.24%   683.10ns    1.46M
 * estimateQuantile(1000_p75)                         1.75%   483.58ns    2.07M
 * estimateQuantile(1000_p99)                         8.06%   105.29ns    9.50M
 * estimateQuantile(1000_p999)                       32.98%    25.72ns   38.87M
 * ============================================================================
 */

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
