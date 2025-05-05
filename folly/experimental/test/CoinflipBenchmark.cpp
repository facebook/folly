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

#include <folly/Benchmark.h>
#include <folly/CPortability.h>
#include <folly/Random.h>
#include <folly/experimental/Coinflip.h>

namespace folly {

FOLLY_NOINLINE
bool coinflip_naive(uint32_t wait) {
  if (wait <= 1) {
    return wait == 1;
  }
  return Random::rand32(wait) == 0;
}

FOLLY_NOINLINE bool coinflip_noinline(uint64_t* counter, uint32_t wait) {
  return Coinflip::coinflip(
      /*counter=*/counter,
      /*wait=*/wait,
      /*rng=*/folly::ThreadLocalPRNG());
}

#define BENCH_BOTH(WAIT)                                    \
  BENCHMARK(coinflip_##WAIT##_naive, n) {                   \
    while (n--) {                                           \
      doNotOptimizeAway(coinflip_naive(WAIT));              \
    }                                                       \
  }                                                         \
  BENCHMARK_RELATIVE(coinflip_##WAIT##_folly, n) {          \
    uint64_t counter = 0;                                   \
    while (n--) {                                           \
      doNotOptimizeAway(coinflip_noinline(&counter, WAIT)); \
    }                                                       \
  }

BENCH_BOTH(2)
BENCH_BOTH(3)
BENCH_BOTH(4)
BENCH_BOTH(5)
BENCH_BOTH(7)
BENCH_BOTH(8)
BENCH_BOTH(9)
BENCH_BOTH(10)
BENCH_BOTH(11)
BENCH_BOTH(12)
BENCH_BOTH(13)
BENCH_BOTH(14)
BENCH_BOTH(15)
BENCH_BOTH(16)
BENCH_BOTH(17)
BENCH_BOTH(31)
BENCH_BOTH(32)
BENCH_BOTH(33)
BENCH_BOTH(63)
BENCH_BOTH(64)
BENCH_BOTH(65)
BENCH_BOTH(127)
BENCH_BOTH(128)
BENCH_BOTH(129)
BENCH_BOTH(255)
BENCH_BOTH(256)
BENCH_BOTH(257)
BENCH_BOTH(511)
BENCH_BOTH(512)
BENCH_BOTH(513)
BENCH_BOTH(1023)
BENCH_BOTH(1024)
BENCH_BOTH(1025)
BENCH_BOTH(2047)
BENCH_BOTH(2048)
BENCH_BOTH(2049)

BENCH_BOTH(100)
BENCH_BOTH(1000)
BENCH_BOTH(10000)
BENCH_BOTH(100000)
BENCH_BOTH(1000000)
BENCH_BOTH(10000000)

} // namespace folly

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
