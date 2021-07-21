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

#include <folly/portability/Config.h>

// AtomicSharedPtr-detail.h only works with libstdc++, so skip these tests for
// other vendors
#ifdef FOLLY_USE_LIBSTDCPP

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/concurrency/AtomicSharedPtr.h>
#include <folly/concurrency/CoreCachedSharedPtr.h>
#include <folly/experimental/ReadMostlySharedPtr.h>
#include <folly/portability/GTest.h>

TEST(CoreCachedSharedPtr, Basic) {
  auto p = std::make_shared<int>(1);
  std::weak_ptr<int> wp(p);

  folly::CoreCachedSharedPtr<int> cached(p);
  folly::CoreCachedWeakPtr<int> wcached(cached);

  std::shared_ptr<int> p2 = cached.get();
  std::weak_ptr<int> wp2 = wcached.get();
  ASSERT_TRUE(p2 != nullptr);
  ASSERT_EQ(*p2, 1);
  ASSERT_FALSE(wp2.expired());

  p.reset();
  cached.reset();
  // p2 should survive.
  ASSERT_FALSE(wp.expired());
  // Here we don't know anything about wp2: could be expired even if
  // there is a living reference to the main object.

  p2.reset();
  ASSERT_TRUE(wp.expired());
  ASSERT_TRUE(wp2.expired());
}

