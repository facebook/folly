/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/ThreadCachedInt.h>

#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/Hash.h>
#include <folly/portability/GFlags.h>

using namespace folly;

TEST(ThreadCachedInt, SingleThreadedNotCached) {
  ThreadCachedInt<int64_t> val(0, 0);
  EXPECT_EQ(0, val.readFast());
  ++val;
  EXPECT_EQ(1, val.readFast());
  for (int i = 0; i < 41; ++i) {
    val.increment(1);
  }
  EXPECT_EQ(42, val.readFast());
  --val;
  EXPECT_EQ(41, val.readFast());
}

// Note: This is somewhat fragile to the implementation.  If this causes
// problems, feel free to remove it.
TEST(ThreadCachedInt, SingleThreadedCached) {
  ThreadCachedInt<int64_t> val(0, 10);
  EXPECT_EQ(0, val.readFast());
  ++val;
  EXPECT_EQ(0, val.readFast());
  for (int i = 0; i < 7; ++i) {
    val.increment(1);
  }
  EXPECT_EQ(0, val.readFast());
  EXPECT_EQ(0, val.readFastAndReset());
  EXPECT_EQ(8, val.readFull());
  EXPECT_EQ(8, val.readFullAndReset());
  EXPECT_EQ(0, val.readFull());
  EXPECT_EQ(0, val.readFast());
}

ThreadCachedInt<int32_t> globalInt32(0, 11);
ThreadCachedInt<int64_t> globalInt64(0, 11);
int kNumInserts = 100000;
DEFINE_int32(numThreads, 8, "Number simultaneous threads for benchmarks.");
#define CREATE_INC_FUNC(size)                                       \
  void incFunc ## size () {                                         \
    const int num = kNumInserts / FLAGS_numThreads;                 \
    for (int i = 0; i < num; ++i) {                                 \
      ++globalInt ## size ;                                         \
    }                                                               \
  }
CREATE_INC_FUNC(64);
CREATE_INC_FUNC(32);

// Confirms counts are accurate with competing threads
TEST(ThreadCachedInt, MultiThreadedCached) {
  kNumInserts = 100000;
  CHECK_EQ(0, kNumInserts % FLAGS_numThreads) <<
    "FLAGS_numThreads must evenly divide kNumInserts (" << kNumInserts << ").";
  const int numPerThread = kNumInserts / FLAGS_numThreads;
  ThreadCachedInt<int64_t> TCInt64(0, numPerThread - 2);
  {
    std::atomic<bool> run(true);
    std::atomic<int> threadsDone(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < FLAGS_numThreads; ++i) {
      threads.push_back(std::thread([&] {
        FOR_EACH_RANGE(k, 0, numPerThread) {
          ++TCInt64;
        }
        std::atomic_fetch_add(&threadsDone, 1);
        while (run.load()) { usleep(100); }
      }));
    }

    // We create and increment another ThreadCachedInt here to make sure it
    // doesn't interact with the other instances
    ThreadCachedInt<int64_t> otherTCInt64(0, 10);
    otherTCInt64.set(33);
    ++otherTCInt64;

    while (threadsDone.load() < FLAGS_numThreads) { usleep(100); }

    ++otherTCInt64;

    // Threads are done incrementing, but caches have not been flushed yet, so
    // we have to readFull.
    EXPECT_NE(kNumInserts, TCInt64.readFast());
    EXPECT_EQ(kNumInserts, TCInt64.readFull());

    run.store(false);
    for (auto& t : threads) {
      t.join();
    }

  }  // Caches are flushed when threads finish
  EXPECT_EQ(kNumInserts, TCInt64.readFast());
}

