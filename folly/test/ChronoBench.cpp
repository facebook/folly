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

#include <atomic>
#include <chrono>

#include <folly/Benchmark.h>
#include <folly/Chrono.h>
#include <folly/ClockGettimeWrappers.h>
#include <folly/chrono/Hardware.h>
#include <folly/hash/Hash.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>

extern "C" FOLLY_KEEP uint64_t check_folly_hardware_timestamp_measurement() {
  auto const start = folly::hardware_timestamp_measurement_start();
  folly::detail::keep_sink_nx();
  auto const stop = folly::hardware_timestamp_measurement_stop();
  return stop - start;
}

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

BENCHMARK(system_clock_now, iters) {
  uint64_t r = 0;
  while (iters--) {
    using clock = std::chrono::system_clock;
    auto const s = clock::now().time_since_epoch().count();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(coarse_system_clock_now, iters) {
  uint64_t r = 0;
  while (iters--) {
    using clock = folly::chrono::coarse_system_clock;
    auto const s = clock::now().time_since_epoch().count();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

#if defined(CLOCK_MONOTONIC)
BENCHMARK(clock_gettime_monotonic, iters) {
  int64_t r = 0;
  while (iters--) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    r = folly::hash::twang_mix64(r ^ ts.tv_sec ^ ts.tv_nsec);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_MONOTONIC_COARSE)
BENCHMARK(clock_gettime_monotonic_coarse, iters) {
  int64_t r = 0;
  while (iters--) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    r = folly::hash::twang_mix64(r ^ ts.tv_sec ^ ts.tv_nsec);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_REALTIME)
BENCHMARK(clock_gettime_realtime, iters) {
  int64_t r = 0;
  while (iters--) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    r = folly::hash::twang_mix64(r ^ ts.tv_sec ^ ts.tv_nsec);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_REALTIME_COARSE)
BENCHMARK(clock_gettime_realtime_coarse, iters) {
  int64_t r = 0;
  while (iters--) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    r = folly::hash::twang_mix64(r ^ ts.tv_sec ^ ts.tv_nsec);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_MONOTONIC)
BENCHMARK(folly_chrono_clock_gettime_ns_monotonic, iters) {
  int64_t r = 0;
  while (iters--) {
    auto const s = folly::chrono::clock_gettime_ns(CLOCK_MONOTONIC);
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_MONOTONIC_COARSE)
BENCHMARK(folly_chrono_clock_gettime_ns_monotonic_coarse, iters) {
  int64_t r = 0;
  while (iters--) {
    auto const s = folly::chrono::clock_gettime_ns(CLOCK_MONOTONIC_COARSE);
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_REALTIME)
BENCHMARK(folly_chrono_clock_gettime_ns_realtime, iters) {
  int64_t r = 0;
  while (iters--) {
    auto const s = folly::chrono::clock_gettime_ns(CLOCK_REALTIME);
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}
#endif

#if defined(CLOCK_REALTIME_COARSE)
BENCHMARK(folly_chrono_clock_gettime_ns_realtime_coarse, iters) {
  int64_t r = 0;
  while (iters--) {
    auto const s = folly::chrono::clock_gettime_ns(CLOCK_REALTIME_COARSE);
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}
#endif

BENCHMARK(hardware_timestamp_unserialized, iters) {
  uint64_t r = 0;
  while (iters--) {
    auto const s = folly::hardware_timestamp();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(hardware_timestamp_measurement_start, iters) {
  uint64_t r = 0;
  while (iters--) {
    auto const s = folly::hardware_timestamp_measurement_start();
    r = folly::hash::twang_mix64(r ^ s);
  }
  folly::doNotOptimizeAway(r);
}

BENCHMARK(hardware_timestamp_measurement_stop, iters) {
  uint64_t r = 0;
  while (iters--) {
    auto const s = folly::hardware_timestamp_measurement_stop();
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
