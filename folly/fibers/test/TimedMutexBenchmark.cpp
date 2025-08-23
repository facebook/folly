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
#include <folly/fibers/TimedMutex.h>
#include <folly/init/Init.h>

using namespace folly::fibers;

namespace {

template <class Mutex>
void concurrentReadersBenchmark(int iters, size_t numThreads) {
  Mutex mutex;

  std::vector<std::thread> threads{numThreads};
  for (auto& t : threads) {
    t = std::thread([&] {
      for (int i = 0; i < iters; ++i) {
        std::shared_lock lock(mutex);
        folly::doNotOptimizeAway(lock.owns_lock());
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

} // namespace

BENCHMARK(TimedRWMutexWritePriority_readers_1, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 1);
}

BENCHMARK(TimedRWMutexWritePriority_readers_2, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 2);
}

BENCHMARK(TimedRWMutexWritePriority_readers_4, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 4);
}

BENCHMARK(TimedRWMutexWritePriority_readers_8, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 8);
}

BENCHMARK(TimedRWMutexWritePriority_readers_16, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 16);
}

BENCHMARK(TimedRWMutexWritePriority_readers_32, iters) {
  concurrentReadersBenchmark<TimedRWMutexWritePriority<Baton>>(iters, 32);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);

  folly::runBenchmarks();
  return 0;
}

#if 0
// On Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz
$ buck2 run @mode/opt folly/fibers/test:timed_mutex_benchmark  -- --bm_min_usec 500000
============================================================================
[...]y/fibers/test/TimedMutexBenchmark.cpp     relative  time/iter   iters/s
============================================================================
TimedRWMutexWritePriority_readers_1                        24.93ns    40.11M
TimedRWMutexWritePriority_readers_2                       234.01ns     4.27M
TimedRWMutexWritePriority_readers_4                       960.16ns     1.04M
TimedRWMutexWritePriority_readers_8                         2.64us   379.30K
TimedRWMutexWritePriority_readers_16                       14.03us    71.27K
TimedRWMutexWritePriority_readers_32                       41.93us    23.85K
#endif
