/*
 * Copyright 2016-present Facebook, Inc.
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

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include <google/base/spinlock.h>

#include <folly/Benchmark.h>
#include <folly/SharedMutex.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/SmallLocks.h>

/* "Work cycle" is just an additional nop loop iteration.
 * A smaller number of work cyles will result in more contention,
 * which is what we're trying to measure.  The relative ratio of
 * locked to unlocked cycles will simulate how big critical sections
 * are in production code
 */
DEFINE_int32(work, 100, "Number of work cycles");
DEFINE_int32(unlocked_work, 1000, "Number of unlocked work cycles");
DEFINE_int32(
    threads,
    std::thread::hardware_concurrency(),
    "Number of threads for fairness test");
DEFINE_bool(run_fairness, true, "Run fairness benchmarks");

static void burn(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(i);
  }
}

namespace {

template <typename Mutex>
std::unique_lock<Mutex> lock(Mutex& mutex) {
  return std::unique_lock<Mutex>{mutex};
}
template <typename Mutex, typename Other>
void unlock(Mutex&, Other) {}
auto lock(folly::DistributedMutex& mutex) {
  return mutex.lock();
}
template <typename State>
void unlock(folly::DistributedMutex& mutex, State state) {
  mutex.unlock(std::move(state));
}

struct SimpleBarrier {
  explicit SimpleBarrier(int count) : count_(count) {}
  void wait() {
    // we spin for a bit to try and get the kernel to schedule threads on
    // different cores
    for (auto i = 0; i < 100000; ++i) {
      folly::doNotOptimizeAway(i);
    }
    num_.fetch_add(1);
    while (num_.load() != count_) {
    }
  }

 private:
  std::atomic<int> num_{0};
  const int count_;
};
} // namespace

template <typename Lock>
class InitLock {
  Lock lock_;

 public:
  InitLock() {
    lock_.init();
  }
  void lock() {
    lock_.lock();
  }
  void unlock() {
    lock_.unlock();
  }
};

class GoogleSpinLockAdapter {
 public:
  void lock() {
    lock_.Lock();
  }
  void unlock() {
    lock_.Unlock();
  }

 private:
  SpinLock lock_;
};

template <typename Lock>
static void runContended(size_t numOps, size_t numThreads) {
  folly::BenchmarkSuspender braces;
  size_t totalthreads = std::thread::hardware_concurrency();
  if (totalthreads < numThreads) {
    totalthreads = numThreads;
  }
  size_t threadgroups = totalthreads / numThreads;
  struct lockstruct {
    char padding1[128];
    Lock mutex;
    char padding2[128];
    long value = 1;
  };

  auto locks =
      (struct lockstruct*)calloc(threadgroups, sizeof(struct lockstruct));

  char padding3[128];
  (void)padding3;
  std::vector<std::thread> threads(totalthreads);

  SimpleBarrier runbarrier(totalthreads + 1);

  for (size_t t = 0; t < totalthreads; ++t) {
    threads[t] = std::thread([&, t] {
      lockstruct* mutex = &locks[t % threadgroups];
      runbarrier.wait();
      for (size_t op = 0; op < numOps; op += 1) {
        auto state = lock(mutex->mutex);
        burn(FLAGS_work);
        mutex->value++;
        unlock(mutex->mutex, std::move(state));
        burn(FLAGS_unlocked_work);
      }
    });
  }

  runbarrier.wait();
  braces.dismissing([&] {
    for (auto& thr : threads) {
      thr.join();
    }
  });
}

