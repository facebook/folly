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

#include <folly/hash/Hash.h>

#include <stdint.h>

#include <deque>
#include <random>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/Preprocessor.h>
#include <folly/portability/GFlags.h>

namespace detail {

std::vector<uint8_t> randomBytes(size_t n) {
  std::vector<uint8_t> ret(n);
  std::default_random_engine rng(1729); // Deterministic seed.
  std::uniform_int_distribution<uint16_t> dist(0, 255);
  std::generate(ret.begin(), ret.end(), [&]() { return dist(rng); });
  return ret;
}

std::vector<uint8_t> benchData = randomBytes(1 << 20); // 1MiB, fits in cache.

template <class Hasher>
void bmHasher(Hasher hasher, size_t k, size_t iters) {
  CHECK_LE(k, benchData.size());
  for (size_t i = 0, pos = 0; i < iters; ++i, ++pos) {
    if (pos == benchData.size() - k + 1) {
      pos = 0;
    }
    folly::doNotOptimizeAway(hasher(benchData.data() + pos, k));
  }
}

template <class Hasher>
void addHashBenchmark(const std::string& name) {
  static std::deque<std::string> names;

  for (size_t k = 1; k < 16; ++k) {
    names.emplace_back(fmt::format("{}: k={}", name, k));
    folly::addBenchmark(__FILE__, names.back().c_str(), [=](unsigned iters) {
      Hasher hasher;
      bmHasher(hasher, k, iters);
      return iters;
    });
  }

  for (size_t i = 0; i < 16; ++i) {
    auto k = size_t(1) << i;
    names.emplace_back(fmt::format("{}: k=2^{}", name, i));
    folly::addBenchmark(__FILE__, names.back().c_str(), [=](unsigned iters) {
      Hasher hasher;
      bmHasher(hasher, k, iters);
      return iters;
    });
  }

  /* Draw line. */
  folly::addBenchmark(__FILE__, "-", []() { return 0; });
}

struct SpookyHashV2 {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::SpookyHashV2::Hash64(data, size, 0);
  }
};

struct FNV64 {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::fnv64_buf(data, size);
  }
};

} // namespace detail

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  std::deque<std::string> names; // Backing for benchmark names.

#define BENCHMARK_HASH(HASHER) \
  detail::addHashBenchmark<detail::HASHER>(FOLLY_PP_STRINGIZE(HASHER));

  BENCHMARK_HASH(SpookyHashV2);
  BENCHMARK_HASH(FNV64);

#undef BENCHMARK_HASH

  folly::runBenchmarks();

  return 0;
}

#if 0
Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz
$ hash_benchmark --bm_min_usec=100000
============================================================================
fbcode/folly/hash/test/HashBenchmark.cpp     relative  time/iter   iters/s
============================================================================
SpookyHashV2: k=1                                           6.91ns   144.73M
SpookyHashV2: k=2                                           7.49ns   133.59M
SpookyHashV2: k=3                                           7.62ns   131.24M
SpookyHashV2: k=4                                           7.23ns   138.37M
SpookyHashV2: k=5                                           7.48ns   133.66M
SpookyHashV2: k=6                                           7.61ns   131.36M
SpookyHashV2: k=7                                           8.01ns   124.85M
SpookyHashV2: k=8                                           7.26ns   137.68M
SpookyHashV2: k=9                                           7.54ns   132.57M
SpookyHashV2: k=10                                          7.84ns   127.49M
SpookyHashV2: k=11                                          7.96ns   125.66M
SpookyHashV2: k=12                                          7.26ns   137.65M
SpookyHashV2: k=13                                          7.61ns   131.40M
SpookyHashV2: k=14                                          7.93ns   126.09M
SpookyHashV2: k=15                                          8.43ns   118.69M
SpookyHashV2: k=2^0                                         7.20ns   138.98M
SpookyHashV2: k=2^1                                         7.70ns   129.91M
SpookyHashV2: k=2^2                                         7.06ns   141.63M
SpookyHashV2: k=2^3                                         7.22ns   138.43M
SpookyHashV2: k=2^4                                        13.62ns    73.44M
SpookyHashV2: k=2^5                                        13.92ns    71.85M
SpookyHashV2: k=2^6                                        21.50ns    46.51M
SpookyHashV2: k=2^7                                        36.76ns    27.21M
SpookyHashV2: k=2^8                                        46.37ns    21.57M
SpookyHashV2: k=2^9                                        64.77ns    15.44M
SpookyHashV2: k=2^10                                       96.45ns    10.37M
SpookyHashV2: k=2^11                                      164.44ns     6.08M
SpookyHashV2: k=2^12                                      310.08ns     3.22M
SpookyHashV2: k=2^13                                      601.24ns     1.66M
SpookyHashV2: k=2^14                                        1.17us   854.85K
SpookyHashV2: k=2^15                                        2.39us   418.45K
----------------------------------------------------------------------------
FNV64: k=1                                                  1.58ns   631.45M
FNV64: k=2                                                  1.96ns   510.70M
FNV64: k=3                                                  2.36ns   424.50M
FNV64: k=4                                                  2.90ns   344.46M
FNV64: k=5                                                  3.67ns   272.16M
FNV64: k=6                                                  4.30ns   232.82M
FNV64: k=7                                                  4.86ns   205.77M
FNV64: k=8                                                  4.43ns   225.59M
FNV64: k=9                                                  5.17ns   193.32M
FNV64: k=10                                                 5.70ns   175.31M
FNV64: k=11                                                 6.40ns   156.20M
FNV64: k=12                                                 7.41ns   134.97M
FNV64: k=13                                                 7.83ns   127.71M
FNV64: k=14                                                 8.38ns   119.35M
FNV64: k=15                                                 9.10ns   109.90M
FNV64: k=2^0                                                1.59ns   629.20M
FNV64: k=2^1                                                1.97ns   507.67M
FNV64: k=2^2                                                2.72ns   367.76M
FNV64: k=2^3                                                4.25ns   235.10M
FNV64: k=2^4                                                8.86ns   112.86M
FNV64: k=2^5                                               21.73ns    46.01M
FNV64: k=2^6                                               48.68ns    20.54M
FNV64: k=2^7                                              123.62ns     8.09M
FNV64: k=2^8                                              275.64ns     3.63M
FNV64: k=2^9                                              582.25ns     1.72M
FNV64: k=2^10                                               1.18us   845.79K
FNV64: k=2^11                                               2.36us   422.91K
FNV64: k=2^12                                               4.78us   209.24K
FNV64: k=2^13                                               9.57us   104.50K
FNV64: k=2^14                                              19.12us    52.31K
FNV64: k=2^15                                              38.75us    25.81K
----------------------------------------------------------------------------
#endif
