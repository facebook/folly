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

#include <folly/stats/QuantileHistogram.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>

#include <folly/Benchmark.h>
#include <folly/portability/GFlags.h>

using folly::QuantileHistogram;

void addValue(unsigned int iters) {
  QuantileHistogram qhist;

  constexpr size_t kNumValues = 512;
  std::vector<double> values;
  size_t valuesIndex = 0;

  BENCHMARK_SUSPEND {
    std::default_random_engine generator;
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    values.reserve(kNumValues);
    for (size_t i = 0; i < kNumValues; i++) {
      values.push_back(dist(generator));
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    if (valuesIndex >= kNumValues) {
      valuesIndex = 0;
    }

    qhist.addValue(values[valuesIndex++]);
  }
}

void addValueMultithreaded(unsigned int iters, size_t nThreads) {
  folly::CPUShardedQuantileHistogram qhist;

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
              qhist.addValue(v);
            }
          }
        },
        threadIndex);
  }

  for (auto& th : threads) {
    th.join();
  }
}

void mergeUnsortedVals(unsigned int iters, size_t bufSize) {
  QuantileHistogram qhist;

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
      buffers.push_back(std::move(buffer));
    }
  }

  for (const auto& buffer : buffers) {
    qhist = qhist.merge(buffer);
  }
}

void mergeDigests(unsigned int iters, size_t maxSize, size_t nDigests) {
  std::vector<QuantileHistogram<>> digests;
  BENCHMARK_SUSPEND {
    QuantileHistogram qhist;
    std::default_random_engine generator;
    generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

    std::lognormal_distribution<double> distribution(0.0, 1.0);

    for (size_t i = 0; i < nDigests; ++i) {
      std::vector<double> buffer;
      for (size_t j = 0; j < maxSize; ++j) {
        buffer.push_back(distribution(generator));
      }
      digests.push_back(qhist.merge(buffer));
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    QuantileHistogram<>::merge(digests);
  }
}

void estimateQuantile(unsigned int iters, size_t maxSize, double quantile) {
  QuantileHistogram qhist;

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
      qhist = qhist.merge(buffer);
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(qhist.estimateQuantile(quantile));
  }
}

BENCHMARK_NAMED_PARAM(addValue, unsorted)

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 1, 1)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 2, 2)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 4, 4)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 8, 8)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 16, 16)
BENCHMARK_NAMED_PARAM(addValueMultithreaded, 32, 32)

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(mergeUnsortedVals, x100, 100)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeUnsortedVals, x500, 500)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeUnsortedVals, x1000, 1000)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeUnsortedVals, x10000, 10000)

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(mergeDigests, x10, 100, 10)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeDigests, x30, 100, 30)
BENCHMARK_RELATIVE_NAMED_PARAM(mergeDigests, x60, 100, 60)

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(estimateQuantile, p001, 100, 0.001)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p01, 100, 0.01)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p10, 100, 0.1)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p25, 100, 0.25)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p50, 100, 0.5)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p75, 100, 0.75)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p90, 100, 0.9)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p99, 100, 0.99)
BENCHMARK_RELATIVE_NAMED_PARAM(estimateQuantile, p999, 100, 0.999)

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