template <typename Lock>
static void runFairness(std::size_t numThreads) {
  size_t totalthreads = std::thread::hardware_concurrency();
  if (totalthreads < numThreads) {
    totalthreads = numThreads;
  }
  long threadgroups = totalthreads / numThreads;
  struct lockstruct {
    char padding1[128];
    Lock lock;
  };

  auto locks =
      (struct lockstruct*)calloc(threadgroups, sizeof(struct lockstruct));

  char padding3[64];
  (void)padding3;
  std::vector<std::thread> threads(totalthreads);

  std::atomic<bool> stop{false};

  std::mutex rlock;
  std::vector<long> results;
  std::vector<std::chrono::microseconds> maxes;

  std::vector<std::chrono::microseconds> aqTime;
  std::vector<unsigned long> aqTimeSq;

  SimpleBarrier runbarrier(totalthreads + 1);

  for (size_t t = 0; t < totalthreads; ++t) {
    threads[t] = std::thread([&, t] {
      lockstruct* mutex = &locks[t % threadgroups];
      long value = 0;
      std::chrono::microseconds max(0);
      std::chrono::microseconds time(0);
      unsigned long timeSq(0);
      runbarrier.wait();
      while (!stop) {
        std::chrono::steady_clock::time_point prelock =
            std::chrono::steady_clock::now();
        auto state = lock(mutex->lock);
        std::chrono::steady_clock::time_point postlock =
            std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(
            postlock - prelock);
        time += diff;
        timeSq += diff.count() * diff.count();
        if (diff > max) {
          max = diff;
        }
        burn(FLAGS_work);
        value++;
        unlock(mutex->lock, std::move(state));
        burn(FLAGS_unlocked_work);
      }
      {
        std::lock_guard<std::mutex> g(rlock);
        results.push_back(value);
        maxes.push_back(max);
        aqTime.push_back(time);
        aqTimeSq.push_back(timeSq);
      }
    });
  }

  runbarrier.wait();
  /* sleep override */
  std::this_thread::sleep_for(std::chrono::seconds(4));
  stop = true;

  for (auto& thr : threads) {
    thr.join();
  }

  // Calulate some stats
  unsigned long sum = std::accumulate(results.begin(), results.end(), 0.0);
  double m = sum / results.size();

  double accum = 0.0;
  std::for_each(results.begin(), results.end(), [&](const double d) {
    accum += (d - m) * (d - m);
  });
  double stdev = std::sqrt(accum / (results.size() - 1));
  std::chrono::microseconds mx = *std::max_element(maxes.begin(), maxes.end());
  std::chrono::microseconds agAqTime = std::accumulate(
      aqTime.begin(), aqTime.end(), std::chrono::microseconds(0));
  unsigned long agAqTimeSq =
      std::accumulate(aqTimeSq.begin(), aqTimeSq.end(), 0);
  std::chrono::microseconds mean = agAqTime / sum;
  double variance = (sum * agAqTimeSq - (agAqTime.count() * agAqTime.count())) /
      sum / (sum - 1);
  double stddev2 = std::sqrt(variance);

  printf("Sum: %li Mean: %.0f stddev: %.0f\n", sum, m, stdev);
  printf(
      "Lock time stats in us: mean %li stddev %.0f max %li\n",
      mean.count(),
      stddev2,
      mx.count());
}

template <typename Mutex>
void runUncontended(std::size_t iters) {
  auto&& mutex = Mutex{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    auto state = lock(mutex);
    unlock(mutex, std::move(state));
  }
}

BENCHMARK(StdMutexUncontendedBenchmark, iters) {
  runUncontended<std::mutex>(iters);
}

BENCHMARK(GoogleSpinUncontendedBenchmark, iters) {
  runUncontended<GoogleSpinLockAdapter>(iters);
}

BENCHMARK(MicroSpinLockUncontendedBenchmark, iters) {
  runUncontended<InitLock<folly::MicroSpinLock>>(iters);
}

BENCHMARK(PicoSpinLockUncontendedBenchmark, iters) {
  runUncontended<InitLock<folly::PicoSpinLock<std::uint16_t>>>(iters);
}

BENCHMARK(MicroLockUncontendedBenchmark, iters) {
  runUncontended<InitLock<folly::MicroLock>>(iters);
}

BENCHMARK(SharedMutexUncontendedBenchmark, iters) {
  runUncontended<folly::SharedMutex>(iters);
}

BENCHMARK(DistributedMutexUncontendedBenchmark, iters) {
  runUncontended<folly::DistributedMutex>(iters);
}

BENCHMARK(AtomicFetchAddUncontendedBenchmark, iters) {
  auto&& atomic = std::atomic<uint64_t>{0};
  while (iters--) {
    folly::doNotOptimizeAway(atomic.fetch_add(1));
  }
}

struct VirtualBase {
  virtual void foo() = 0;
  virtual ~VirtualBase() {}
};

struct VirtualImpl : VirtualBase {
  void foo() override { /* noop */
  }
  ~VirtualImpl() override {}
};

#ifndef __clang__
__attribute__((noinline, noclone)) VirtualBase* makeVirtual() {
  return new VirtualImpl();
}

BENCHMARK(VirtualFunctionCall, iters) {
  VirtualBase* vb = makeVirtual();
  while (iters--) {
    vb->foo();
  }
  delete vb;
}
#endif

