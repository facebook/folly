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
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/fibers/TimedMutex.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>

#if FOLLY_HAS_COROUTINES
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#endif

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

void threadLockBenchmark(int iters, size_t numThreads) {
  TimedMutex mutex;

  std::vector<std::thread> threads{numThreads};
  for (auto& t : threads) {
    t = std::thread([&] {
      for (int i = 0; i < iters; ++i) {
        mutex.lock();
        mutex.unlock();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

void fiberLockBenchmark(int iters, size_t numFibers) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);
  TimedMutex mutex;

  for (size_t i = 0; i < numFibers; ++i) {
    fm.addTask([&] {
      for (int j = 0; j < iters; ++j) {
        mutex.lock();
        mutex.unlock();
      }
    });
  }

  evb.loop();
}

#if FOLLY_HAS_COROUTINES
void coroLockBenchmark(int iters, size_t numThreads) {
  TimedMutex mutex;

  std::vector<std::thread> threads{numThreads};
  for (auto& t : threads) {
    t = std::thread([&] {
      folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
        for (int i = 0; i < iters; ++i) {
          co_await mutex.co_lock();
          mutex.unlock();
        }
      }());
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

folly::coro::Task<void> coroStTask(TimedMutex& mutex, unsigned iters) {
  for (unsigned j = 0; j < iters; ++j) {
    co_await mutex.co_lock();
    mutex.unlock();
  }
}

void TimedMutex_coro_st(unsigned iters, unsigned numCoros) {
  folly::ScopedEventBaseThread evbThread;
  auto* evb = evbThread.getEventBase();
  TimedMutex mutex;

  std::vector<folly::SemiFuture<folly::Unit>> futures;
  futures.reserve(numCoros);
  for (unsigned i = 0; i < numCoros; ++i) {
    futures.push_back(
        folly::coro::co_withExecutor(evb, coroStTask(mutex, iters)).start());
  }

  for (auto& f : futures) {
    std::move(f).get();
  }
}
#endif

} // namespace

BENCHMARK_DRAW_LINE();

BENCHMARK(TimedMutex_thread_1, iters) {
  threadLockBenchmark(iters, 1);
}

BENCHMARK(TimedMutex_thread_2, iters) {
  threadLockBenchmark(iters, 2);
}

BENCHMARK(TimedMutex_thread_4, iters) {
  threadLockBenchmark(iters, 4);
}

BENCHMARK(TimedMutex_thread_8, iters) {
  threadLockBenchmark(iters, 8);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(TimedMutex_fiber_1, iters) {
  fiberLockBenchmark(iters, 1);
}

BENCHMARK(TimedMutex_fiber_2, iters) {
  fiberLockBenchmark(iters, 2);
}

BENCHMARK(TimedMutex_fiber_4, iters) {
  fiberLockBenchmark(iters, 4);
}

BENCHMARK(TimedMutex_fiber_8, iters) {
  fiberLockBenchmark(iters, 8);
}

#if FOLLY_HAS_COROUTINES
BENCHMARK_DRAW_LINE();

BENCHMARK(TimedMutex_coro_1, iters) {
  coroLockBenchmark(iters, 1);
}

BENCHMARK(TimedMutex_coro_2, iters) {
  coroLockBenchmark(iters, 2);
}

BENCHMARK(TimedMutex_coro_4, iters) {
  coroLockBenchmark(iters, 4);
}

BENCHMARK(TimedMutex_coro_8, iters) {
  coroLockBenchmark(iters, 8);
}

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(TimedMutex_coro_st, 1)
BENCHMARK_PARAM(TimedMutex_coro_st, 2)
BENCHMARK_PARAM(TimedMutex_coro_st, 4)
BENCHMARK_PARAM(TimedMutex_coro_st, 8)
#endif

BENCHMARK_DRAW_LINE();

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