namespace {

template <class Operation>
size_t parallelRun(Operation op, size_t numThreads) {
  constexpr size_t kIters = 1000 * 1000;

  std::vector<std::thread> threads;

  // Prevent the compiler from hoisting code out of the loop.
  auto opNoinline = [&]() FOLLY_NOINLINE { op(); };

  std::atomic<size_t> ready = 0;
  std::atomic<bool> done = false;
  std::atomic<size_t> iters = 0;

  for (size_t t = 0; t < numThreads; ++t) {
    threads.emplace_back([&] {
      ++ready;
      while (ready.load() < numThreads) {
      }
      size_t i = 0;
      // Interrupt all workers as soon as the first one is done, so the overall
      // wall time is close to the time spent in each thread.
      for (; !done.load(std::memory_order_acquire) && i < kIters; ++i) {
        opNoinline();
      }
      done = true;
      iters += i;
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  return iters / numThreads;
}

size_t benchmarkSharedPtrAcquire(size_t numThreads) {
  auto p = std::make_shared<int>(1);
  return parallelRun([&] { return p; }, numThreads);
}

size_t benchmarkWeakPtrLock(size_t numThreads) {
  auto p = std::make_shared<int>(1);
  std::weak_ptr<int> wp = p;
  return parallelRun([&] { return wp.lock(); }, numThreads);
}

size_t benchmarkAtomicSharedPtrAcquire(size_t numThreads) {
  auto s = std::make_shared<int>(1);
  folly::atomic_shared_ptr<int> p;
  p.store(s);
  return parallelRun([&] { return p.load(); }, numThreads);
}

size_t benchmarkCoreCachedSharedPtrAcquire(size_t numThreads) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return parallelRun([&] { return p.get(); }, numThreads);
}

size_t benchmarkCoreCachedWeakPtrLock(size_t numThreads) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  folly::CoreCachedWeakPtr<int> wp(p);
  return parallelRun([&] { return wp.lock(); }, numThreads);
}

size_t benchmarkAtomicCoreCachedSharedPtrAcquire(size_t numThreads) {
  folly::AtomicCoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return parallelRun([&] { return p.get(); }, numThreads);
}

size_t benchmarkReadMostlySharedPtrAcquire(size_t numThreads) {
  folly::ReadMostlyMainPtr<int> p{std::make_shared<int>(1)};
  return parallelRun([&] { return p.getShared(); }, numThreads);
}

size_t benchmarkReadMostlyWeakPtrLock(size_t numThreads) {
  folly::ReadMostlyMainPtr<int> p{std::make_shared<int>(1)};
  folly::ReadMostlyWeakPtr<int> w{p};
  return parallelRun([&] { return w.lock(); }, numThreads);
}

} // namespace

#define BENCHMARK_THREADS(THREADS)                                       \
  BENCHMARK_MULTI(SharedPtrAcquire_##THREADS##Threads) {                 \
    return benchmarkSharedPtrAcquire(THREADS);                           \
  }                                                                      \
  BENCHMARK_MULTI(WeakPtrLock_##THREADS##Threads) {                      \
    return benchmarkWeakPtrLock(THREADS);                                \
  }                                                                      \
  BENCHMARK_MULTI(AtomicSharedPtrAcquire_##THREADS##Threads) {           \
    return benchmarkAtomicSharedPtrAcquire(THREADS);                     \
  }                                                                      \
  BENCHMARK_MULTI(CoreCachedSharedPtrAquire_##THREADS##Threads) {        \
    return benchmarkCoreCachedSharedPtrAcquire(THREADS);                 \
  }                                                                      \
  BENCHMARK_MULTI(CoreCachedWeakPtrLock_##THREADS##Threads) {            \
    return benchmarkCoreCachedWeakPtrLock(THREADS);                      \
  }                                                                      \
  BENCHMARK_MULTI(AtomicCoreCachedSharedPtrAcquire_##THREADS##Threads) { \
    return benchmarkAtomicCoreCachedSharedPtrAcquire(THREADS);           \
  }                                                                      \
  BENCHMARK_MULTI(ReadMostlySharedPtrAcquire_##THREADS##Threads) {       \
    return benchmarkReadMostlySharedPtrAcquire(THREADS);                 \
  }                                                                      \
  BENCHMARK_MULTI(ReadMostlyWeakPtrLock_##THREADS##Threads) {            \
    return benchmarkReadMostlyWeakPtrLock(THREADS);                      \
  }                                                                      \
  BENCHMARK_DRAW_LINE();

BENCHMARK_THREADS(1)
BENCHMARK_THREADS(4)
BENCHMARK_THREADS(8)
BENCHMARK_THREADS(16)
BENCHMARK_THREADS(32)
BENCHMARK_THREADS(64)

BENCHMARK_MULTI(SharedPtrSingleThreadReset) {
  auto p = std::make_shared<int>(1);
  return parallelRun([&] { p = std::make_shared<int>(1); }, 1);
}
BENCHMARK_MULTI(AtomicSharedPtrSingleThreadReset) {
  auto s = std::make_shared<int>(1);
  folly::atomic_shared_ptr<int> p;
  p.store(s);
  return parallelRun([&] { p.store(std::make_shared<int>(1)); }, 1);
}
BENCHMARK_MULTI(CoreCachedSharedPtrSingleThreadReset) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return parallelRun([&] { p.reset(std::make_shared<int>(1)); }, 1);
}
BENCHMARK_MULTI(AtomicCoreCachedSharedPtrSingleThreadReset) {
  folly::AtomicCoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return parallelRun([&] { p.reset(std::make_shared<int>(1)); }, 1);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto ret = RUN_ALL_TESTS();
  if (ret == 0 && FLAGS_benchmark) {
    folly::runBenchmarks();
  }

  return ret;
}

#endif // #ifdef FOLLY_USE_LIBSTDCPP

#if 0
// On Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz, 2 sockets, 28 cores, 56 threads.
$ buck-out/gen/folly/concurrency/test/core_cached_shared_ptr_test --benchmark --bm_min_usec 500000

