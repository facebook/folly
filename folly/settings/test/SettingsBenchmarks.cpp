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

#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/CPortability.h>
#include <folly/init/Init.h>
#include <folly/settings/Settings.h>
#include <folly/settings/test/a.h>
#include <folly/synchronization/test/Barrier.h>

/*
buck run @mode/opt folly/settings/test:settings_bench -- --bm_min_iters=10000000
============================================================================
[...]/settings/test/SettingsBenchmarks.cpp     relative  time/iter   iters/s
============================================================================
trivial_access                                            340.54ps     2.94G
trivial_access_with_observer                              438.07ps     2.28G
trivial_from_another_translation_unit                     286.65ps     3.49G
non_trivial_access                                          1.24ns   808.93M
----------------------------------------------------------------------------
trival_access_parallel(1thr)                              475.86ps     2.10G
trival_access_parallel(8thr)                              519.45ps     1.93G
trival_access_parallel(24thr)                               1.25ns   800.23M
trival_access_parallel(48thr)                               1.27ns   790.17M
trival_access_parallel(72thr)                               1.32ns   756.09M
----------------------------------------------------------------------------
non_trival_access_parallel(1thr)                            1.53ns   654.97M
non_trival_access_parallel(8thr)                            1.56ns   639.60M
non_trival_access_parallel(24thr)                           3.48ns   287.66M
non_trival_access_parallel(48thr)                           3.50ns   285.47M
non_trival_access_parallel(72thr)                           3.44ns   290.68M
*/

FOLLY_SETTING_DEFINE(
    follytest,
    trivial,
    int,
    100,
    folly::settings::Mutability::Mutable,
    folly::settings::CommandLine::AcceptOverrides,
    "desc");

FOLLY_SETTING_DEFINE(
    follytest,
    non_trivial,
    std::string,
    "default",
    folly::settings::Mutability::Mutable,
    folly::settings::CommandLine::AcceptOverrides,
    "desc");

BENCHMARK(trivial_access, iters) {
  for (unsigned int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(*FOLLY_SETTING(follytest, trivial));
  }
}
BENCHMARK(trivial_access_with_observer, iters) {
  for (unsigned int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(
        FOLLY_SETTING(follytest, trivial).valueRegisterObserverDependency());
  }
}
BENCHMARK(trivial_from_another_translation_unit, iters) {
  for (unsigned int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(*a_ns::FOLLY_SETTING(follytest, public_flag_to_a));
  }
}

BENCHMARK(non_trivial_access, iters) {
  for (unsigned int i = 0; i < iters; ++i) {
    folly::doNotOptimizeAway(*FOLLY_SETTING(follytest, non_trivial));
  }
}

template <typename Func>
void parallel(size_t numThreads, const Func& func) {
  folly::BenchmarkSuspender suspender;
  std::vector<std::thread> threads;
  folly::test::Barrier barrier(numThreads + 1);
  for (size_t i = 0; i < numThreads; ++i) {
    threads.emplace_back([&]() {
      barrier.wait(); // A
      func();
      barrier.wait(); // B
    });
  }
  barrier.wait(); // A
  suspender.dismissing([&] {
    barrier.wait(); // B
  });
  for (auto& thread : threads) {
    thread.join();
  }
}

FOLLY_NOINLINE void trival_access_parallel(size_t iters, size_t nThreads) {
  parallel(nThreads, [&] {
    for (size_t i = 0; i < iters; ++i) {
      folly::doNotOptimizeAway(*FOLLY_SETTING(follytest, trivial));
    }
  });
}
FOLLY_NOINLINE void non_trival_access_parallel(size_t iters, size_t nThreads) {
  parallel(nThreads, [&] {
    for (size_t i = 0; i < iters; ++i) {
      folly::doNotOptimizeAway(*FOLLY_SETTING(follytest, non_trivial));
    }
  });
}

#define BENCH_PARALLEL(func)             \
  BENCHMARK_DRAW_LINE();                 \
  BENCHMARK_NAMED_PARAM(func, 1thr, 1)   \
  BENCHMARK_NAMED_PARAM(func, 8thr, 8)   \
  BENCHMARK_NAMED_PARAM(func, 24thr, 24) \
  BENCHMARK_NAMED_PARAM(func, 48thr, 48) \
  BENCHMARK_NAMED_PARAM(func, 72thr, 72)

BENCH_PARALLEL(trival_access_parallel)
BENCH_PARALLEL(non_trival_access_parallel)

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();

  return 0;
}
