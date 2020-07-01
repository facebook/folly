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

#include <atomic>
#include <chrono>

#include <folly/Benchmark.h>
#include <folly/Chrono.h>
#include <folly/chrono/Hardware.h>
#include <folly/hash/Hash.h>
#include <folly/init/Init.h>

BENCHMARK(steady_clock_now, iters) {
  uint64_t r = 0;
  while (iters--) {
    using clock = std::chrono::steady_clock;
    auto const s = clock::now().time_since_epoch().count();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(coarse_steady_clock_now, iters) {
  uint64_t r = 0;
  while (iters--) {
    using clock = folly::chrono::coarse_steady_clock;
    auto const s = clock::now().time_since_epoch().count();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(hardware_timestamp_unserialized, iters) {
  uint64_t r = 0;
  while (iters--) {
    auto const s = folly::hardware_timestamp();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(atomic_load_relaxed, iters) {
  std::atomic<uint64_t> now{0};
  uint64_t r = 0;
  while (iters--) {
    auto const s = now.load(std::memory_order_relaxed);
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
