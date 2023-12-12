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

#include <random>
#include <glog/logging.h>
#include <folly/Benchmark.h>
#include <folly/hash/Checksum.h>

constexpr size_t kBufSize = 512 * 1024;
uint8_t* buf;

#define BENCH_CRC32(S) \
  BENCHMARK(crc32_##S) { folly::doNotOptimizeAway(folly::crc32(buf, (S), 2)); }

#define BENCH_CRC32C(S)                                   \
  BENCHMARK(crc32c_##S) {                                 \
    folly::doNotOptimizeAway(folly::crc32c(buf, (S), 2)); \
  }

BENCH_CRC32(512)
BENCH_CRC32(1024)
BENCH_CRC32(2048)
BENCH_CRC32(4096)
BENCH_CRC32(8192)
BENCH_CRC32(16384)
BENCH_CRC32(32768)
BENCH_CRC32(65536)
BENCH_CRC32(131072)
BENCH_CRC32(262144)
BENCH_CRC32(524288)

BENCH_CRC32C(512)
BENCH_CRC32C(1024)
BENCH_CRC32C(2048)
BENCH_CRC32C(4096)
BENCH_CRC32C(8192)
BENCH_CRC32C(16384)
BENCH_CRC32C(32768)
BENCH_CRC32C(65536)
BENCH_CRC32C(131072)
BENCH_CRC32C(262144)
BENCH_CRC32C(524288)

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  buf = static_cast<uint8_t*>(std::aligned_alloc(4096, kBufSize + 64));

  std::default_random_engine rng(1729); // Deterministic seed.
  std::uniform_int_distribution<uint16_t> dist(0, 255);
  std::generate(buf, buf + kBufSize, [&]() { return dist(rng); });

  folly::runBenchmarks();

  std::free(buf);

  return 0;
}

/*
Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz
$ checksum_benchmark --bm_min_usec=10000
============================================================================
folly/hash/test/ChecksumBenchmark.cpp           relative  time/iter  iters/s
============================================================================
crc32_512                                                   55.73ns   17.94M
crc32_1024                                                  85.15ns   11.74M
crc32_2048                                                 116.29ns    8.60M
crc32_4096                                                 191.03ns    5.23M
crc32_8192                                                 341.44ns    2.93M
crc32_16384                                                627.76ns    1.59M
crc32_32768                                                  1.21us  827.16K
============================================================================
*/
