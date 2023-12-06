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

// AtomicSharedPtr-detail.h only works with libstdc++, so skip these tests for
// other vendors
// PackedSyncPtr requires x64, ppc64 or aarch64, skip these tests for
// other arches
#include <folly/Portability.h>
#include <folly/portability/Config.h>
#if defined(__GLIBCXX__) && (FOLLY_X64 || FOLLY_PPC64 || FOLLY_AARCH64)

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/concurrency/AtomicSharedPtr.h>
#include <folly/concurrency/CoreCachedSharedPtr.h>
#include <folly/concurrency/ThreadCachedSynchronized.h>
#include <folly/experimental/ReadMostlySharedPtr.h>
#include <folly/portability/GTest.h>

namespace {

template <class Operation>
void parallelRun(
    Operation op, size_t numThreads = std::thread::hardware_concurrency()) {
  std::vector<std::thread> threads;
  for (size_t t = 0; t < numThreads; ++t) {
    threads.emplace_back([&, t] { op(t); });
  }
  for (auto& t : threads) {
    t.join();
  }
}

} // namespace

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

  // Check that other cores get the correct shared_ptr too.
  parallelRun([&](size_t) {
    EXPECT_TRUE(cached.get().get() == p.get());
    EXPECT_EQ(*cached.get(), 1);
    EXPECT_EQ(*wcached.lock(), 1);
  });

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

TEST(CoreCachedSharedPtr, AtomicCoreCachedSharedPtr) {
  constexpr size_t kIters = 2000;
  {
    folly::AtomicCoreCachedSharedPtr<size_t> p;
    parallelRun([&](size_t) {
      for (size_t i = 0; i < kIters; ++i) {
        p.reset(std::make_shared<size_t>(i));
        EXPECT_TRUE(p.get());
        // Just read the value, and ensure that ASAN/TSAN do not complain.
        EXPECT_GE(*p.get(), 0);
      }
    });
  }
  {
    // One writer thread, all other readers, verify consistency.
    std::atomic<size_t> largestValueObserved{0};
    folly::AtomicCoreCachedSharedPtr<size_t> p{std::make_shared<size_t>(0)};
    parallelRun([&](size_t t) {
      if (t == 0) {
        for (size_t i = 0; i < kIters; ++i) {
          p.reset(std::make_shared<size_t>(i + 1));
        }
      } else {
        while (true) {
          auto exp = largestValueObserved.load();
          auto value = *p.get();
          EXPECT_GE(value, exp);
          // Maintain the maximum value observed so far. As soon as one thread
          // observes an update, they all should observe it.
          while (value > exp &&
                 !largestValueObserved.compare_exchange_weak(exp, value)) {
          }
          if (exp == kIters) {
            break;
          }
        }
      }
    });
  }
}

namespace {

template <class Holder>
void testAliasingCornerCases() {
  {
    Holder h;
    auto p1 = std::make_shared<int>(1);
    std::weak_ptr<int> w1 = p1;
    // Aliasing constructor, p2 is nullptr but still manages the object in p1.
    std::shared_ptr<int> p2(p1, nullptr);
    // And now it's the only reference left.
    p1.reset();
    EXPECT_FALSE(w1.expired());

    // Pass the ownership to the Holder.
    h.reset(p2);
    p2.reset();

    // Object should still be alive.
    EXPECT_FALSE(w1.expired());

    // And resetting the holder will destroy the managed object.
    h.reset();
    // Except in the case of AtomicCoreCachedSharedPtr, where the object is only
    // scheduled for reclamation, so we have to wait for a hazptr scan.
    folly::hazptr_cleanup();
    EXPECT_TRUE(w1.expired());
  }

  {
    Holder h;
    int x = 1;
    // p points to x, but has no managed object.
    std::shared_ptr<int> p(std::shared_ptr<int>{}, &x);
    h.reset(p);
    EXPECT_TRUE(h.get().get() == &x);
  }
}

} // namespace

TEST(CoreCachedSharedPtr, AliasingCornerCases) {
  testAliasingCornerCases<folly::CoreCachedSharedPtr<int>>();
}

TEST(CoreCachedSharedPtr, AliasingCornerCasesAtomic) {
  testAliasingCornerCases<folly::AtomicCoreCachedSharedPtr<int>>();
}

