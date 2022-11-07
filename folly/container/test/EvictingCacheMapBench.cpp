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

#include <folly/container/EvictingCacheMap.h>

#include <folly/Benchmark.h>

using namespace folly;

inline uint64_t key(size_t i) {
  // Sensitive to non-avalanched hasher
  uint64_t marker = uint64_t{12345} << 48;
  uint64_t tmp = marker ^ i;
  int shift = ((i >> 8) & 7) * 8;
  return (tmp << shift) | (tmp >> (64 - shift));
}

// This function creates a cache of size `numElts` and scans through it `n`
// times in order to cause the most churn in the LRU structure as possible.
// This is meant to exercise the performance of the lookup path in the cache,
// most notably promotions within the LRU.
void scanCache(uint32_t n, size_t numElts) {
  BenchmarkSuspender suspender;

  EvictingCacheMap<uint64_t, size_t> m(numElts);
  for (size_t i = 0; i < numElts; ++i) {
    m.insert(key(i), i);
  }

  suspender.dismiss();
  for (uint32_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < numElts; ++j) {
      m.get(key(j));
    }
  }
}

void insertCache(uint32_t n, size_t numElts) {
  // Under-size to force cache evictions
  EvictingCacheMap<uint64_t, size_t> m(numElts * 9 / 10);
  for (uint32_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < numElts; ++j) {
      m.insert(key(j), i);
    }
  }
}

// Increment by factor of 4 * golden ratio to vary distance between
// powers of 2
BENCHMARK_PARAM(scanCache, 1000)
BENCHMARK_PARAM(scanCache, 6472)
BENCHMARK_PARAM(scanCache, 41889)
BENCHMARK_PARAM(scanCache, 271108)
BENCHMARK_PARAM(scanCache, 1754650) // 1.75M
BENCHMARK_PARAM(scanCache, 11356334) // 11.3M

BENCHMARK_PARAM(insertCache, 1000)
BENCHMARK_PARAM(insertCache, 6472)
BENCHMARK_PARAM(insertCache, 41889)
BENCHMARK_PARAM(insertCache, 271108)
BENCHMARK_PARAM(insertCache, 1754650) // 1.75M
BENCHMARK_PARAM(insertCache, 11356334) // 11.3M

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
}
