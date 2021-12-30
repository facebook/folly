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

#include <folly/synchronization/Baton.h>

#include <thread>

#include <folly/Benchmark.h>
#include <folly/synchronization/NativeSemaphore.h>
#include <folly/synchronization/test/BatonTestHelpers.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly::test;
using folly::detail::EmulatedFutexAtomic;

BENCHMARK(baton_pingpong_blocking, iters) {
  run_pingpong_test<true, std::atomic>(iters);
}

BENCHMARK(baton_pingpong_nonblocking, iters) {
  run_pingpong_test<false, std::atomic>(iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(baton_pingpong_emulated_futex_blocking, iters) {
  run_pingpong_test<true, EmulatedFutexAtomic>(iters);
}

BENCHMARK(baton_pingpong_emulated_futex_nonblocking, iters) {
  run_pingpong_test<false, EmulatedFutexAtomic>(iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(native_sem_pingpong, iters) {
  alignas(folly::hardware_destructive_interference_size)
      folly::NativeSemaphore a;
  alignas(folly::hardware_destructive_interference_size)
      folly::NativeSemaphore b;

  auto thr = std::thread([&] {
    for (size_t i = 0; i < iters; ++i) {
      a.wait();
      b.post();
    }
  });
  for (size_t i = 0; i < iters; ++i) {
    a.post();
    b.wait();
  }
  thr.join();
}

// I am omitting a benchmark result snapshot because these microbenchmarks
// mainly illustrate that PreBlockAttempts is very effective for rapid
// handoffs.  The performance of Baton and sem_t is essentially identical
// to the required futex calls for the blocking case

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
