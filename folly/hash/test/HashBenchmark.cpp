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
#include <folly/hash/MurmurHash.h>

#include <stdint.h>

#include <deque>
#include <random>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/Preprocessor.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GFlags.h>

extern "C" FOLLY_KEEP uint64_t
check_folly_spooky_hash_v2_hash_32(void const* data, size_t size) {
  return folly::hash::SpookyHashV2::Hash32(data, size, 0);
}

extern "C" FOLLY_KEEP uint64_t
check_folly_spooky_hash_v2_hash_64(void const* data, size_t size) {
  return folly::hash::SpookyHashV2::Hash64(data, size, 0);
}

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

struct MurmurHash {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::murmurHash64(
        reinterpret_cast<const char*>(data), size, 0);
  }
};

} // namespace detail

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  std::deque<std::string> names; // Backing for benchmark names.

#define BENCHMARK_HASH(HASHER) \
  detail::addHashBenchmark<detail::HASHER>(FOLLY_PP_STRINGIZE(HASHER));

  BENCHMARK_HASH(SpookyHashV2);
  BENCHMARK_HASH(FNV64);
  BENCHMARK_HASH(MurmurHash);

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
SpookyHashV2: k=1                                           7.36ns   135.94M
SpookyHashV2: k=2                                           7.47ns   133.91M
SpookyHashV2: k=3                                           7.74ns   129.16M
SpookyHashV2: k=4                                           7.14ns   140.06M
SpookyHashV2: k=5                                           7.52ns   133.05M
SpookyHashV2: k=6                                           7.84ns   127.58M
SpookyHashV2: k=7                                           8.11ns   123.34M
SpookyHashV2: k=8                                           7.24ns   138.09M
SpookyHashV2: k=9                                           7.32ns   136.57M
SpookyHashV2: k=10                                          7.62ns   131.27M
SpookyHashV2: k=11                                          7.84ns   127.58M
SpookyHashV2: k=12                                          8.07ns   123.90M
SpookyHashV2: k=13                                          7.85ns   127.40M
SpookyHashV2: k=14                                          8.02ns   124.76M
SpookyHashV2: k=15                                          8.24ns   121.42M
SpookyHashV2: k=2^0                                         7.18ns   139.28M
SpookyHashV2: k=2^1                                         7.65ns   130.68M
SpookyHashV2: k=2^2                                         7.24ns   138.16M
SpookyHashV2: k=2^3                                         7.40ns   135.15M
SpookyHashV2: k=2^4                                        13.91ns    71.91M
SpookyHashV2: k=2^5                                        14.06ns    71.11M
SpookyHashV2: k=2^6                                        21.83ns    45.81M
SpookyHashV2: k=2^7                                        37.35ns    26.77M
SpookyHashV2: k=2^8                                        47.34ns    21.12M
SpookyHashV2: k=2^9                                        65.66ns    15.23M
SpookyHashV2: k=2^10                                       99.14ns    10.09M
SpookyHashV2: k=2^11                                      172.31ns     5.80M
SpookyHashV2: k=2^12                                      314.87ns     3.18M
SpookyHashV2: k=2^13                                      596.77ns     1.68M
SpookyHashV2: k=2^14                                        1.16us   860.42K
SpookyHashV2: k=2^15                                        2.33us   428.39K
----------------------------------------------------------------------------
FNV64: k=1                                                  1.67ns   597.73M
FNV64: k=2                                                  2.16ns   463.65M
FNV64: k=3                                                  2.98ns   335.84M
FNV64: k=4                                                  3.34ns   299.81M
FNV64: k=5                                                  3.94ns   253.49M
FNV64: k=6                                                  4.50ns   222.04M
FNV64: k=7                                                  5.10ns   196.27M
FNV64: k=8                                                  4.29ns   233.13M
FNV64: k=9                                                  5.16ns   193.88M
FNV64: k=10                                                 5.70ns   175.35M
FNV64: k=11                                                 6.28ns   159.25M
FNV64: k=12                                                 7.32ns   136.54M
FNV64: k=13                                                 8.01ns   124.80M
FNV64: k=14                                                 8.80ns   113.60M
FNV64: k=15                                                 9.42ns   106.17M
FNV64: k=2^0                                                1.66ns   600.75M
FNV64: k=2^1                                                2.21ns   452.85M
FNV64: k=2^2                                                3.40ns   294.24M
FNV64: k=2^3                                                4.33ns   231.13M
FNV64: k=2^4                                                9.42ns   106.15M
FNV64: k=2^5                                               22.39ns    44.66M
FNV64: k=2^6                                               50.47ns    19.81M
FNV64: k=2^7                                              127.87ns     7.82M
FNV64: k=2^8                                              279.80ns     3.57M
FNV64: k=2^9                                              589.47ns     1.70M
FNV64: k=2^10                                               1.22us   817.45K
FNV64: k=2^11                                               2.46us   406.98K
FNV64: k=2^12                                               4.92us   203.27K
FNV64: k=2^13                                               9.84us   101.61K
FNV64: k=2^14                                              19.66us    50.85K
FNV64: k=2^15                                              39.65us    25.22K
----------------------------------------------------------------------------
MurmurHash: k=1                                             1.92ns   520.45M
MurmurHash: k=2                                             2.22ns   451.21M
MurmurHash: k=3                                             2.28ns   437.75M
MurmurHash: k=4                                             1.98ns   504.77M
MurmurHash: k=5                                             2.18ns   458.61M
MurmurHash: k=6                                             2.46ns   406.96M
MurmurHash: k=7                                             2.52ns   396.89M
MurmurHash: k=8                                             2.84ns   352.24M
MurmurHash: k=9                                             3.63ns   275.50M
MurmurHash: k=10                                            3.88ns   257.82M
MurmurHash: k=11                                            4.03ns   248.11M
MurmurHash: k=12                                            3.72ns   268.52M
MurmurHash: k=13                                            3.91ns   255.67M
MurmurHash: k=14                                            4.20ns   238.10M
MurmurHash: k=15                                            4.41ns   226.70M
MurmurHash: k=2^0                                           1.87ns   533.86M
MurmurHash: k=2^1                                           2.17ns   460.14M
MurmurHash: k=2^2                                           1.96ns   510.29M
MurmurHash: k=2^3                                           2.78ns   359.29M
MurmurHash: k=2^4                                           3.84ns   260.18M
MurmurHash: k=2^5                                           5.22ns   191.49M
MurmurHash: k=2^6                                           8.99ns   111.18M
MurmurHash: k=2^7                                          17.05ns    58.63M
MurmurHash: k=2^8                                          32.43ns    30.84M
MurmurHash: k=2^9                                          70.59ns    14.17M
MurmurHash: k=2^10                                        147.21ns     6.79M
MurmurHash: k=2^11                                        301.94ns     3.31M
MurmurHash: k=2^12                                        614.43ns     1.63M
MurmurHash: k=2^13                                          1.23us   810.19K
MurmurHash: k=2^14                                          2.47us   405.39K
MurmurHash: k=2^15                                          4.94us   202.32K
----------------------------------------------------------------------------
#endif