namespace {

template <class Operation>
size_t benchmarkParallelRun(Operation op, size_t numThreads) {
  constexpr size_t kIters = 2 * 1000 * 1000;

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
  return benchmarkParallelRun([&] { return p; }, numThreads);
}

size_t benchmarkWeakPtrLock(size_t numThreads) {
  auto p = std::make_shared<int>(1);
  std::weak_ptr<int> wp = p;
  return benchmarkParallelRun([&] { return wp.lock(); }, numThreads);
}

size_t benchmarkAtomicSharedPtrAcquire(size_t numThreads) {
  auto s = std::make_shared<int>(1);
  folly::atomic_shared_ptr<int> p;
  p.store(s);
  return benchmarkParallelRun([&] { return p.load(); }, numThreads);
}

size_t benchmarkCoreCachedSharedPtrAcquire(size_t numThreads) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return benchmarkParallelRun([&] { return p.get(); }, numThreads);
}

size_t benchmarkCoreCachedWeakPtrLock(size_t numThreads) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  folly::CoreCachedWeakPtr<int> wp(p);
  return benchmarkParallelRun([&] { return wp.lock(); }, numThreads);
}

size_t benchmarkAtomicCoreCachedSharedPtrAcquire(size_t numThreads) {
  folly::AtomicCoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return benchmarkParallelRun([&] { return p.get(); }, numThreads);
}

size_t benchmarkReadMostlySharedPtrAcquire(size_t numThreads) {
  folly::ReadMostlyMainPtr<int> p{std::make_shared<int>(1)};
  return benchmarkParallelRun([&] { return p.getShared(); }, numThreads);
}

size_t benchmarkReadMostlyWeakPtrLock(size_t numThreads) {
  folly::ReadMostlyMainPtr<int> p{std::make_shared<int>(1)};
  folly::ReadMostlyWeakPtr<int> w{p};
  return benchmarkParallelRun([&] { return w.lock(); }, numThreads);
}

