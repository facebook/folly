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
#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/SharedMutex.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/init/Init.h>

using namespace folly;

void runBenchmark(double writePercent, size_t numThreads, size_t numTasks) {
  auto suspender = folly::BenchmarkSuspender();
  folly::CPUThreadPoolExecutor executor(numThreads);
  coro::SharedMutex mutex;
  int valueProtected{42};
  std::atomic<bool> go(false);
  auto task = [&]() -> coro::Task<void> {
    // decide if this will be a writer or reader before start benchmarking
    bool isWrite = (folly::Random::randDouble01() < writePercent);
    while (!go.load()) {
      std::this_thread::yield();
    }
    if (isWrite) {
      auto wLock = co_await mutex.co_scoped_lock();
      auto copy = valueProtected;
      folly::doNotOptimizeAway(copy);
    } else {
      auto rLock = co_await mutex.co_scoped_lock_shared();
      auto copy = valueProtected;
      folly::doNotOptimizeAway(copy);
    }
  };
  coro::AsyncScope scope;
  for (size_t i = 0; i < numTasks; ++i) {
    scope.add(coro::co_withExecutor(&executor, task()));
  }
  suspender.dismiss();

  // start benchmarking
  go.store(true);
  // block the current thread until all tasks are complete
  scope.cleanup().wait();
}

BENCHMARK(SharedMutex_all_read_1thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.0, 1, 10000);
  }
}
BENCHMARK(SharedMutex_all_read_4thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.0, 4, 10000);
  }
}
BENCHMARK(SharedMutex_all_read_8thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.0, 8, 10000);
  }
}
BENCHMARK(SharedMutex_all_read_16thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.0, 16, 10000);
  }
}
BENCHMARK(SharedMutex_all_read_32thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.0, 32, 10000);
  }
}
BENCHMARK_DRAW_LINE();
BENCHMARK(SharedMutex_1pct_write_1thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.01, 1, 10000);
  }
}
BENCHMARK(SharedMutex_1pct_write_4thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.01, 4, 10000);
  }
}
BENCHMARK(SharedMutex_1pct_write_8thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.01, 8, 10000);
  }
}
BENCHMARK(SharedMutex_1pct_write_16thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.01, 16, 10000);
  }
}
BENCHMARK(SharedMutex_1pct_write_32thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.01, 32, 10000);
  }
}
BENCHMARK_DRAW_LINE();
BENCHMARK(SharedMutex_10pct_write_1thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.1, 1, 10000);
  }
}
BENCHMARK(SharedMutex_10pct_write_4thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.1, 4, 10000);
  }
}
BENCHMARK(SharedMutex_10pct_write_8thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.1, 8, 10000);
  }
}
BENCHMARK(SharedMutex_10pct_write_16thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.1, 16, 10000);
  }
}
BENCHMARK(SharedMutex_10pct_write_32thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(0.1, 32, 10000);
  }
}
BENCHMARK_DRAW_LINE();
BENCHMARK(SharedMutex_all_write_1thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(1.0, 1, 10000);
  }
}
BENCHMARK(SharedMutex_all_write_4thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(1.0, 4, 10000);
  }
}
BENCHMARK(SharedMutex_all_write_8thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(1.0, 8, 10000);
  }
}
BENCHMARK(SharedMutex_all_write_16thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(1.0, 16, 10000);
  }
}
BENCHMARK(SharedMutex_all_write_32thread_10000task, iters) {
  for (size_t i = 0; i < iters; ++i) {
    runBenchmark(1.0, 32, 10000);
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);

  folly::runBenchmarks();
  return 0;
}

#if 0

On Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz
$ buck run @mode/opt folly/coro/test:shared_mutex_benchmark -- --bm_min_usec 50000
============================================================================
[...]ly/coro/test/SharedMutexBenchmark.cpp     relative  time/iter   iters/s
============================================================================
SharedMutex_all_read_1thread_10000task                      3.56ms    280.74
SharedMutex_all_read_4thread_10000task                      5.16ms    193.83
SharedMutex_all_read_8thread_10000task                      5.59ms    179.02
SharedMutex_all_read_16thread_10000task                     8.58ms    116.49
SharedMutex_all_read_32thread_10000task                    11.45ms     87.33
----------------------------------------------------------------------------
SharedMutex_1pct_write_1thread_10000task                    3.59ms    278.48
SharedMutex_1pct_write_4thread_10000task                   10.97ms     91.12
SharedMutex_1pct_write_8thread_10000task                   13.18ms     75.88
SharedMutex_1pct_write_16thread_10000task                  14.85ms     67.34
SharedMutex_1pct_write_32thread_10000task                  17.24ms     58.00
----------------------------------------------------------------------------
SharedMutex_10pct_write_1thread_10000task                   3.70ms    269.94
SharedMutex_10pct_write_4thread_10000task                  15.34ms     65.17
SharedMutex_10pct_write_8thread_10000task                  16.81ms     59.50
SharedMutex_10pct_write_16thread_10000task                 19.14ms     52.25
SharedMutex_10pct_write_32thread_10000task                 22.95ms     43.57
----------------------------------------------------------------------------
SharedMutex_all_write_1thread_10000task                     3.70ms    270.50
SharedMutex_all_write_4thread_10000task                    20.68ms     48.36
SharedMutex_all_write_8thread_10000task                    22.51ms     44.43
SharedMutex_all_write_16thread_10000task                   25.31ms     39.51
SharedMutex_all_write_32thread_10000task                   27.40ms     36.50

#endif
