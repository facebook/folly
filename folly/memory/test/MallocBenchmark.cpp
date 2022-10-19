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

#include <cmath>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/memory/Malloc.h>

// Returns random values with a power-law distribution (values from 2-4 are
// roughly equally represented as values from 16-32). A maxExponent of 60
// corresponds to about 1 exibyte which is close to the size limit for
// jemalloc.
std::vector<size_t> getRandomSizesPowerLaw(
    size_t maxExponent, size_t numSizes = 4096) {
  std::vector<size_t> sizes;
  sizes.reserve(numSizes);
  for (size_t i = 0; i < numSizes; i++) {
    sizes[i] = static_cast<size_t>(
        pow(2, maxExponent * folly::Random::randDouble01()));
  }
  return sizes;
}

std::vector<size_t> getRandomSizes(
    size_t minSize, size_t maxSize, size_t numSizes = 4096) {
  std::vector<size_t> sizes;
  sizes.reserve(numSizes);
  for (size_t i = 0; i < numSizes; i++) {
    sizes[i] =
        static_cast<size_t>(pow(2, folly::Random::rand64(minSize, maxSize)));
  }
  return sizes;
}

void bench(
    size_t iters,
    size_t (*nallocxLike)(size_t),
    std::vector<size_t> randomSizes) {
  size_t idx = 0;
  while (iters--) {
    if (idx == randomSizes.size()) {
      idx = 0;
    }
    size_t goodSize = nallocxLike(randomSizes[idx]);
    folly::doNotOptimizeAway(goodSize);
  }
}

void powerLaw(size_t iters, size_t (*nallocxLike)(size_t)) {
  return bench(iters, nallocxLike, getRandomSizesPowerLaw(/*maxExponent=*/60));
}

void uniform_0_128(size_t iters, size_t (*nallocxLike)(size_t)) {
  return bench(
      iters, nallocxLike, getRandomSizes(/*minSize=*/0, /*maxSize=*/128));
}

BENCHMARK_NAMED_PARAM(powerLaw, nallocx, [](size_t s) { return nallocx(s, 0); })
BENCHMARK_RELATIVE_NAMED_PARAM(
    powerLaw, naiveGoodMallocSize, folly::naiveGoodMallocSize)
BENCHMARK_RELATIVE_NAMED_PARAM(powerLaw, goodMallocSize, folly::goodMallocSize)

BENCHMARK_NAMED_PARAM(uniform_0_128, nallocx, [](size_t s) {
  return nallocx(s, 0);
})
BENCHMARK_RELATIVE_NAMED_PARAM(
    uniform_0_128, naiveGoodMallocSize, folly::naiveGoodMallocSize)
BENCHMARK_RELATIVE_NAMED_PARAM(
    uniform_0_128, goodMallocSize, folly::goodMallocSize)

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  folly::runBenchmarks();
  return 0;
}
