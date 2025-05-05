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

#include <folly/experimental/SingletonRelaxedCounter.h>

#include <cstddef>
#include <thread>

#include <boost/thread/barrier.hpp>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>
#include <folly/portability/GFlags.h>

DEFINE_int32(num_threads, 4, "No. of threads allocting the objects");

namespace {
struct PrivateTag {};
} // namespace

using Counter = folly::SingletonRelaxedCounter<size_t, PrivateTag>;

// small wrappers around the functions being benchmarked
// useful for looking at the inlined native code of the fast path
extern "C" FOLLY_KEEP void check_singleton_relaxed_counter_add_1() {
  Counter::add(1);
}
extern "C" FOLLY_KEEP void check_singleton_relaxed_counter_add(size_t i) {
  Counter::add(i);
}

namespace folly {

// a noop which the compiler cannot see through
// used to prevent some compiler optimizations which could skew benchmarks
[[gnu::weak]] void noop() noexcept {}

BENCHMARK(MultiThreadPerformance, iters) {
  BenchmarkSuspender braces;

  boost::barrier barrier(1 + FLAGS_num_threads);

  std::vector<std::thread> threads(FLAGS_num_threads);

  for (auto& thread : threads) {
    thread = std::thread([&] {
      auto once = [] {
        noop();
        Counter::add(1);
        noop();
        Counter::sub(1);
      };

      // in case the first call does expensive setup, such as take a global lock
      once();

      barrier.wait(); // A - wait for thread start

      barrier.wait(); // B - init the work

      for (auto i = 0u; i < iters; ++i) {
        once();
      }

      barrier.wait(); // C - join the work
    });
  }

  barrier.wait(); // A - wait for thread start

  // we want to exclude thread start/stop operations from measurement
  braces.dismissing([&] {
    barrier.wait(); // B - init the work
    barrier.wait(); // C - join the work
  });

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace folly

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
