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
addValueMultithreaded(1)                                   36.25ns    27.59M
addValueMultithreaded(2)                                   18.43ns    54.26M
addValueMultithreaded(4)                                    9.43ns   106.01M
addValueMultithreaded(8)                                    4.95ns   201.85M
addValueMultithreaded(16)                                   3.22ns   310.33M
addValueMultithreaded(32)                                   2.41ns   414.16M
----------------------------------------------------------------------------
merge(100x1)                                                2.03us   491.85K
merge(100x5)                                    62.728%     3.24us   308.53K
merge(100x10)                                   42.260%     4.81us   207.86K
merge(1000x1)                                   9.6260%    21.12us    47.35K
merge(1000x5)                                   6.4313%    31.61us    31.63K
merge(1000x10)                                  4.7209%    43.07us    23.22K
----------------------------------------------------------------------------
mergeDigests(100x10)                                       12.62us    79.26K
mergeDigests(100x30)                            21.259%    59.35us    16.85K
mergeDigests(100x60)                            7.9139%   159.43us     6.27K
mergeDigests(1000x60)                          0.74658%     1.69ms    591.71
----------------------------------------------------------------------------
estimateQuantile(100x1_p001)                                6.29ns   159.04M
estimateQuantile(100_p01)                       61.636%    10.20ns    98.02M
estimateQuantile(100_p25)                       16.610%    37.86ns    26.42M
estimateQuantile(100_p50)                       11.535%    54.51ns    18.34M
estimateQuantile(100_p75)                       17.786%    35.35ns    28.29M
estimateQuantile(100_p99)                       66.422%     9.47ns   105.63M
estimateQuantile(100_p999)                      114.83%     5.48ns   182.62M
----------------------------------------------------------------------------
estimateQuantile(1000_p001)                     24.820%    25.33ns    39.47M
estimateQuantile(1000_p01)                      6.2674%   100.33ns     9.97M
estimateQuantile(1000_p25)                      1.4331%   438.77ns     2.28M
estimateQuantile(1000_p50)                     0.99586%   631.41ns     1.58M
estimateQuantile(1000_p75)                      1.3975%   449.96ns     2.22M
estimateQuantile(1000_p99)                      8.4795%    74.15ns    13.49M
estimateQuantile(1000_p999)                     33.323%    18.87ns    52.99M
#endif

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
