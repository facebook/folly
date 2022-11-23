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
#include <folly/Optional.h>
#include <folly/concurrency/AtomicSharedPtr.h>
#include <folly/concurrency/CoreCachedSharedPtr.h>
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
    folly::Optional<Holder> h(folly::in_place);
    auto p1 = std::make_shared<int>(1);
    std::weak_ptr<int> w1 = p1;
    // Aliasing constructor, p2 is nullptr but still manages the object in p1.
    std::shared_ptr<int> p2(p1, nullptr);
    // And now it's the only reference left.
    p1.reset();
    EXPECT_FALSE(w1.expired());

    // Pass the ownership to the Holder.
    h->reset(p2);
    p2.reset();

    // Object should still be alive.
    EXPECT_FALSE(w1.expired());

    // And destroying the holder will retire it.
    h.reset();
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
// On Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz, 2 sockets, 28 cores, 56 threads.
$ buck-out/gen/folly/concurrency/test/core_cached_shared_ptr_test --benchmark --bm_min_usec 500000

============================================================================
folly/concurrency/test/CoreCachedSharedPtrTest.cpprelative  time/iter  iters/s
============================================================================
SharedPtrAcquire_1Threads                                   18.31ns   54.62M
WeakPtrLock_1Threads                                        20.07ns   49.83M
AtomicSharedPtrAcquire_1Threads                             26.31ns   38.01M
CoreCachedSharedPtrAquire_1Threads                          20.17ns   49.57M
CoreCachedWeakPtrLock_1Threads                              22.41ns   44.62M
AtomicCoreCachedSharedPtrAcquire_1Threads                   23.59ns   42.40M
ReadMostlySharedPtrAcquire_1Threads                         20.16ns   49.61M
ReadMostlyWeakPtrLock_1Threads                              20.46ns   48.87M
----------------------------------------------------------------------------
SharedPtrAcquire_4Threads                                  193.62ns    5.16M
WeakPtrLock_4Threads                                       216.29ns    4.62M
AtomicSharedPtrAcquire_4Threads                            508.04ns    1.97M
CoreCachedSharedPtrAquire_4Threads                          21.52ns   46.46M
CoreCachedWeakPtrLock_4Threads                              23.80ns   42.01M
AtomicCoreCachedSharedPtrAcquire_4Threads                   24.81ns   40.30M
ReadMostlySharedPtrAcquire_4Threads                         21.67ns   46.15M
ReadMostlyWeakPtrLock_4Threads                              21.72ns   46.04M
----------------------------------------------------------------------------
SharedPtrAcquire_8Threads                                  389.71ns    2.57M
WeakPtrLock_8Threads                                       467.29ns    2.14M
AtomicSharedPtrAcquire_8Threads                              1.38us  727.03K
CoreCachedSharedPtrAquire_8Threads                          21.49ns   46.53M
CoreCachedWeakPtrLock_8Threads                              23.83ns   41.97M
AtomicCoreCachedSharedPtrAcquire_8Threads                   24.68ns   40.52M
ReadMostlySharedPtrAcquire_8Threads                         21.68ns   46.12M
ReadMostlyWeakPtrLock_8Threads                              21.48ns   46.55M
----------------------------------------------------------------------------
SharedPtrAcquire_16Threads                                 739.59ns    1.35M
WeakPtrLock_16Threads                                      896.23ns    1.12M
AtomicSharedPtrAcquire_16Threads                             2.88us  347.73K
CoreCachedSharedPtrAquire_16Threads                         21.98ns   45.50M
CoreCachedWeakPtrLock_16Threads                             25.98ns   38.49M
AtomicCoreCachedSharedPtrAcquire_16Threads                  26.44ns   37.82M
ReadMostlySharedPtrAcquire_16Threads                        23.75ns   42.11M
ReadMostlyWeakPtrLock_16Threads                             22.89ns   43.70M
----------------------------------------------------------------------------
SharedPtrAcquire_32Threads                                   1.36us  732.78K
WeakPtrLock_32Threads                                        1.93us  518.58K
AtomicSharedPtrAcquire_32Threads                             5.68us  176.04K
CoreCachedSharedPtrAquire_32Threads                         29.24ns   34.20M
CoreCachedWeakPtrLock_32Threads                             32.17ns   31.08M
AtomicCoreCachedSharedPtrAcquire_32Threads                  28.67ns   34.88M
ReadMostlySharedPtrAcquire_32Threads                        29.36ns   34.06M
ReadMostlyWeakPtrLock_32Threads                             27.27ns   36.67M
----------------------------------------------------------------------------
SharedPtrAcquire_64Threads                                   2.39us  418.35K
WeakPtrLock_64Threads                                        4.21us  237.61K
AtomicSharedPtrAcquire_64Threads                             8.63us  115.86K
CoreCachedSharedPtrAquire_64Threads                         49.70ns   20.12M
CoreCachedWeakPtrLock_64Threads                             74.74ns   13.38M
AtomicCoreCachedSharedPtrAcquire_64Threads                  56.09ns   17.83M
ReadMostlySharedPtrAcquire_64Threads                        49.22ns   20.32M
ReadMostlyWeakPtrLock_64Threads                             49.16ns   20.34M
----------------------------------------------------------------------------
SharedPtrSingleThreadReset                                  10.45ns   95.70M
AtomicSharedPtrSingleThreadReset                            42.83ns   23.35M
CoreCachedSharedPtrSingleThreadReset                         2.51us  398.43K
AtomicCoreCachedSharedPtrSingleThreadReset                   2.36us  423.31K
============================================================================
#endif
