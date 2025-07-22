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
#include <folly/hash/rapidhash.h>

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
check_folly_hash_128_to_64(uint64_t upper, uint64_t lower) {
  return folly::hash::hash_128_to_64(upper, lower);
}

extern "C" FOLLY_KEEP uint64_t
check_folly_commutative_hash_128_to_64(uint64_t upper, uint64_t lower) {
  return folly::hash::commutative_hash_128_to_64(upper, lower);
}

extern "C" FOLLY_KEEP uint64_t check_folly_twang_mix64(uint64_t key) {
  return folly::hash::twang_mix64(key);
}

extern "C" FOLLY_KEEP uint64_t check_folly_twang_unmix64(uint64_t key) {
  return folly::hash::twang_unmix64(key);
}

extern "C" FOLLY_KEEP uint32_t check_folly_twang_32from64(uint64_t key) {
  return folly::hash::twang_32from64(key);
}

extern "C" FOLLY_KEEP uint32_t check_folly_jenkins_rev_mix32(uint32_t key) {
  return folly::hash::jenkins_rev_mix32(key);
}

extern "C" FOLLY_KEEP uint32_t check_folly_jenkins_rev_unmix32(uint32_t key) {
  return folly::hash::jenkins_rev_unmix32(key);
}

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
void addHashBenchmark(const std::string& hasherName) {
  for (size_t k = 1; k < 16; ++k) {
    std::string name = fmt::format("{}: k={}", hasherName, k);
    folly::addBenchmark(__FILE__, name, [=](unsigned iters) {
      Hasher hasher;
      bmHasher(hasher, k, iters);
      return iters;
    });
  }

  for (size_t i = 0; i < 21; ++i) {
    auto k = size_t(1) << i;
    std::string name = fmt::format("{}: k=2^{}", hasherName, i);
    folly::addBenchmark(__FILE__, name, [=](unsigned iters) {
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
    return folly::hash::fnv64_buf_BROKEN(data, size);
  }
};

struct MurmurHash {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::murmurHash64(
        reinterpret_cast<const char*>(data), size, 0);
  }
};

struct RapidHash {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::rapidhash(reinterpret_cast<const char*>(data), size);
  }
};

struct RapidHashMicro {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::rapidhashMicro(
        reinterpret_cast<const char*>(data), size);
  }
};

