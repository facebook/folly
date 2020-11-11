/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/container/EvictingCacheMap.h>

#include <folly/Benchmark.h>

using namespace folly;

// This function creates a cache of size `numElts` and scans through it `n`
// times in order to cause the most churn in the LRU structure as possible.
// This is meant to exercise the performance of the lookup path in the cache,
// most notably promotions within the LRU.
void scanCache(uint32_t n, size_t numElts) {
  BenchmarkSuspender suspender;

  EvictingCacheMap<size_t, size_t> m(numElts);
  for (size_t i = 0; i < numElts; ++i) {
    m.insert(i, i);
  }

  suspender.dismissing([&]() {
    for (uint32_t i = 0; i < n; ++i) {
      for (size_t j = 0; j < numElts; ++j) {
        m.get(j);
      }
    }
  });
}

BENCHMARK_PARAM(scanCache, 1000) // 1K
BENCHMARK_PARAM(scanCache, 10000) // 10K
BENCHMARK_PARAM(scanCache, 100000) // 100K
BENCHMARK_PARAM(scanCache, 1000000) // 1M

int main() {
  runBenchmarks();
}