size_t benchmarkThreadCachedSynchronizedAcquire(size_t numThreads) {
  folly::thread_cached_synchronized<std::shared_ptr<int>> p{
      std::make_shared<int>(1)};
  return benchmarkParallelRun([&] { return *p; }, numThreads);
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
  BENCHMARK_MULTI(ThreadCachedSynchronizedAcquire_##THREADS##Threads) {  \
    return benchmarkThreadCachedSynchronizedAcquire(THREADS);            \
  }                                                                      \
  BENCHMARK_MULTI(CoreCachedSharedPtrAcquire_##THREADS##Threads) {       \
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
  return benchmarkParallelRun([&] { p = std::make_shared<int>(1); }, 1);
}
BENCHMARK_MULTI(AtomicSharedPtrSingleThreadReset) {
  auto s = std::make_shared<int>(1);
  folly::atomic_shared_ptr<int> p;
  p.store(s);
  return benchmarkParallelRun([&] { p.store(std::make_shared<int>(1)); }, 1);
}
BENCHMARK_MULTI(CoreCachedSharedPtrSingleThreadReset) {
  folly::CoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return benchmarkParallelRun([&] { p.reset(std::make_shared<int>(1)); }, 1);
}
BENCHMARK_MULTI(AtomicCoreCachedSharedPtrSingleThreadReset) {
  folly::AtomicCoreCachedSharedPtr<int> p(std::make_shared<int>(1));
  return benchmarkParallelRun([&] { p.reset(std::make_shared<int>(1)); }, 1);
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

#endif // defined(__GLIBCXX__)

#if 0
// On Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz, 2 sockets, 40 cores, 80 threads.
$ buck-out/gen/folly/concurrency/test/core_cached_shared_ptr_test --benchmark --bm_min_usec 500000

============================================================================
[...]ency/test/CoreCachedSharedPtrTest.cpp     relative  time/iter   iters/s
============================================================================
SharedPtrAcquire_1Threads                                  19.86ns    50.36M
WeakPtrLock_1Threads                                       21.23ns    47.10M
AtomicSharedPtrAcquire_1Threads                            30.92ns    32.34M
ThreadCachedSynchronizedAcquire_1Threads                   21.61ns    46.27M
CoreCachedSharedPtrAcquire_1Threads                        21.87ns    45.72M
CoreCachedWeakPtrLock_1Threads                             22.09ns    45.27M
AtomicCoreCachedSharedPtrAcquire_1Threads                  24.47ns    40.87M
ReadMostlySharedPtrAcquire_1Threads                        15.49ns    64.57M
ReadMostlyWeakPtrLock_1Threads                             15.32ns    65.27M
----------------------------------------------------------------------------
SharedPtrAcquire_4Threads                                 255.09ns     3.92M
WeakPtrLock_4Threads                                      287.98ns     3.47M
AtomicSharedPtrAcquire_4Threads                           709.64ns     1.41M
ThreadCachedSynchronizedAcquire_4Threads                  228.03ns     4.39M
CoreCachedSharedPtrAcquire_4Threads                        22.68ns    44.09M
CoreCachedWeakPtrLock_4Threads                             23.62ns    42.34M
AtomicCoreCachedSharedPtrAcquire_4Threads                  24.35ns    41.07M
ReadMostlySharedPtrAcquire_4Threads                        16.86ns    59.31M
ReadMostlyWeakPtrLock_4Threads                             16.25ns    61.55M
----------------------------------------------------------------------------
SharedPtrAcquire_8Threads                                 488.90ns     2.05M
WeakPtrLock_8Threads                                      577.98ns     1.73M
AtomicSharedPtrAcquire_8Threads                             1.78us   561.16K
ThreadCachedSynchronizedAcquire_8Threads                  483.47ns     2.07M
CoreCachedSharedPtrAcquire_8Threads                        23.45ns    42.64M
CoreCachedWeakPtrLock_8Threads                             23.81ns    41.99M
AtomicCoreCachedSharedPtrAcquire_8Threads                  24.49ns    40.83M
ReadMostlySharedPtrAcquire_8Threads                        17.10ns    58.49M
ReadMostlyWeakPtrLock_8Threads                             16.73ns    59.76M
----------------------------------------------------------------------------
SharedPtrAcquire_16Threads                                  1.08us   929.56K
WeakPtrLock_16Threads                                       1.56us   642.75K
AtomicSharedPtrAcquire_16Threads                            2.94us   340.19K
ThreadCachedSynchronizedAcquire_16Threads                   1.02us   981.43K
CoreCachedSharedPtrAcquire_16Threads                       25.58ns    39.10M
CoreCachedWeakPtrLock_16Threads                            24.79ns    40.34M
AtomicCoreCachedSharedPtrAcquire_16Threads                 25.97ns    38.50M
ReadMostlySharedPtrAcquire_16Threads                       18.11ns    55.20M
ReadMostlyWeakPtrLock_16Threads                            17.91ns    55.85M
----------------------------------------------------------------------------
SharedPtrAcquire_32Threads                                  2.25us   444.85K
WeakPtrLock_32Threads                                       3.29us   304.08K
AtomicSharedPtrAcquire_32Threads                            6.86us   145.76K
ThreadCachedSynchronizedAcquire_32Threads                   2.30us   434.67K
CoreCachedSharedPtrAcquire_32Threads                       25.35ns    39.45M
CoreCachedWeakPtrLock_32Threads                            25.37ns    39.41M
AtomicCoreCachedSharedPtrAcquire_32Threads                 26.69ns    37.46M
ReadMostlySharedPtrAcquire_32Threads                       19.32ns    51.75M
ReadMostlyWeakPtrLock_32Threads                            18.37ns    54.43M
----------------------------------------------------------------------------
SharedPtrAcquire_64Threads                                  4.92us   203.42K
WeakPtrLock_64Threads                                      10.90us    91.77K
AtomicSharedPtrAcquire_64Threads                           14.06us    71.12K
ThreadCachedSynchronizedAcquire_64Threads                   4.64us   215.59K
CoreCachedSharedPtrAcquire_64Threads                       33.87ns    29.53M
CoreCachedWeakPtrLock_64Threads                            48.94ns    20.43M
AtomicCoreCachedSharedPtrAcquire_64Threads                 35.85ns    27.89M
ReadMostlySharedPtrAcquire_64Threads                       32.08ns    31.17M
ReadMostlyWeakPtrLock_64Threads                            29.83ns    33.52M
----------------------------------------------------------------------------
SharedPtrSingleThreadReset                                 12.52ns    79.89M
AtomicSharedPtrSingleThreadReset                           48.73ns    20.52M
CoreCachedSharedPtrSingleThreadReset                        2.94us   340.29K
AtomicCoreCachedSharedPtrSingleThreadReset                  5.15us   194.14K
============================================================================
#endif