struct RapidHashNano {
  uint64_t operator()(const uint8_t* data, size_t size) const {
    return folly::hash::rapidhashNano(
        reinterpret_cast<const char*>(data), size);
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
  BENCHMARK_HASH(RapidHash);
  BENCHMARK_HASH(RapidHashMicro);
  BENCHMARK_HASH(RapidHashNano);

#undef BENCHMARK_HASH

  folly::runBenchmarks();

  return 0;
}

#if 0
AMD EPYC 9000 series CPU
$ hash_benchmark --bm_min_usec=100000
============================================================================
fbcode/folly/hash/test/HashBenchmark.cpp     relative  time/iter   iters/s
============================================================================
SpookyHashV2: k=1                                           5.80ns   172.36M
SpookyHashV2: k=2                                           6.18ns   161.74M
SpookyHashV2: k=3                                           6.36ns   157.12M
SpookyHashV2: k=4                                           5.68ns   175.90M
SpookyHashV2: k=5                                           6.18ns   161.70M
SpookyHashV2: k=6                                           6.38ns   156.71M
SpookyHashV2: k=7                                           6.59ns   151.76M
SpookyHashV2: k=8                                           5.70ns   175.40M
SpookyHashV2: k=9                                           5.85ns   170.82M
SpookyHashV2: k=10                                          6.23ns   160.51M
SpookyHashV2: k=11                                          6.37ns   156.97M
SpookyHashV2: k=12                                          5.87ns   170.32M
SpookyHashV2: k=13                                          6.23ns   160.47M
SpookyHashV2: k=14                                          6.36ns   157.19M
SpookyHashV2: k=15                                          6.65ns   150.32M
SpookyHashV2: k=2^0                                         5.80ns   172.43M
SpookyHashV2: k=2^1                                         6.18ns   161.81M
SpookyHashV2: k=2^2                                         5.68ns   175.97M
SpookyHashV2: k=2^3                                         5.70ns   175.51M
SpookyHashV2: k=2^4                                        11.98ns    83.48M
SpookyHashV2: k=2^5                                        11.36ns    88.06M
SpookyHashV2: k=2^6                                        18.85ns    53.06M
SpookyHashV2: k=2^7                                        35.96ns    27.81M
SpookyHashV2: k=2^8                                        48.41ns    20.66M
SpookyHashV2: k=2^9                                        69.53ns    14.38M
SpookyHashV2: k=2^10                                      104.62ns     9.56M
SpookyHashV2: k=2^11                                      184.95ns     5.41M
SpookyHashV2: k=2^12                                      330.11ns     3.03M
SpookyHashV2: k=2^13                                      634.08ns     1.58M
SpookyHashV2: k=2^14                                        1.23us   813.28K
SpookyHashV2: k=2^15                                        2.44us   410.27K
SpookyHashV2: k=2^16                                        4.83us   206.89K
SpookyHashV2: k=2^17                                        9.63us   103.86K
SpookyHashV2: k=2^18                                       19.21us    52.05K
SpookyHashV2: k=2^19                                       38.45us    26.01K
SpookyHashV2: k=2^20                                       77.18us    12.96K
----------------------------------------------------------------------------
FNV64: k=1                                                  1.12ns   893.79M
FNV64: k=2                                                  1.64ns   608.67M
FNV64: k=3                                                  2.22ns   450.32M
FNV64: k=4                                                  2.66ns   376.63M
FNV64: k=5                                                  3.22ns   310.52M
FNV64: k=6                                                  3.91ns   255.89M
FNV64: k=7                                                  4.75ns   210.39M
FNV64: k=8                                                  4.75ns   210.61M
FNV64: k=9                                                  5.62ns   177.82M
FNV64: k=10                                                 6.02ns   166.23M
FNV64: k=11                                                 6.86ns   145.80M
FNV64: k=12                                                 9.04ns   110.62M
FNV64: k=13                                                 9.97ns   100.32M
FNV64: k=14                                                10.55ns    94.77M
FNV64: k=15                                                11.19ns    89.34M
FNV64: k=2^0                                                1.12ns   894.46M
FNV64: k=2^1                                                1.64ns   608.43M
FNV64: k=2^2                                                2.66ns   376.59M
FNV64: k=2^3                                                4.75ns   210.71M
FNV64: k=2^4                                               11.56ns    86.50M
FNV64: k=2^5                                               22.44ns    44.56M
FNV64: k=2^6                                               61.72ns    16.20M
FNV64: k=2^7                                              149.54ns     6.69M
FNV64: k=2^8                                              324.43ns     3.08M
FNV64: k=2^9                                              675.13ns     1.48M
FNV64: k=2^10                                               1.38us   726.87K
FNV64: k=2^11                                               2.78us   359.99K
FNV64: k=2^12                                               5.59us   179.04K
FNV64: k=2^13                                              11.19us    89.34K
FNV64: k=2^14                                              22.41us    44.62K
FNV64: k=2^15                                              44.84us    22.30K
FNV64: k=2^16                                              89.69us    11.15K
FNV64: k=2^17                                             179.43us     5.57K
FNV64: k=2^18                                             358.91us     2.79K
FNV64: k=2^19                                             717.66us     1.39K
FNV64: k=2^20                                               1.44ms    696.51
----------------------------------------------------------------------------
MurmurHash: k=1                                             1.60ns   623.43M
MurmurHash: k=2                                             1.88ns   530.71M
MurmurHash: k=3                                             1.96ns   509.79M
MurmurHash: k=4                                             1.66ns   602.64M
MurmurHash: k=5                                             2.05ns   486.80M
MurmurHash: k=6                                             1.89ns   530.06M
MurmurHash: k=7                                             2.22ns   450.01M
MurmurHash: k=8                                             2.65ns   378.05M
MurmurHash: k=9                                             3.28ns   304.85M
MurmurHash: k=10                                            3.63ns   275.54M
MurmurHash: k=11                                            3.56ns   280.71M
MurmurHash: k=12                                            3.20ns   312.74M
MurmurHash: k=13                                            3.55ns   281.31M
MurmurHash: k=14                                            3.77ns   265.45M
MurmurHash: k=15                                            3.98ns   251.25M
MurmurHash: k=2^0                                           1.60ns   623.43M
MurmurHash: k=2^1                                           1.88ns   530.67M
MurmurHash: k=2^2                                           1.66ns   602.61M
MurmurHash: k=2^3                                           2.64ns   378.10M
MurmurHash: k=2^4                                           3.83ns   260.84M
MurmurHash: k=2^5                                           6.04ns   165.59M
MurmurHash: k=2^6                                          11.38ns    87.85M
MurmurHash: k=2^7                                          19.87ns    50.33M
MurmurHash: k=2^8                                          37.13ns    26.93M
MurmurHash: k=2^9                                          78.16ns    12.79M
MurmurHash: k=2^10                                        165.82ns     6.03M
MurmurHash: k=2^11                                        340.98ns     2.93M
MurmurHash: k=2^12                                        691.62ns     1.45M
MurmurHash: k=2^13                                          1.39us   718.14K
MurmurHash: k=2^14                                          2.80us   357.25K
MurmurHash: k=2^15                                          5.61us   178.20K
MurmurHash: k=2^16                                         11.22us    89.09K
MurmurHash: k=2^17                                         22.44us    44.56K
MurmurHash: k=2^18                                         44.87us    22.29K
MurmurHash: k=2^19                                         89.77us    11.14K
MurmurHash: k=2^20                                        179.86us     5.56K
----------------------------------------------------------------------------
RapidHash: k=1                                              3.43ns   291.75M
RapidHash: k=2                                              3.43ns   291.76M
RapidHash: k=3                                              3.43ns   291.76M
RapidHash: k=4                                              3.08ns   324.23M
RapidHash: k=5                                              3.08ns   324.21M
RapidHash: k=6                                              3.09ns   324.13M
RapidHash: k=7                                              3.09ns   323.89M
RapidHash: k=8                                              3.09ns   323.80M
RapidHash: k=9                                              3.09ns   323.82M
RapidHash: k=10                                             3.09ns   323.81M
RapidHash: k=11                                             3.09ns   324.05M
RapidHash: k=12                                             3.09ns   324.14M
RapidHash: k=13                                             3.09ns   324.05M
RapidHash: k=14                                             3.09ns   324.07M
RapidHash: k=15                                             3.09ns   324.08M
RapidHash: k=2^0                                            3.43ns   291.77M
RapidHash: k=2^1                                            3.43ns   291.83M
RapidHash: k=2^2                                            3.08ns   324.24M
RapidHash: k=2^3                                            3.09ns   323.97M
RapidHash: k=2^4                                            3.09ns   324.06M
RapidHash: k=2^5                                            4.80ns   208.47M
RapidHash: k=2^6                                            5.55ns   180.23M
RapidHash: k=2^7                                            8.65ns   115.62M
RapidHash: k=2^8                                           15.47ns    64.63M
RapidHash: k=2^9                                           27.17ns    36.80M
RapidHash: k=2^10                                          50.30ns    19.88M
RapidHash: k=2^11                                          96.09ns    10.41M
RapidHash: k=2^12                                         188.00ns     5.32M
RapidHash: k=2^13                                         355.37ns     2.81M
RapidHash: k=2^14                                         709.39ns     1.41M
RapidHash: k=2^15                                           1.42us   706.36K
RapidHash: k=2^16                                           2.82us   354.93K
RapidHash: k=2^17                                           5.64us   177.36K
RapidHash: k=2^18                                          11.25us    88.90K
RapidHash: k=2^19                                          22.50us    44.44K
RapidHash: k=2^20                                          45.54us    21.96K
----------------------------------------------------------------------------
RapidHashMicro: k=1                                         2.74ns   364.96M
RapidHashMicro: k=2                                         2.74ns   364.96M
RapidHashMicro: k=3                                         2.74ns   364.92M
RapidHashMicro: k=4                                         2.41ns   415.65M
RapidHashMicro: k=5                                         2.41ns   415.68M
RapidHashMicro: k=6                                         2.41ns   415.59M
RapidHashMicro: k=7                                         2.41ns   415.59M
RapidHashMicro: k=8                                         2.40ns   415.93M
RapidHashMicro: k=9                                         2.40ns   415.93M
RapidHashMicro: k=10                                        2.41ns   415.79M
RapidHashMicro: k=11                                        2.41ns   415.79M
RapidHashMicro: k=12                                        2.40ns   415.89M
RapidHashMicro: k=13                                        2.40ns   415.93M
RapidHashMicro: k=14                                        2.40ns   415.85M
RapidHashMicro: k=15                                        2.40ns   415.90M
RapidHashMicro: k=2^0                                       2.74ns   364.83M
RapidHashMicro: k=2^1                                       2.74ns   365.38M
RapidHashMicro: k=2^2                                       2.41ns   415.76M
RapidHashMicro: k=2^3                                       2.40ns   416.03M
RapidHashMicro: k=2^4                                       2.40ns   415.85M
RapidHashMicro: k=2^5                                       3.77ns   264.93M
RapidHashMicro: k=2^6                                       4.78ns   209.14M
RapidHashMicro: k=2^7                                       8.66ns   115.44M
RapidHashMicro: k=2^8                                      15.20ns    65.79M
RapidHashMicro: k=2^9                                      25.15ns    39.77M
RapidHashMicro: k=2^10                                     46.68ns    21.42M
RapidHashMicro: k=2^11                                     88.21ns    11.34M
RapidHashMicro: k=2^12                                    171.90ns     5.82M
RapidHashMicro: k=2^13                                    338.56ns     2.95M
RapidHashMicro: k=2^14                                    672.92ns     1.49M
RapidHashMicro: k=2^15                                      1.34us   746.37K
RapidHashMicro: k=2^16                                      2.68us   372.97K
RapidHashMicro: k=2^17                                      5.34us   187.15K
RapidHashMicro: k=2^18                                     10.67us    93.75K
RapidHashMicro: k=2^19                                     21.37us    46.79K
RapidHashMicro: k=2^20                                     43.10us    23.20K
----------------------------------------------------------------------------
RapidHashNano: k=1                                          2.74ns   364.98M
RapidHashNano: k=2                                          2.74ns   364.94M
RapidHashNano: k=3                                          2.74ns   364.92M
RapidHashNano: k=4                                          2.41ns   415.68M
RapidHashNano: k=5                                          2.41ns   415.78M
RapidHashNano: k=6                                          2.41ns   415.67M
RapidHashNano: k=7                                          2.41ns   415.69M
RapidHashNano: k=8                                          2.40ns   415.98M
RapidHashNano: k=9                                          2.40ns   415.99M
RapidHashNano: k=10                                         2.40ns   415.94M
RapidHashNano: k=11                                         2.40ns   415.93M
RapidHashNano: k=12                                         2.40ns   415.91M
RapidHashNano: k=13                                         2.40ns   415.91M
RapidHashNano: k=14                                         2.40ns   415.86M
RapidHashNano: k=15                                         2.40ns   415.97M
RapidHashNano: k=2^0                                        2.74ns   365.00M
RapidHashNano: k=2^1                                        2.74ns   364.93M
RapidHashNano: k=2^2                                        2.41ns   415.60M
RapidHashNano: k=2^3                                        2.40ns   416.09M
RapidHashNano: k=2^4                                        2.40ns   415.87M
RapidHashNano: k=2^5                                        3.44ns   290.83M
RapidHashNano: k=2^6                                        5.05ns   198.00M
RapidHashNano: k=2^7                                        8.36ns   119.63M
RapidHashNano: k=2^8                                       14.69ns    68.09M
RapidHashNano: k=2^9                                       25.86ns    38.67M
RapidHashNano: k=2^10                                      49.95ns    20.02M
RapidHashNano: k=2^11                                      97.24ns    10.28M
RapidHashNano: k=2^12                                     189.93ns     5.27M
RapidHashNano: k=2^13                                     375.24ns     2.66M
RapidHashNano: k=2^14                                     744.88ns     1.34M
RapidHashNano: k=2^15                                       1.52us   657.09K
RapidHashNano: k=2^16                                       3.02us   331.24K
RapidHashNano: k=2^17                                       5.99us   166.97K
RapidHashNano: k=2^18                                      11.97us    83.55K
RapidHashNano: k=2^19                                      24.84us    40.25K
RapidHashNano: k=2^20                                      49.12us    20.36K
----------------------------------------------------------------------------

ARM Neoverse-V2 CPU
============================================================================
fbcode/folly/hash/test/HashBenchmark.cpp     relative  time/iter   iters/s
============================================================================
SpookyHashV2: k=1                                           4.86ns   205.71M
SpookyHashV2: k=2                                           4.97ns   201.06M
SpookyHashV2: k=3                                           5.15ns   194.04M
SpookyHashV2: k=4                                           4.83ns   207.19M
SpookyHashV2: k=5                                           4.99ns   200.50M
SpookyHashV2: k=6                                           5.13ns   194.97M
SpookyHashV2: k=7                                           5.39ns   185.54M
SpookyHashV2: k=8                                           4.81ns   207.87M
SpookyHashV2: k=9                                           4.99ns   200.58M
SpookyHashV2: k=10                                          5.15ns   194.27M
SpookyHashV2: k=11                                          5.35ns   186.89M
SpookyHashV2: k=12                                          4.97ns   201.36M
SpookyHashV2: k=13                                          5.07ns   197.39M
SpookyHashV2: k=14                                          5.26ns   190.25M
SpookyHashV2: k=15                                          5.54ns   180.64M
SpookyHashV2: k=2^0                                         4.84ns   206.49M
SpookyHashV2: k=2^1                                         5.00ns   200.15M
SpookyHashV2: k=2^2                                         4.81ns   207.98M
SpookyHashV2: k=2^3                                         4.86ns   205.85M
SpookyHashV2: k=2^4                                        10.02ns    99.80M
SpookyHashV2: k=2^5                                        10.51ns    95.19M
SpookyHashV2: k=2^6                                        16.95ns    59.01M
SpookyHashV2: k=2^7                                        32.35ns    30.91M
SpookyHashV2: k=2^8                                        36.11ns    27.69M
SpookyHashV2: k=2^9                                        52.96ns    18.88M
SpookyHashV2: k=2^10                                       80.89ns    12.36M
SpookyHashV2: k=2^11                                      146.00ns     6.85M
SpookyHashV2: k=2^12                                      267.59ns     3.74M
SpookyHashV2: k=2^13                                      520.53ns     1.92M
SpookyHashV2: k=2^14                                        1.02us   978.70K
SpookyHashV2: k=2^15                                        2.03us   491.99K
SpookyHashV2: k=2^16                                        4.03us   247.85K
SpookyHashV2: k=2^17                                        8.07us   123.93K
SpookyHashV2: k=2^18                                       15.99us    62.54K
SpookyHashV2: k=2^19                                       31.84us    31.41K
SpookyHashV2: k=2^20                                       64.26us    15.56K
----------------------------------------------------------------------------
FNV64: k=1                                                900.40ps     1.11G
FNV64: k=2                                                  1.22ns   816.61M
FNV64: k=3                                                  1.59ns   629.80M
FNV64: k=4                                                  1.97ns   507.80M
FNV64: k=5                                                  2.37ns   422.18M
FNV64: k=6                                                  2.79ns   358.69M
FNV64: k=7                                                  3.18ns   314.06M
FNV64: k=8                                                  3.63ns   275.29M
FNV64: k=9                                                  4.07ns   245.44M
FNV64: k=10                                                 4.55ns   219.75M
FNV64: k=11                                                 5.06ns   197.64M
FNV64: k=12                                                 5.57ns   179.52M
FNV64: k=13                                                 6.09ns   164.21M
FNV64: k=14                                                 6.56ns   152.46M
FNV64: k=15                                                 7.08ns   141.23M
FNV64: k=2^0                                              897.97ps     1.11G
FNV64: k=2^1                                                1.24ns   807.58M
FNV64: k=2^2                                                1.94ns   514.85M
FNV64: k=2^3                                                3.59ns   278.28M
FNV64: k=2^4                                                7.75ns   128.98M
FNV64: k=2^5                                               17.59ns    56.86M
FNV64: k=2^6                                               40.59ns    24.64M
FNV64: k=2^7                                               98.97ns    10.10M
FNV64: k=2^8                                              218.46ns     4.58M
FNV64: k=2^9                                              458.28ns     2.18M
FNV64: k=2^10                                             933.57ns     1.07M
FNV64: k=2^11                                               1.90us   526.35K
FNV64: k=2^12                                               3.80us   263.17K
FNV64: k=2^13                                               7.62us   131.16K
FNV64: k=2^14                                              15.32us    65.27K
FNV64: k=2^15                                              30.47us    32.82K
FNV64: k=2^16                                              61.54us    16.25K
FNV64: k=2^17                                             122.80us     8.14K
FNV64: k=2^18                                             244.89us     4.08K
FNV64: k=2^19                                             490.45us     2.04K
FNV64: k=2^20                                             974.42us     1.03K
----------------------------------------------------------------------------
MurmurHash: k=1                                           800.93ps     1.25G
MurmurHash: k=2                                           839.94ps     1.19G
MurmurHash: k=3                                           943.71ps     1.06G
MurmurHash: k=4                                           940.77ps     1.06G
MurmurHash: k=5                                           934.52ps     1.07G
MurmurHash: k=6                                           930.07ps     1.08G
MurmurHash: k=7                                             1.06ns   945.85M
MurmurHash: k=8                                             1.36ns   736.43M
MurmurHash: k=9                                             1.69ns   591.59M
MurmurHash: k=10                                            1.94ns   516.57M
MurmurHash: k=11                                            2.11ns   475.03M
MurmurHash: k=12                                            1.74ns   574.93M
MurmurHash: k=13                                            1.82ns   548.98M
MurmurHash: k=14                                            2.09ns   479.18M
MurmurHash: k=15                                            2.17ns   460.00M
MurmurHash: k=2^0                                         797.67ps     1.25G
MurmurHash: k=2^1                                         847.22ps     1.18G
MurmurHash: k=2^2                                         933.75ps     1.07G
MurmurHash: k=2^3                                           1.35ns   738.51M
MurmurHash: k=2^4                                           2.02ns   495.93M
MurmurHash: k=2^5                                           3.26ns   306.59M
MurmurHash: k=2^6                                           6.00ns   166.56M
MurmurHash: k=2^7                                          11.28ns    88.63M
MurmurHash: k=2^8                                          22.85ns    43.76M
MurmurHash: k=2^9                                          48.04ns    20.82M
MurmurHash: k=2^10                                        109.83ns     9.11M
MurmurHash: k=2^11                                        230.43ns     4.34M
MurmurHash: k=2^12                                        466.45ns     2.14M
MurmurHash: k=2^13                                        947.20ns     1.06M
MurmurHash: k=2^14                                          1.90us   525.52K
MurmurHash: k=2^15                                          3.84us   260.44K
MurmurHash: k=2^16                                          7.68us   130.25K
MurmurHash: k=2^17                                         15.28us    65.44K
MurmurHash: k=2^18                                         30.80us    32.47K
MurmurHash: k=2^19                                         60.93us    16.41K
MurmurHash: k=2^20                                        123.62us     8.09K
----------------------------------------------------------------------------
RapidHash: k=1                                              1.83ns   547.33M
RapidHash: k=2                                              1.83ns   545.42M
RapidHash: k=3                                              1.83ns   547.41M
RapidHash: k=4                                              1.74ns   576.05M
RapidHash: k=5                                              1.75ns   572.67M
RapidHash: k=6                                              1.75ns   571.89M
RapidHash: k=7                                              1.74ns   574.81M
RapidHash: k=8                                              1.75ns   572.97M
RapidHash: k=9                                              1.75ns   569.93M
RapidHash: k=10                                             1.75ns   571.37M
RapidHash: k=11                                             1.75ns   572.62M
RapidHash: k=12                                             1.75ns   570.81M
RapidHash: k=13                                             1.74ns   575.19M
RapidHash: k=14                                             1.75ns   571.86M
RapidHash: k=15                                             1.76ns   569.45M
RapidHash: k=2^0                                            1.83ns   546.84M
RapidHash: k=2^1                                            1.84ns   542.67M
RapidHash: k=2^2                                            1.75ns   571.55M
RapidHash: k=2^3                                            1.75ns   570.59M
RapidHash: k=2^4                                            1.74ns   573.73M
RapidHash: k=2^5                                            6.06ns   164.97M
RapidHash: k=2^6                                            7.32ns   136.53M
RapidHash: k=2^7                                            8.56ns   116.83M
RapidHash: k=2^8                                           11.95ns    83.67M
RapidHash: k=2^9                                           21.28ns    47.00M
RapidHash: k=2^10                                          32.94ns    30.36M
RapidHash: k=2^11                                          60.44ns    16.55M
RapidHash: k=2^12                                         118.04ns     8.47M
RapidHash: k=2^13                                         228.19ns     4.38M
RapidHash: k=2^14                                         450.62ns     2.22M
RapidHash: k=2^15                                         892.95ns     1.12M
RapidHash: k=2^16                                           1.78us   563.35K
RapidHash: k=2^17                                           3.64us   274.81K
RapidHash: k=2^18                                           7.28us   137.45K
RapidHash: k=2^19                                          14.72us    67.94K
RapidHash: k=2^20                                          29.68us    33.69K
----------------------------------------------------------------------------
RapidHashMicro: k=1                                         1.81ns   553.77M
RapidHashMicro: k=2                                         1.80ns   555.24M
RapidHashMicro: k=3                                         1.80ns   554.39M
RapidHashMicro: k=4                                         1.75ns   572.25M
RapidHashMicro: k=5                                         1.74ns   574.47M
RapidHashMicro: k=6                                         1.75ns   571.09M
RapidHashMicro: k=7                                         1.73ns   577.13M
RapidHashMicro: k=8                                         1.74ns   575.04M
RapidHashMicro: k=9                                         1.73ns   577.82M
RapidHashMicro: k=10                                        1.74ns   574.97M
RapidHashMicro: k=11                                        1.73ns   577.59M
RapidHashMicro: k=12                                        1.73ns   579.19M
RapidHashMicro: k=13                                        1.72ns   580.85M
RapidHashMicro: k=14                                        1.75ns   570.49M
RapidHashMicro: k=15                                        1.74ns   575.08M
RapidHashMicro: k=2^0                                       1.81ns   551.19M
RapidHashMicro: k=2^1                                       1.81ns   551.41M
RapidHashMicro: k=2^2                                       1.73ns   578.13M
RapidHashMicro: k=2^3                                       1.72ns   582.30M
RapidHashMicro: k=2^4                                       1.74ns   574.35M
RapidHashMicro: k=2^5                                       2.80ns   357.71M
RapidHashMicro: k=2^6                                       4.01ns   249.14M
RapidHashMicro: k=2^7                                       8.09ns   123.54M
RapidHashMicro: k=2^8                                      11.92ns    83.86M
RapidHashMicro: k=2^9                                      19.72ns    50.70M
RapidHashMicro: k=2^10                                     36.74ns    27.22M
RapidHashMicro: k=2^11                                     66.32ns    15.08M
RapidHashMicro: k=2^12                                    127.50ns     7.84M
RapidHashMicro: k=2^13                                    250.60ns     3.99M
RapidHashMicro: k=2^14                                    499.14ns     2.00M
RapidHashMicro: k=2^15                                    992.53ns     1.01M
RapidHashMicro: k=2^16                                      1.98us   504.40K
RapidHashMicro: k=2^17                                      4.01us   249.55K
RapidHashMicro: k=2^18                                      8.02us   124.75K
RapidHashMicro: k=2^19                                     16.20us    61.74K
RapidHashMicro: k=2^20                                     32.38us    30.88K
----------------------------------------------------------------------------
RapidHashNano: k=1                                          1.83ns   547.40M
RapidHashNano: k=2                                          1.83ns   547.34M
RapidHashNano: k=3                                          1.83ns   547.45M
RapidHashNano: k=4                                          1.73ns   578.94M
RapidHashNano: k=5                                          1.73ns   577.38M
RapidHashNano: k=6                                          1.73ns   578.66M
RapidHashNano: k=7                                          1.72ns   579.98M
RapidHashNano: k=8                                          1.75ns   570.16M
RapidHashNano: k=9                                          1.73ns   577.08M
RapidHashNano: k=10                                         1.74ns   575.20M
RapidHashNano: k=11                                         1.73ns   577.25M
RapidHashNano: k=12                                         1.73ns   577.37M
RapidHashNano: k=13                                         1.75ns   573.06M
RapidHashNano: k=14                                         1.73ns   578.76M
RapidHashNano: k=15                                         1.73ns   579.37M
RapidHashNano: k=2^0                                        1.83ns   546.93M
RapidHashNano: k=2^1                                        1.82ns   550.47M
RapidHashNano: k=2^2                                        1.71ns   584.00M
RapidHashNano: k=2^3                                        1.73ns   577.64M
RapidHashNano: k=2^4                                        1.74ns   576.14M
RapidHashNano: k=2^5                                        2.76ns   362.17M
RapidHashNano: k=2^6                                        4.68ns   213.54M
RapidHashNano: k=2^7                                        6.92ns   144.61M
RapidHashNano: k=2^8                                       11.37ns    87.97M
RapidHashNano: k=2^9                                       20.72ns    48.27M
RapidHashNano: k=2^10                                      39.52ns    25.30M
RapidHashNano: k=2^11                                      78.77ns    12.70M
RapidHashNano: k=2^12                                     154.82ns     6.46M
RapidHashNano: k=2^13                                     308.12ns     3.25M
RapidHashNano: k=2^14                                     617.76ns     1.62M
RapidHashNano: k=2^15                                       1.23us   812.54K
RapidHashNano: k=2^16                                       2.46us   406.14K
RapidHashNano: k=2^17                                       4.91us   203.54K
RapidHashNano: k=2^18                                       9.82us   101.82K
RapidHashNano: k=2^19                                      19.82us    50.46K
RapidHashNano: k=2^20                                      39.76us    25.15K
----------------------------------------------------------------------------

#endif
