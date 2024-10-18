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
#include <folly/synchronization/test/Barrier.h>

/*
buck run @mode/opt folly/settings/test:settings_bench -- --bm_min_iters=10000000
============================================================================
[...]/settings/test/SettingsBenchmarks.cpp     relative  time/iter   iters/s
============================================================================
trivial_access                                            290.59ps     3.44G
non_trivial_access                                          1.27ns   787.19M
----------------------------------------------------------------------------
trival_access_parallel(1thr)                              482.59ps     2.07G
trival_access_parallel(8thr)                              530.65ps     1.88G
trival_access_parallel(24thr)                             816.65ps     1.22G
trival_access_parallel(48thr)                               1.10ns   911.76M
trival_access_parallel(72thr)                               1.32ns   756.95M
----------------------------------------------------------------------------
non_trival_access_parallel(1thr)                            1.53ns   651.83M
non_trival_access_parallel(8thr)                            1.60ns   623.54M
non_trival_access_parallel(24thr)                           2.36ns   423.37M
non_trival_access_parallel(48thr)                           3.09ns   323.19M
non_trival_access_parallel(72thr)                           3.77ns   265.19M
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
