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

#include <bitset>
#include <random>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/container/StdBitset.h>
#include <folly/init/Init.h>

using namespace folly;

template <size_t N>
std::bitset<N> createRandomBitset(size_t seed, double density = 0.5) {
  std::bitset<N> bitset;
  std::mt19937 gen(seed);
  std::bernoulli_distribution dist(density);

  for (size_t i = 0; i < N; ++i) {
    if (dist(gen)) {
      bitset.set(i);
    }
  }
  return bitset;
}

template <size_t N>
std::bitset<N> createSparseBitset(size_t seed) {
  std::bitset<N> bitset;
  std::mt19937 gen(seed);
  std::uniform_int_distribution<size_t> dist(0, N - 1);

  size_t numBits = std::max(1UL, N / 100);
  for (size_t i = 0; i < numBits; ++i) {
    bitset.set(dist(gen));
  }
  return bitset;
}

template <size_t N>
std::bitset<N> createDenseBitset(size_t seed) {
  auto bitset = createSparseBitset<N>(seed);
  bitset.flip();
  return bitset;
}

BENCHMARK(StdBitsetFindFirst_64_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<64>(i + 12345);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_64_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<64>(i + 23456);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_64_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<64>(i + 34567);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_256_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<256>(i + 45678);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_256_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<256>(i + 56789);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_256_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<256>(i + 67890);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_1024_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<1024>(i + 78901);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_1024_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<1024>(i + 89012);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindFirst_1024_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<1024>(i + 90123);
    }
    auto result = std_bitset_find_first(bitset);
    doNotOptimizeAway(result);
  }
}

BENCHMARK(StdBitsetFindNext_64_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<64>(i + 11111);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_64_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<64>(i + 22222);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_64_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<64> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<64>(i + 33333);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_256_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<256>(i + 44444);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_256_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<256>(i + 55555);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_256_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<256> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<256>(i + 66666);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_1024_Sparse, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createSparseBitset<1024>(i + 77777);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_1024_Dense, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createDenseBitset<1024>(i + 88888);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetFindNext_1024_Random, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<1024>(i + 99999);
    }
    size_t pos = 0;
    size_t count = 0;
    while (pos < bitset.size() && count < 10) {
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
      if (pos < bitset.size()) {
        ++pos;
        ++count;
      }
    }
    doNotOptimizeAway(count);
  }
}

BENCHMARK(StdBitsetIterateAllBits_1024, iters) {
  for (size_t i = 0; i < iters; ++i) {
    std::bitset<1024> bitset;
    BENCHMARK_SUSPEND {
      bitset = createRandomBitset<1024>(i);
    }

    size_t count = 0;
    size_t pos = std_bitset_find_first(bitset);
    makeUnpredictable(pos);
    while (pos < bitset.size()) {
      ++count;
      pos = std_bitset_find_next(bitset, pos);
      makeUnpredictable(pos);
    }
    doNotOptimizeAway(count);
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  runBenchmarks();
  return 0;
}