#define MAKE_MT_CACHE_SIZE_BM(size)                             \
  void BM_mt_cache_size ## size (int iters, int cacheSize) {    \
    kNumInserts = iters;                                        \
    globalInt ## size.set(0);                                   \
    globalInt ## size.setCacheSize(cacheSize);                  \
    std::vector<std::thread> threads;                           \
    for (int i = 0; i < FLAGS_numThreads; ++i) {                \
      threads.push_back(std::thread(incFunc ## size));          \
    }                                                           \
    for (auto& t : threads) {                                   \
      t.join();                                                 \
    }                                                           \
  }
MAKE_MT_CACHE_SIZE_BM(64);
MAKE_MT_CACHE_SIZE_BM(32);

#define REG_BASELINE(name, inc_stmt)                            \
  BENCHMARK(FB_CONCATENATE(BM_mt_baseline_, name), iters) {     \
    const int iterPerThread = iters / FLAGS_numThreads;         \
    std::vector<std::thread> threads;                           \
    for (int i = 0; i < FLAGS_numThreads; ++i) {                \
      threads.push_back(std::thread([&]() {                     \
            for (int i = 0; i < iterPerThread; ++i) {           \
              inc_stmt;                                         \
            }                                                   \
          }));                                                  \
    }                                                           \
    for (auto& t : threads) {                                   \
      t.join();                                                 \
    }                                                           \
  }

ThreadLocal<int64_t> globalTL64Baseline;
ThreadLocal<int32_t> globalTL32Baseline;
std::atomic<int64_t> globalInt64Baseline(0);
std::atomic<int32_t> globalInt32Baseline(0);
FOLLY_TLS int64_t global__thread64;
FOLLY_TLS int32_t global__thread32;

// Alternate lock-free implementation.  Achieves about the same performance,
// but uses about 20x more memory than ThreadCachedInt with 24 threads.
struct ShardedAtomicInt {
  static const int64_t kBuckets_ = 2048;
  std::atomic<int64_t> ints_[kBuckets_];

  inline void inc(int64_t val = 1) {
    int bucket = hash::twang_mix64(
      uint64_t(pthread_self())) & (kBuckets_ - 1);
    std::atomic_fetch_add(&ints_[bucket], val);
  }

  // read the first few and extrapolate
  int64_t readFast() {
    int64_t ret = 0;
    static const int numToRead = 8;
    FOR_EACH_RANGE(i, 0, numToRead) {
      ret += ints_[i].load(std::memory_order_relaxed);
    }
    return ret * (kBuckets_ / numToRead);
  }

  // readFull is lock-free, but has to do thousands of loads...
  int64_t readFull() {
    int64_t ret = 0;
    for (auto& i : ints_) {
      // Fun fact - using memory_order_consume below reduces perf 30-40% in high
      // contention benchmarks.
      ret += i.load(std::memory_order_relaxed);
    }
    return ret;
  }
};
ShardedAtomicInt shd_int64;

REG_BASELINE(_thread64, global__thread64 += 1);
REG_BASELINE(_thread32, global__thread32 += 1);
REG_BASELINE(ThreadLocal64, *globalTL64Baseline += 1);
REG_BASELINE(ThreadLocal32, *globalTL32Baseline += 1);
REG_BASELINE(atomic_inc64,
             std::atomic_fetch_add(&globalInt64Baseline, int64_t(1)));
REG_BASELINE(atomic_inc32,
             std::atomic_fetch_add(&globalInt32Baseline, int32_t(1)));
REG_BASELINE(ShardedAtm64, shd_int64.inc());

BENCHMARK_PARAM(BM_mt_cache_size64, 0);
BENCHMARK_PARAM(BM_mt_cache_size64, 10);
BENCHMARK_PARAM(BM_mt_cache_size64, 100);
BENCHMARK_PARAM(BM_mt_cache_size64, 1000);
BENCHMARK_PARAM(BM_mt_cache_size32, 0);
BENCHMARK_PARAM(BM_mt_cache_size32, 10);
BENCHMARK_PARAM(BM_mt_cache_size32, 100);
BENCHMARK_PARAM(BM_mt_cache_size32, 1000);
BENCHMARK_DRAW_LINE();

// single threaded
BENCHMARK(Atomic_readFull) {
  doNotOptimizeAway(globalInt64Baseline.load(std::memory_order_relaxed));
}
BENCHMARK(ThrCache_readFull) {
  doNotOptimizeAway(globalInt64.readFull());
}
BENCHMARK(Sharded_readFull) {
  doNotOptimizeAway(shd_int64.readFull());
}
BENCHMARK(ThrCache_readFast) {
  doNotOptimizeAway(globalInt64.readFast());
}
BENCHMARK(Sharded_readFast) {
  doNotOptimizeAway(shd_int64.readFast());
}
BENCHMARK_DRAW_LINE();

// multi threaded
REG_BASELINE(Atomic_readFull,
      doNotOptimizeAway(globalInt64Baseline.load(std::memory_order_relaxed)));
REG_BASELINE(ThrCache_readFull, doNotOptimizeAway(globalInt64.readFull()));
REG_BASELINE(Sharded_readFull, doNotOptimizeAway(shd_int64.readFull()));
REG_BASELINE(ThrCache_readFast, doNotOptimizeAway(globalInt64.readFast()));
REG_BASELINE(Sharded_readFast, doNotOptimizeAway(shd_int64.readFast()));
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
    "bm_min_usec", "10000", gflags::SET_FLAG_IF_DEFAULT
  );
  if (FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return RUN_ALL_TESTS();
}