BENCHMARK_DRAW_LINE();

#define BENCH_BASE(...) FB_VA_GLUE(BENCHMARK_NAMED_PARAM, (__VA_ARGS__))
#define BENCH_REL(...) FB_VA_GLUE(BENCHMARK_RELATIVE_NAMED_PARAM, (__VA_ARGS__))

static void std_mutex(size_t numOps, size_t numThreads) {
  runContended<std::mutex>(numOps, numThreads);
}
static void google_spin(size_t numOps, size_t numThreads) {
  runContended<GoogleSpinLockAdapter>(numOps, numThreads);
}
static void folly_microspin(size_t numOps, size_t numThreads) {
  runContended<InitLock<folly::MicroSpinLock>>(numOps, numThreads);
}
static void folly_picospin(size_t numOps, size_t numThreads) {
  runContended<InitLock<folly::PicoSpinLock<uint16_t>>>(numOps, numThreads);
}
static void folly_microlock(size_t numOps, size_t numThreads) {
  runContended<folly::MicroLock>(numOps, numThreads);
}
static void folly_sharedmutex(size_t numOps, size_t numThreads) {
  runContended<folly::SharedMutex>(numOps, numThreads);
}
static void folly_distributedmutex(size_t numOps, size_t numThreads) {
  runContended<folly::DistributedMutex>(numOps, numThreads);
}

BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 1thread, 1)
BENCH_REL(google_spin, 1thread, 1)
BENCH_REL(folly_microspin, 1thread, 1)
BENCH_REL(folly_picospin, 1thread, 1)
BENCH_REL(folly_microlock, 1thread, 1)
BENCH_REL(folly_sharedmutex, 1thread, 1)
BENCH_REL(folly_distributedmutex, 1thread, 1)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 2thread, 2)
BENCH_REL(google_spin, 2thread, 2)
BENCH_REL(folly_microspin, 2thread, 2)
BENCH_REL(folly_picospin, 2thread, 2)
BENCH_REL(folly_microlock, 2thread, 2)
BENCH_REL(folly_sharedmutex, 2thread, 2)
BENCH_REL(folly_distributedmutex, 2thread, 2)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 4thread, 4)
BENCH_REL(google_spin, 4thread, 4)
BENCH_REL(folly_microspin, 4thread, 4)
BENCH_REL(folly_picospin, 4thread, 4)
BENCH_REL(folly_microlock, 4thread, 4)
BENCH_REL(folly_sharedmutex, 4thread, 4)
BENCH_REL(folly_distributedmutex, 4thread, 4)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 8thread, 8)
BENCH_REL(google_spin, 8thread, 8)
BENCH_REL(folly_microspin, 8thread, 8)
BENCH_REL(folly_picospin, 8thread, 8)
BENCH_REL(folly_microlock, 8thread, 8)
BENCH_REL(folly_sharedmutex, 8thread, 8)
BENCH_REL(folly_distributedmutex, 8thread, 8)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 16thread, 16)
BENCH_REL(google_spin, 16thread, 16)
BENCH_REL(folly_microspin, 16thread, 16)
BENCH_REL(folly_picospin, 16thread, 16)
BENCH_REL(folly_microlock, 16thread, 16)
BENCH_REL(folly_sharedmutex, 16thread, 16)
BENCH_REL(folly_distributedmutex, 16thread, 16)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 32thread, 32)
BENCH_REL(google_spin, 32thread, 32)
BENCH_REL(folly_microspin, 32thread, 32)
BENCH_REL(folly_picospin, 32thread, 32)
BENCH_REL(folly_microlock, 32thread, 32)
BENCH_REL(folly_sharedmutex, 32thread, 32)
BENCH_REL(folly_distributedmutex, 32thread, 32)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 64thread, 64)
BENCH_REL(google_spin, 64thread, 64)
BENCH_REL(folly_microspin, 64thread, 64)
BENCH_REL(folly_picospin, 64thread, 64)
BENCH_REL(folly_microlock, 64thread, 64)
BENCH_REL(folly_sharedmutex, 64thread, 64)
BENCH_REL(folly_distributedmutex, 64thread, 64)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 128thread, 128)
BENCH_REL(google_spin, 128thread, 128)
BENCH_REL(folly_microspin, 128thread, 128)
BENCH_REL(folly_picospin, 128thread, 128)
BENCH_REL(folly_microlock, 128thread, 128)
BENCH_REL(folly_sharedmutex, 128thread, 128)
BENCH_REL(folly_distributedmutex, 128thread, 128)

