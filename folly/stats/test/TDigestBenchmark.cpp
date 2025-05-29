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

#include <folly/stats/DigestBuilder.h>
#include <folly/stats/TDigest.h>

#include <algorithm>
#include <chrono>
#include <random>

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>

using folly::TDigest;

void addValueMultithreaded(unsigned int iters, size_t nThreads) {
  // This is a rather huge buffer size because it will allocate over 8k bytes
  // per CPU. However, it seems to be the typical size used by ODS. The TDigest
  // algorithm does see substantial speedups by having a large number of
  // unsorted values to process in a batch.
  folly::DigestBuilder<TDigest> digestBuilder(
      /*bufferSize=*/1000, /*digestSize=*/100);

  constexpr int kNumValues = 512;
  std::vector<std::vector<double>> valuesPerThread;

  BENCHMARK_SUSPEND {
    std::default_random_engine generator;
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    valuesPerThread.reserve(nThreads);
    for (size_t threadIndex = 0; threadIndex < nThreads; threadIndex++) {
      valuesPerThread.push_back(std::vector<double>{});
      auto& values = valuesPerThread.back();
      values.reserve(kNumValues);
      for (size_t i = 0; i < kNumValues; i++) {
        values.push_back(dist(generator));
      }
    }
  }

  std::atomic<int> remainingBatches{static_cast<int>(iters / kNumValues)};
  std::vector<std::thread> threads(nThreads);
  for (size_t threadIndex = 0; threadIndex < nThreads; threadIndex++) {
    threads[threadIndex] = std::thread(
        [&](size_t index) {
          while (remainingBatches.fetch_sub(1, std::memory_order_acq_rel) > 0) {
            for (const auto v : valuesPerThread[index]) {
              digestBuilder.append(v);
            }
          }
        },
        threadIndex);
  }

  for (auto& th : threads) {
    th.join();
  }
}

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
    digest = digest.merge(folly::sorted_equivalent, buffer);
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
      digest = digest.merge(buffer);
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    digest.estimateQuantile(quantile);
  }
}

BENCHMARK_NAMED_PARAM(addValueMultithreaded, 1, 1)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 2, 2)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 4, 4)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 8, 8)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 16, 16)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 32, 32)

BENCHMARK_DRAW_LINE();
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

#if 0
$ buck2 run @mode/opt-clang-thinlto folly/stats/test:tdigest_benchmark -- --bm_min_usec 200000
============================================================================
[...]folly/stats/test/TDigestBenchmark.cpp     relative  time/iter   iters/s
============================================================================
addValueMultithreaded(1)                                   35.74ns    27.98M
addValueMultithreaded(2)                                   17.87ns    55.95M
addValueMultithreaded(4)                                    9.20ns   108.74M
addValueMultithreaded(8)                                    5.00ns   199.95M
addValueMultithreaded(16)                                   3.23ns   309.88M
addValueMultithreaded(32)                                   2.47ns   404.19M
----------------------------------------------------------------------------
merge(100x1)                                                2.07us   483.18K
merge(100x5)                                    60.331%     3.43us   291.51K
merge(100x10)                                   44.095%     4.69us   213.06K
merge(1000x1)                                   9.9117%    20.88us    47.89K
merge(1000x5)                                   6.2382%    33.18us    30.14K
merge(1000x10)                                  4.6617%    44.40us    22.52K
----------------------------------------------------------------------------
mergeDigests(100x10)                                        9.73us   102.75K
mergeDigests(100x30)                            13.410%    72.58us    13.78K
mergeDigests(100x60)                            4.9018%   198.55us     5.04K
mergeDigests(1000x60)                          0.45690%     2.13ms    469.47
----------------------------------------------------------------------------
estimateQuantile(100x1_p001)                                7.16ns   139.59M
estimateQuantile(100_p01)                       69.246%    10.35ns    96.66M
estimateQuantile(100_p25)                       11.517%    62.20ns    16.08M
estimateQuantile(100_p50)                       13.373%    53.57ns    18.67M
estimateQuantile(100_p75)                       12.293%    58.27ns    17.16M
estimateQuantile(100_p99)                       74.274%     9.64ns   103.68M
estimateQuantile(100_p999)                      116.46%     6.15ns   162.57M
----------------------------------------------------------------------------
estimateQuantile(1000_p001)                     28.191%    25.41ns    39.35M
estimateQuantile(1000_p01)                      7.0709%   101.31ns     9.87M
estimateQuantile(1000_p25)                      1.6211%   441.91ns     2.26M
estimateQuantile(1000_p50)                      1.1638%   615.55ns     1.62M
estimateQuantile(1000_p75)                      1.6436%   435.84ns     2.29M
estimateQuantile(1000_p99)                      9.5754%    74.81ns    13.37M
estimateQuantile(1000_p999)                     37.233%    19.24ns    51.98M
#endif

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