/*
 Ran with 20 threads on dual 12-core Xeon(R) X5650 @ 2.67GHz with 12-MB caches

 Benchmark                               Iters   Total t    t/iter iter/sec
 ------------------------------------------------------------------------------
 + 103% BM_mt_baseline__thread64     10000000  13.54 ms  1.354 ns  704.4 M
*       BM_mt_baseline__thread32     10000000  6.651 ms  665.1 ps    1.4 G
 +50.3% BM_mt_baseline_ThreadLocal64  10000000  9.994 ms  999.4 ps  954.2 M
 +49.9% BM_mt_baseline_ThreadLocal32  10000000  9.972 ms  997.2 ps  956.4 M
 +2650% BM_mt_baseline_atomic_inc64  10000000  182.9 ms  18.29 ns  52.13 M
 +2665% BM_mt_baseline_atomic_inc32  10000000  183.9 ms  18.39 ns  51.85 M
 +75.3% BM_mt_baseline_ShardedAtm64  10000000  11.66 ms  1.166 ns  817.8 M
 +6670% BM_mt_cache_size64/0         10000000  450.3 ms  45.03 ns  21.18 M
 +1644% BM_mt_cache_size64/10        10000000    116 ms   11.6 ns   82.2 M
 + 381% BM_mt_cache_size64/100       10000000  32.04 ms  3.204 ns  297.7 M
 + 129% BM_mt_cache_size64/1000      10000000  15.24 ms  1.524 ns  625.8 M
 +6052% BM_mt_cache_size32/0         10000000  409.2 ms  40.92 ns  23.31 M
 +1304% BM_mt_cache_size32/10        10000000  93.39 ms  9.339 ns  102.1 M
 + 298% BM_mt_cache_size32/100       10000000  26.52 ms  2.651 ns  359.7 M
 +68.1% BM_mt_cache_size32/1000      10000000  11.18 ms  1.118 ns  852.9 M
------------------------------------------------------------------------------
 +10.4% Atomic_readFull              10000000  36.05 ms  3.605 ns  264.5 M
 + 619% ThrCache_readFull            10000000  235.1 ms  23.51 ns  40.57 M
 SLOW   Sharded_readFull              1981093      2 s    1.01 us  967.3 k
*       ThrCache_readFast            10000000  32.65 ms  3.265 ns  292.1 M
 +10.0% Sharded_readFast             10000000  35.92 ms  3.592 ns  265.5 M
------------------------------------------------------------------------------
 +4.54% BM_mt_baseline_Atomic_readFull  10000000  8.672 ms  867.2 ps  1.074 G
 SLOW   BM_mt_baseline_ThrCache_readFull  10000000  996.9 ms  99.69 ns  9.567 M
 SLOW   BM_mt_baseline_Sharded_readFull  10000000  891.5 ms  89.15 ns   10.7 M
*       BM_mt_baseline_ThrCache_readFast  10000000  8.295 ms  829.5 ps  1.123 G
 +12.7% BM_mt_baseline_Sharded_readFast  10000000  9.348 ms  934.8 ps   1020 M
------------------------------------------------------------------------------
*/
