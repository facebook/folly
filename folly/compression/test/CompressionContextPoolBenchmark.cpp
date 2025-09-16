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

#include <folly/compression/CompressionContextPool.h>
#include <folly/compression/CompressionContextPoolSingletons.h>
#include <folly/compression/CompressionCoreLocalContextPool.h>

#include <memory>
#include <thread>

#include <glog/logging.h>

#include <folly/Benchmark.h>

namespace {

class alignas(folly::hardware_destructive_interference_size) Foo {
 public:
  void use() { used_ = true; }

  void reset() { used_ = false; }

 private:
  bool used_{false};
};

struct FooCreator {
  Foo* operator()() const { return new Foo(); }
};

struct FooDeleter {
  void operator()(Foo* f) const { delete f; }
};

struct FooResetter {
  void operator()(Foo* f) const { f->reset(); }
};

using FooStackPool = folly::compression::
    CompressionContextPool<Foo, FooCreator, FooDeleter, FooResetter>;

template <int NumStripes>
using FooCoreLocalPool = folly::compression::CompressionCoreLocalContextPool<
    Foo,
    FooCreator,
    FooDeleter,
    FooResetter,
    NumStripes>;

template <typename Pool>
size_t multithreadedBench(size_t iters, size_t numThreads) {
  folly::BenchmarkSuspender startup_suspender;

  iters *= 1024;
  const size_t iters_per_thread = iters;

  Pool pool;

  std::atomic<size_t> ready{0};
  std::atomic<size_t> finished{0};
  std::atomic<bool> go{false};
  std::atomic<bool> exit{false};
  std::atomic<size_t> total{0};

  {
    // Pre-init enough objects.
    std::vector<typename Pool::Ref> refs;
    while (refs.size() < numThreads) {
      refs.push_back(pool.get());
    }
  }
  pool.flush_shallow();

  // if we happen to be using the tlsRoundRobin, then sequentially
  // assigning the thread identifiers is the unlikely best-case scenario.
  // We don't want to unfairly benefit or penalize.  Computing the exact
  // maximum likelihood of the probability distributions is annoying, so
  // I approximate as 2/5 of the ids that have no threads, 2/5 that have
  // 1, 2/15 that have 2, and 1/15 that have 3.  We accomplish this by
  // wrapping back to slot 0 when we hit 1/15 and 1/5.

  std::vector<std::thread> threads;
  while (threads.size() < numThreads) {
    threads.push_back(std::thread([&, iters_per_thread]() {
      size_t local_count = 0;

      auto finish_loop_iters = iters_per_thread / 10;
      if (finish_loop_iters < 10) {
        finish_loop_iters = 10;
      }

      ready++;

      while (!go.load()) {
        std::this_thread::yield();
      }

      for (size_t i = iters_per_thread; i > 0; --i) {
        auto ref = pool.get();
        ref->use();
        folly::doNotOptimizeAway(ref);
      }

      local_count += iters_per_thread;

      finished++;

      while (!exit.load()) {
        // Keep accumulating iterations until all threads are finished.
        for (size_t i = finish_loop_iters; i > 0; --i) {
          auto ref = pool.get();
          ref->use();
          folly::doNotOptimizeAway(ref);
        }
        local_count += finish_loop_iters;
      }

      total += local_count;

      // LOG(INFO) << local_count;
    }));

    if (threads.size() == numThreads / 15 || threads.size() == numThreads / 5) {
      // create a few dummy threads to wrap back around to 0 mod numCpus
      for (size_t i = threads.size(); i != numThreads; ++i) {
        std::thread t([&]() {});
        t.join();
      }
    }
  }

  while (ready < numThreads) {
    std::this_thread::yield();
  }
  startup_suspender.dismiss();
  go = true;

  while (finished < numThreads) {
    std::this_thread::yield();
  }

  exit = true;

  folly::BenchmarkSuspender end_suspender;

  for (auto& thr : threads) {
    thr.join();
  }

  // LOG(INFO) << total.load();

  return total.load() / numThreads;
}

#define POOL_BENCHMARK(Name, Pool, NumThreads)      \
  BENCHMARK_MULTI(Name, n) {                        \
    return multithreadedBench<Pool>(n, NumThreads); \
  }

#define REGULAR_POOL_BENCHMARK(NumThreads) \
  POOL_BENCHMARK(StackPool_##NumThreads##_threads, FooStackPool, NumThreads)

#define CORE_LOCAL_POOL_BENCHMARK(NumSlots, NumThreads) \
  POOL_BENCHMARK(                                       \
      CLPool_##NumSlots##_slots_##NumThreads##_threads, \
      FooCoreLocalPool<NumSlots>,                       \
      NumThreads)

REGULAR_POOL_BENCHMARK(1)
REGULAR_POOL_BENCHMARK(2)
REGULAR_POOL_BENCHMARK(4)
REGULAR_POOL_BENCHMARK(8)
REGULAR_POOL_BENCHMARK(16)
REGULAR_POOL_BENCHMARK(32)
REGULAR_POOL_BENCHMARK(64)
REGULAR_POOL_BENCHMARK(128)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 1)
CORE_LOCAL_POOL_BENCHMARK(2, 1)
CORE_LOCAL_POOL_BENCHMARK(4, 1)
CORE_LOCAL_POOL_BENCHMARK(8, 1)
CORE_LOCAL_POOL_BENCHMARK(16, 1)
CORE_LOCAL_POOL_BENCHMARK(32, 1)
CORE_LOCAL_POOL_BENCHMARK(64, 1)
CORE_LOCAL_POOL_BENCHMARK(128, 1)
CORE_LOCAL_POOL_BENCHMARK(256, 1)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 2)
CORE_LOCAL_POOL_BENCHMARK(2, 2)
CORE_LOCAL_POOL_BENCHMARK(4, 2)
CORE_LOCAL_POOL_BENCHMARK(8, 2)
CORE_LOCAL_POOL_BENCHMARK(16, 2)
CORE_LOCAL_POOL_BENCHMARK(32, 2)
CORE_LOCAL_POOL_BENCHMARK(64, 2)
CORE_LOCAL_POOL_BENCHMARK(128, 2)
CORE_LOCAL_POOL_BENCHMARK(256, 2)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 4)
CORE_LOCAL_POOL_BENCHMARK(2, 4)
CORE_LOCAL_POOL_BENCHMARK(4, 4)
CORE_LOCAL_POOL_BENCHMARK(8, 4)
CORE_LOCAL_POOL_BENCHMARK(16, 4)
CORE_LOCAL_POOL_BENCHMARK(32, 4)
CORE_LOCAL_POOL_BENCHMARK(64, 4)
CORE_LOCAL_POOL_BENCHMARK(128, 4)
CORE_LOCAL_POOL_BENCHMARK(256, 4)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 8)
CORE_LOCAL_POOL_BENCHMARK(2, 8)
CORE_LOCAL_POOL_BENCHMARK(4, 8)
CORE_LOCAL_POOL_BENCHMARK(8, 8)
CORE_LOCAL_POOL_BENCHMARK(16, 8)
CORE_LOCAL_POOL_BENCHMARK(32, 8)
CORE_LOCAL_POOL_BENCHMARK(64, 8)
CORE_LOCAL_POOL_BENCHMARK(128, 8)
CORE_LOCAL_POOL_BENCHMARK(256, 8)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 16)
CORE_LOCAL_POOL_BENCHMARK(2, 16)
CORE_LOCAL_POOL_BENCHMARK(4, 16)
CORE_LOCAL_POOL_BENCHMARK(8, 16)
CORE_LOCAL_POOL_BENCHMARK(16, 16)
CORE_LOCAL_POOL_BENCHMARK(32, 16)
CORE_LOCAL_POOL_BENCHMARK(64, 16)
CORE_LOCAL_POOL_BENCHMARK(128, 16)
CORE_LOCAL_POOL_BENCHMARK(256, 16)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 32)
CORE_LOCAL_POOL_BENCHMARK(2, 32)
CORE_LOCAL_POOL_BENCHMARK(4, 32)
CORE_LOCAL_POOL_BENCHMARK(8, 32)
CORE_LOCAL_POOL_BENCHMARK(16, 32)
CORE_LOCAL_POOL_BENCHMARK(32, 32)
CORE_LOCAL_POOL_BENCHMARK(64, 32)
CORE_LOCAL_POOL_BENCHMARK(128, 32)
CORE_LOCAL_POOL_BENCHMARK(256, 32)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 64)
CORE_LOCAL_POOL_BENCHMARK(2, 64)
CORE_LOCAL_POOL_BENCHMARK(4, 64)
CORE_LOCAL_POOL_BENCHMARK(8, 64)
CORE_LOCAL_POOL_BENCHMARK(16, 64)
CORE_LOCAL_POOL_BENCHMARK(32, 64)
CORE_LOCAL_POOL_BENCHMARK(64, 64)
CORE_LOCAL_POOL_BENCHMARK(128, 64)
CORE_LOCAL_POOL_BENCHMARK(256, 64)

BENCHMARK_DRAW_LINE();

CORE_LOCAL_POOL_BENCHMARK(1, 128)
CORE_LOCAL_POOL_BENCHMARK(2, 128)
CORE_LOCAL_POOL_BENCHMARK(4, 128)
CORE_LOCAL_POOL_BENCHMARK(8, 128)
CORE_LOCAL_POOL_BENCHMARK(16, 128)
CORE_LOCAL_POOL_BENCHMARK(32, 128)
CORE_LOCAL_POOL_BENCHMARK(64, 128)
CORE_LOCAL_POOL_BENCHMARK(128, 128)
CORE_LOCAL_POOL_BENCHMARK(256, 128)

} // anonymous namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