template <typename Mutex>
void fairnessTest(std::string type, std::size_t numThreads) {
  std::cout << "------- " << type << " " << numThreads << " threads";
  std::cout << std::endl;
  runFairness<Mutex>(numThreads);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_run_fairness) {
    for (auto numThreads : {2, 4, 8, 16, 32, 64}) {
      fairnessTest<std::mutex>("std::mutex", numThreads);
      fairnessTest<GoogleSpinLockAdapter>("GoogleSpinLock", numThreads);
      fairnessTest<InitLock<folly::MicroSpinLock>>(
          "folly::MicroSpinLock", numThreads);
      fairnessTest<InitLock<folly::PicoSpinLock<std::uint16_t>>>(
          "folly::PicoSpinLock<std::uint16_t>", numThreads);
      fairnessTest<InitLock<folly::MicroLock>>("folly::MicroLock", numThreads);
      fairnessTest<folly::SharedMutex>("folly::SharedMutex", numThreads);
      fairnessTest<folly::DistributedMutex>(
          "folly::DistributedMutex", numThreads);

      std::cout << std::string(76, '=') << std::endl;
    }
  }

  folly::runBenchmarks();

  return 0;
}

/*
./small_locks_benchmark --bm_min_iters=100000
Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz

------- std::mutex 2 threads
Sum: 107741003 Mean: 1923946 stddev: 99873
Lock time stats in us: mean 1 stddev 40 max 53562
------- GoogleSpinLock 2 threads
Sum: 129434359 Mean: 2311327 stddev: 74053
Lock time stats in us: mean 0 stddev 4 max 53102
------- folly::MicroSpinLock 2 threads
Sum: 225366606 Mean: 4024403 stddev: 1884122
Lock time stats in us: mean 0 stddev 19 max 2278444
------- folly::PicoSpinLock<std::uint16_t> 2 threads
Sum: 150216526 Mean: 2682437 stddev: 216045
Lock time stats in us: mean 0 stddev 28 max 36826
------- folly::MicroLock 2 threads
Sum: 132299209 Mean: 2362485 stddev: 496423
Lock time stats in us: mean 0 stddev 32 max 68123
------- folly::SharedMutex 2 threads
Sum: 132465497 Mean: 2365455 stddev: 556997
Lock time stats in us: mean 0 stddev 32 max 24447
------- folly::DistributedMutex 2 threads
Sum: 166667563 Mean: 2976206 stddev: 183292
Lock time stats in us: mean 0 stddev 3 max 2834
============================================================================
------- std::mutex 4 threads
Sum: 56176633 Mean: 1003154 stddev: 20354
Lock time stats in us: mean 2 stddev 76 max 10151
------- GoogleSpinLock 4 threads
Sum: 65060684 Mean: 1161797 stddev: 95631
Lock time stats in us: mean 1 stddev 66 max 9624
------- folly::MicroSpinLock 4 threads
Sum: 124794912 Mean: 2228480 stddev: 752355
Lock time stats in us: mean 1 stddev 2 max 1973546
------- folly::PicoSpinLock<std::uint16_t> 4 threads
Sum: 86858717 Mean: 1551048 stddev: 417050
Lock time stats in us: mean 1 stddev 2 max 87873
------- folly::MicroLock 4 threads
Sum: 64529361 Mean: 1152310 stddev: 363331
Lock time stats in us: mean 2 stddev 66 max 34196
------- folly::SharedMutex 4 threads
Sum: 64509031 Mean: 1151946 stddev: 551973
Lock time stats in us: mean 2 stddev 5 max 58400
------- folly::DistributedMutex 4 threads
Sum: 76778688 Mean: 1371048 stddev: 89767
Lock time stats in us: mean 2 stddev 56 max 4038
============================================================================
------- std::mutex 8 threads
Sum: 27905504 Mean: 498312 stddev: 12266
Lock time stats in us: mean 4 stddev 154 max 10915
------- GoogleSpinLock 8 threads
Sum: 34900763 Mean: 623227 stddev: 34990
Lock time stats in us: mean 3 stddev 4 max 11047
------- folly::MicroSpinLock 8 threads
Sum: 65703639 Mean: 1173279 stddev: 367466
Lock time stats in us: mean 2 stddev 65 max 1985454
------- folly::PicoSpinLock<std::uint16_t> 8 threads
Sum: 46642042 Mean: 832893 stddev: 258465
Lock time stats in us: mean 3 stddev 5 max 90012
------- folly::MicroLock 8 threads
Sum: 28727093 Mean: 512983 stddev: 105746
Lock time stats in us: mean 6 stddev 149 max 24648
------- folly::SharedMutex 8 threads
Sum: 35789774 Mean: 639103 stddev: 420746
Lock time stats in us: mean 5 stddev 120 max 95030
------- folly::DistributedMutex 8 threads
Sum: 33288752 Mean: 594442 stddev: 20581
Lock time stats in us: mean 5 stddev 129 max 7018
============================================================================
------- std::mutex 16 threads
Sum: 10886472 Mean: 194401 stddev: 9357
Lock time stats in us: mean 12 stddev 394 max 13293
------- GoogleSpinLock 16 threads
Sum: 13436731 Mean: 239941 stddev: 25068
Lock time stats in us: mean 10 stddev 319 max 10127
------- folly::MicroSpinLock 16 threads
Sum: 28766414 Mean: 513685 stddev: 109667
Lock time stats in us: mean 7 stddev 149 max 453504
------- folly::PicoSpinLock<std::uint16_t> 16 threads
Sum: 19795815 Mean: 353496 stddev: 110097
Lock time stats in us: mean 10 stddev 217 max 164821
------- folly::MicroLock 16 threads
Sum: 11380567 Mean: 203224 stddev: 25356
Lock time stats in us: mean 15 stddev 377 max 13342
------- folly::SharedMutex 16 threads
Sum: 13734684 Mean: 245262 stddev: 132500
Lock time stats in us: mean 15 stddev 312 max 75465
------- folly::DistributedMutex 16 threads
Sum: 13463633 Mean: 240422 stddev: 8070
Lock time stats in us: mean 15 stddev 319 max 17020
============================================================================
------- std::mutex 32 threads
Sum: 3584545 Mean: 64009 stddev: 1099
Lock time stats in us: mean 39 stddev 1197 max 12949
------- GoogleSpinLock 32 threads
Sum: 4537642 Mean: 81029 stddev: 7258
Lock time stats in us: mean 28 stddev 946 max 10736
------- folly::MicroSpinLock 32 threads
Sum: 9493894 Mean: 169533 stddev: 42004
Lock time stats in us: mean 23 stddev 452 max 934519
------- folly::PicoSpinLock<std::uint16_t> 32 threads
Sum: 7159818 Mean: 127853 stddev: 20791
Lock time stats in us: mean 30 stddev 599 max 116982
------- folly::MicroLock 32 threads
Sum: 4052635 Mean: 72368 stddev: 10196
Lock time stats in us: mean 38 stddev 1059 max 13123
------- folly::SharedMutex 32 threads
Sum: 4207373 Mean: 75131 stddev: 36441
Lock time stats in us: mean 51 stddev 1019 max 89781
------- folly::DistributedMutex 32 threads
Sum: 4499483 Mean: 80347 stddev: 1684
Lock time stats in us: mean 48 stddev 954 max 18793
============================================================================
------- std::mutex 64 threads
Sum: 3584393 Mean: 56006 stddev: 989
Lock time stats in us: mean 48 stddev 1197 max 12681
------- GoogleSpinLock 64 threads
Sum: 4541415 Mean: 70959 stddev: 2042
Lock time stats in us: mean 34 stddev 945 max 12997
------- folly::MicroSpinLock 64 threads
Sum: 9464010 Mean: 147875 stddev: 43363
Lock time stats in us: mean 26 stddev 453 max 464213
------- folly::PicoSpinLock<std::uint16_t> 64 threads
Sum: 6915111 Mean: 108048 stddev: 15833
Lock time stats in us: mean 36 stddev 620 max 162031
------- folly::MicroLock 64 threads
Sum: 4008803 Mean: 62637 stddev: 6055
Lock time stats in us: mean 46 stddev 1070 max 25289
------- folly::SharedMutex 64 threads
Sum: 3580719 Mean: 55948 stddev: 23224
Lock time stats in us: mean 68 stddev 1198 max 63328
------- folly::DistributedMutex 64 threads
Sum: 4464065 Mean: 69751 stddev: 2299
Lock time stats in us: mean 56 stddev 960 max 32873
============================================================================
============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                16.73ns   59.77M
GoogleSpinUncontendedBenchmark                              11.26ns   88.80M
MicroSpinLockUncontendedBenchmark                           10.06ns   99.44M
PicoSpinLockUncontendedBenchmark                            11.25ns   88.89M
MicroLockUncontendedBenchmark                               19.20ns   52.09M
SharedMutexUncontendedBenchmark                             19.45ns   51.40M
DistributedMutexUncontendedBenchmark                        17.02ns   58.75M
AtomicFetchAddUncontendedBenchmark                           5.47ns  182.91M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         802.21ns    1.25M
google_spin(1thread)                             109.81%   730.52ns    1.37M
folly_microspin(1thread)                         119.16%   673.22ns    1.49M
folly_picospin(1thread)                          119.02%   673.99ns    1.48M
folly_microlock(1thread)                         131.67%   609.28ns    1.64M
folly_sharedmutex(1thread)                       118.41%   677.46ns    1.48M
folly_distributedmutex(1thread)                  100.27%   800.02ns    1.25M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.30us  769.21K
google_spin(2thread)                             129.59%     1.00us  996.85K
folly_microspin(2thread)                         158.13%   822.13ns    1.22M
folly_picospin(2thread)                          150.43%   864.23ns    1.16M
folly_microlock(2thread)                         144.94%   896.92ns    1.11M
folly_sharedmutex(2thread)                       120.36%     1.08us  925.83K
folly_distributedmutex(2thread)                  112.98%     1.15us  869.08K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.36us  424.08K
google_spin(4thread)                             120.20%     1.96us  509.75K
folly_microspin(4thread)                         109.07%     2.16us  462.53K
folly_picospin(4thread)                          113.37%     2.08us  480.78K
folly_microlock(4thread)                          83.88%     2.81us  355.71K
folly_sharedmutex(4thread)                        90.47%     2.61us  383.65K
folly_distributedmutex(4thread)                  121.82%     1.94us  516.63K
----------------------------------------------------------------------------
std_mutex(8thread)                                           5.39us  185.64K
google_spin(8thread)                             127.72%     4.22us  237.10K
folly_microspin(8thread)                         106.70%     5.05us  198.08K
folly_picospin(8thread)                           88.02%     6.12us  163.41K
folly_microlock(8thread)                          79.78%     6.75us  148.11K
folly_sharedmutex(8thread)                        78.25%     6.88us  145.26K
folly_distributedmutex(8thread)                  162.74%     3.31us  302.12K
----------------------------------------------------------------------------
std_mutex(16thread)                                         11.74us   85.16K
google_spin(16thread)                            109.91%    10.68us   93.60K
folly_microspin(16thread)                        103.93%    11.30us   88.50K
folly_picospin(16thread)                          50.36%    23.32us   42.89K
folly_microlock(16thread)                         55.85%    21.03us   47.56K
folly_sharedmutex(16thread)                       64.27%    18.27us   54.74K
folly_distributedmutex(16thread)                 181.32%     6.48us  154.41K
----------------------------------------------------------------------------
std_mutex(32thread)                                         31.56us   31.68K
google_spin(32thread)                             95.17%    33.17us   30.15K
folly_microspin(32thread)                        100.60%    31.38us   31.87K
folly_picospin(32thread)                          31.30%   100.84us    9.92K
folly_microlock(32thread)                         55.04%    57.35us   17.44K
folly_sharedmutex(32thread)                       65.09%    48.49us   20.62K
folly_distributedmutex(32thread)                 177.39%    17.79us   56.20K
----------------------------------------------------------------------------
std_mutex(64thread)                                         39.90us   25.06K
google_spin(64thread)                            110.92%    35.98us   27.80K
folly_microspin(64thread)                        105.98%    37.65us   26.56K
folly_picospin(64thread)                          33.03%   120.80us    8.28K
folly_microlock(64thread)                         58.02%    68.78us   14.54K
folly_sharedmutex(64thread)                       68.43%    58.32us   17.15K
folly_distributedmutex(64thread)                 200.38%    19.91us   50.22K
----------------------------------------------------------------------------
std_mutex(128thread)                                        75.67us   13.21K
google_spin(128thread)                           116.14%    65.16us   15.35K
folly_microspin(128thread)                       100.82%    75.06us   13.32K
folly_picospin(128thread)                         44.99%   168.21us    5.94K
folly_microlock(128thread)                        53.93%   140.31us    7.13K
folly_sharedmutex(128thread)                      64.37%   117.55us    8.51K
folly_distributedmutex(128thread)                185.71%    40.75us   24.54K
============================================================================
*/
