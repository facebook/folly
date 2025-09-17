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
      valuesPerThread.emplace_back();
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
============================================================================
[...]folly/stats/test/TDigestBenchmark.cpp     relative  time/iter   iters/s
============================================================================
addValueMultithreaded(1)                                   37.55ns    26.63M
addValueMultithreaded(2)                                   17.82ns    56.12M
addValueMultithreaded(4)                                    9.24ns   108.28M
addValueMultithreaded(8)                                    5.05ns   197.96M
addValueMultithreaded(16)                                   3.22ns   310.56M
addValueMultithreaded(32)                                   2.46ns   406.93M
----------------------------------------------------------------------------
merge(100x1)                                                1.72us   581.36K
merge(100x5)                                    56.395%     3.05us   327.85K
merge(100x10)                                   39.945%     4.31us   232.22K
merge(1000x1)                                   10.710%    16.06us    62.26K
merge(1000x5)                                   6.2172%    27.67us    36.14K
merge(1000x10)                                  4.3072%    39.94us    25.04K
----------------------------------------------------------------------------
mergeDigests(100x10)                                       12.39us    80.73K
mergeDigests(100x30)                            20.209%    61.29us    16.31K
mergeDigests(100x60)                            7.4954%   165.26us     6.05K
mergeDigests(1000x60)                          0.73392%     1.69ms    592.49
----------------------------------------------------------------------------
estimateQuantile(100x1_p001)                                6.21ns   160.91M
estimateQuantile(100_p01)                       60.193%    10.32ns    96.86M
estimateQuantile(100_p25)                       16.003%    38.83ns    25.75M
estimateQuantile(100_p50)                       11.210%    55.44ns    18.04M
estimateQuantile(100_p75)                       18.234%    34.08ns    29.34M
estimateQuantile(100_p99)                       60.848%    10.21ns    97.91M
estimateQuantile(100_p999)                      102.55%     6.06ns   165.01M
----------------------------------------------------------------------------
estimateQuantile(1000_p001)                     24.268%    25.61ns    39.05M
estimateQuantile(1000_p01)                      6.2300%    99.75ns    10.02M
estimateQuantile(1000_p25)                      1.4174%   438.44ns     2.28M
estimateQuantile(1000_p50)                      1.0080%   616.51ns     1.62M
estimateQuantile(1000_p75)                      1.4456%   429.89ns     2.33M
estimateQuantile(1000_p99)                      8.6308%    72.00ns    13.89M
estimateQuantile(1000_p999)                     33.814%    18.38ns    54.41M
#endif

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