============================================================================
folly/concurrency/test/CoreCachedSharedPtrTest.cpprelative  time/iter  iters/s
============================================================================
SharedPtrAcquire_1Threads                                   19.89ns   50.28M
WeakPtrLock_1Threads                                        22.21ns   45.02M
AtomicSharedPtrAcquire_1Threads                             27.50ns   36.37M
CoreCachedSharedPtrAquire_1Threads                          19.36ns   51.65M
CoreCachedWeakPtrLock_1Threads                              22.07ns   45.31M
AtomicCoreCachedSharedPtrAcquire_1Threads                   22.68ns   44.09M
ReadMostlySharedPtrAcquire_1Threads                         20.27ns   49.34M
ReadMostlyWeakPtrLock_1Threads                              20.23ns   49.43M
----------------------------------------------------------------------------
SharedPtrAcquire_4Threads                                  187.84ns    5.32M
WeakPtrLock_4Threads                                       207.78ns    4.81M
AtomicSharedPtrAcquire_4Threads                            552.59ns    1.81M
CoreCachedSharedPtrAquire_4Threads                          20.77ns   48.14M
CoreCachedWeakPtrLock_4Threads                              23.17ns   43.15M
AtomicCoreCachedSharedPtrAcquire_4Threads                   23.85ns   41.94M
ReadMostlySharedPtrAcquire_4Threads                         22.20ns   45.05M
ReadMostlyWeakPtrLock_4Threads                              21.79ns   45.89M
----------------------------------------------------------------------------
SharedPtrAcquire_8Threads                                  341.50ns    2.93M
WeakPtrLock_8Threads                                       463.56ns    2.16M
AtomicSharedPtrAcquire_8Threads                              1.29us  776.90K
CoreCachedSharedPtrAquire_8Threads                          20.92ns   47.81M
CoreCachedWeakPtrLock_8Threads                              23.31ns   42.91M
AtomicCoreCachedSharedPtrAcquire_8Threads                   23.84ns   41.94M
ReadMostlySharedPtrAcquire_8Threads                         21.57ns   46.36M
ReadMostlyWeakPtrLock_8Threads                              21.62ns   46.26M
----------------------------------------------------------------------------
SharedPtrAcquire_16Threads                                 855.57ns    1.17M
WeakPtrLock_16Threads                                      898.42ns    1.11M
AtomicSharedPtrAcquire_16Threads                             3.28us  304.83K
CoreCachedSharedPtrAquire_16Threads                         22.55ns   44.35M
CoreCachedWeakPtrLock_16Threads                             24.73ns   40.44M
AtomicCoreCachedSharedPtrAcquire_16Threads                  26.24ns   38.10M
ReadMostlySharedPtrAcquire_16Threads                        24.44ns   40.92M
ReadMostlyWeakPtrLock_16Threads                             24.60ns   40.65M
----------------------------------------------------------------------------
SharedPtrAcquire_32Threads                                   1.39us  717.39K
WeakPtrLock_32Threads                                        2.02us  494.63K
AtomicSharedPtrAcquire_32Threads                             4.97us  201.20K
CoreCachedSharedPtrAquire_32Threads                         30.78ns   32.49M
CoreCachedWeakPtrLock_32Threads                             27.89ns   35.85M
AtomicCoreCachedSharedPtrAcquire_32Threads                  30.56ns   32.72M
ReadMostlySharedPtrAcquire_32Threads                        29.36ns   34.06M
ReadMostlyWeakPtrLock_32Threads                             29.63ns   33.75M
----------------------------------------------------------------------------
SharedPtrAcquire_64Threads                                   2.49us  402.30K
WeakPtrLock_64Threads                                        4.57us  218.74K
AtomicSharedPtrAcquire_64Threads                             9.78us  102.28K
CoreCachedSharedPtrAquire_64Threads                         48.75ns   20.51M
CoreCachedWeakPtrLock_64Threads                             52.85ns   18.92M
AtomicCoreCachedSharedPtrAcquire_64Threads                  56.58ns   17.67M
ReadMostlySharedPtrAcquire_64Threads                        56.58ns   17.68M
ReadMostlyWeakPtrLock_64Threads                             56.87ns   17.59M
----------------------------------------------------------------------------
SharedPtrSingleThreadReset                                  10.50ns   95.28M
AtomicSharedPtrSingleThreadReset                            44.02ns   22.72M
CoreCachedSharedPtrSingleThreadReset                         4.57us  218.87K
AtomicCoreCachedSharedPtrSingleThreadReset                   5.22us  191.47K
============================================================================
#endif
