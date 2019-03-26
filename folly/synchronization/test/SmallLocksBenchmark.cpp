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
#include <array>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include <google/base/spinlock.h>

#include <folly/Benchmark.h>
#include <folly/CachelinePadded.h>
#include <folly/SharedMutex.h>
#include <folly/experimental/flat_combining/FlatCombining.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/SmallLocks.h>
#include <folly/synchronization/Utility.h>

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

class DistributedMutexFlatCombining {
 public:
  folly::DistributedMutex mutex_;
};

class NoLock {
 public:
  void lock() {}
  void unlock() {}
};

class FlatCombiningMutexNoCaching
    : public folly::FlatCombining<FlatCombiningMutexNoCaching> {
 public:
  using Super = folly::FlatCombining<FlatCombiningMutexNoCaching>;

  template <typename CriticalSection>
  auto lock_combine(CriticalSection func, std::size_t) {
    auto record = this->allocRec();
    auto value = folly::invoke_result_t<CriticalSection&>{};
    this->requestFC([&]() { value = func(); }, record);
    this->freeRec(record);
    return value;
  }
};

class FlatCombiningMutexCaching
    : public folly::FlatCombining<FlatCombiningMutexCaching> {
 public:
  using Super = folly::FlatCombining<FlatCombiningMutexCaching>;

  FlatCombiningMutexCaching() {
    for (auto i = 0; i < 256; ++i) {
      this->records_.push_back(this->allocRec());
    }
  }

  template <typename CriticalSection>
  auto lock_combine(CriticalSection func, std::size_t index) {
    auto value = folly::invoke_result_t<CriticalSection&>{};
    this->requestFC([&]() { value = func(); }, records_.at(index));
    return value;
  }

  std::vector<Super::Rec*> records_;
};

template <typename Mutex, typename CriticalSection>
auto lock_and(Mutex& mutex, std::size_t, CriticalSection func) {
  auto lck = folly::make_unique_lock(mutex);
  return func();
}
template <typename F>
auto lock_and(DistributedMutexFlatCombining& mutex, std::size_t, F func) {
  return mutex.mutex_.lock_combine(std::move(func));
}
template <typename F>
auto lock_and(FlatCombiningMutexNoCaching& mutex, std::size_t i, F func) {
  return mutex.lock_combine(func, i);
}
template <typename F>
auto lock_and(FlatCombiningMutexCaching& mutex, std::size_t i, F func) {
  return mutex.lock_combine(func, i);
}

template <typename Mutex>
std::unique_lock<Mutex> lock(Mutex& mutex) {
  return std::unique_lock<Mutex>{mutex};
}
template <typename Mutex, typename Other>
void unlock(Mutex&, Other) {}

/**
 * Functions to initialize, write and read from data
 *
 * These are used to do different things in the contended benchmark based on
 * the type of the data
 */
std::uint64_t write(std::uint64_t& value) {
  return ++value;
}
void read(std::uint64_t value) {
  folly::doNotOptimizeAway(value);
}
void initialize(std::uint64_t& value) {
  value = 1;
}

class alignas(folly::hardware_destructive_interference_size) Ints {
 public:
  std::array<folly::CachelinePadded<std::uint64_t>, 5> ints_;
};
std::uint64_t write(Ints& vec) {
  auto sum = std::uint64_t{0};
  for (auto& integer : vec.ints_) {
    sum += (*integer += 1);
  }
  return sum;
}
void initialize(Ints&) {}

class alignas(folly::hardware_destructive_interference_size) AtomicsAdd {
 public:
  std::array<folly::CachelinePadded<std::atomic<std::uint64_t>>, 5> ints_;
};
std::uint64_t write(AtomicsAdd& atomics) {
  auto sum = 0;
  for (auto& integer : atomics.ints_) {
    sum += integer->fetch_add(1);
  }
  return sum;
}
void initialize(AtomicsAdd&) {}

class alignas(folly::hardware_destructive_interference_size) AtomicCas {
 public:
  std::atomic<std::uint64_t> integer_{0};
};
std::uint64_t write(AtomicCas& atomic) {
  auto value = atomic.integer_.load();
  while (!atomic.integer_.compare_exchange_strong(value, value + 1)) {
  }
  return value;
}
void initialize(AtomicCas&) {}

template <typename Lock, typename Data = std::uint64_t>
static void
runContended(size_t numOps, size_t numThreads, size_t work = FLAGS_work) {
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
    Data value;
  };

  auto locks = std::vector<lockstruct>(threadgroups);
  for (auto& data : locks) {
    initialize(data.value);
  }
  folly::makeUnpredictable(locks);

  char padding3[128];
  (void)padding3;
  std::vector<std::thread> threads(totalthreads);

  SimpleBarrier runbarrier(totalthreads + 1);

  for (size_t t = 0; t < totalthreads; ++t) {
    threads[t] = std::thread([&, t] {
      lockstruct* mutex = &locks[t % threadgroups];
      runbarrier.wait();
      for (size_t op = 0; op < numOps; op += 1) {
        auto val = lock_and(mutex->mutex, t, [& value = mutex->value, work] {
          burn(work);
          return write(value);
        });
        read(val);
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
    folly::makeUnpredictable(mutex);
    auto state = lock(mutex);
    folly::makeUnpredictable(mutex);
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
static void folly_distributedmutex_combining(size_t ops, size_t threads) {
  runContended<DistributedMutexFlatCombining>(ops, threads);
}
static void folly_flatcombining_no_caching(size_t numOps, size_t numThreads) {
  runContended<FlatCombiningMutexNoCaching>(numOps, numThreads);
}
static void folly_flatcombining_caching(size_t numOps, size_t numThreads) {
  runContended<FlatCombiningMutexCaching>(numOps, numThreads);
}

static void std_mutex_simple(size_t numOps, size_t numThreads) {
  runContended<std::mutex, Ints>(numOps, numThreads, 0);
}
static void google_spin_simple(size_t numOps, size_t numThreads) {
  runContended<GoogleSpinLockAdapter, Ints>(numOps, numThreads, 0);
}
static void folly_microspin_simple(size_t numOps, size_t numThreads) {
  runContended<InitLock<folly::MicroSpinLock>, Ints>(numOps, numThreads, 0);
}
static void folly_picospin_simple(size_t numOps, size_t numThreads) {
  runContended<InitLock<folly::PicoSpinLock<uint16_t>>, Ints>(
      numOps, numThreads, 0);
}
static void folly_microlock_simple(size_t numOps, size_t numThreads) {
  runContended<folly::MicroLock, Ints>(numOps, numThreads, 0);
}
static void folly_sharedmutex_simple(size_t numOps, size_t numThreads) {
  runContended<folly::SharedMutex, Ints>(numOps, numThreads, 0);
}
static void folly_distributedmutex_simple(size_t numOps, size_t numThreads) {
  runContended<folly::DistributedMutex, Ints>(numOps, numThreads, 0);
}
static void folly_distributedmutex_combining_simple(size_t o, size_t t) {
  runContended<DistributedMutexFlatCombining, Ints>(o, t, 0);
}
static void atomics_fetch_add(size_t numOps, size_t numThreads) {
  runContended<NoLock, AtomicsAdd>(numOps, numThreads, 0);
}
static void atomic_cas(size_t numOps, size_t numThreads) {
  runContended<NoLock, AtomicCas>(numOps, numThreads, 0);
}
static void folly_flatcombining_no_caching_simple(size_t ops, size_t threads) {
  runContended<FlatCombiningMutexNoCaching>(ops, threads, 0);
}
static void folly_flatcombining_caching_simple(size_t ops, size_t threads) {
  runContended<FlatCombiningMutexCaching>(ops, threads, 0);
}

BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 1thread, 1)
BENCH_REL(google_spin, 1thread, 1)
BENCH_REL(folly_microspin, 1thread, 1)
BENCH_REL(folly_picospin, 1thread, 1)
BENCH_REL(folly_microlock, 1thread, 1)
BENCH_REL(folly_sharedmutex, 1thread, 1)
BENCH_REL(folly_distributedmutex, 1thread, 1)
BENCH_REL(folly_distributedmutex_combining, 1thread, 1)
BENCH_REL(folly_flatcombining_no_caching, 1thread, 1)
BENCH_REL(folly_flatcombining_caching, 1thread, 1)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 2thread, 2)
BENCH_REL(google_spin, 2thread, 2)
BENCH_REL(folly_microspin, 2thread, 2)
BENCH_REL(folly_picospin, 2thread, 2)
BENCH_REL(folly_microlock, 2thread, 2)
BENCH_REL(folly_sharedmutex, 2thread, 2)
BENCH_REL(folly_distributedmutex, 2thread, 2)
BENCH_REL(folly_distributedmutex_combining, 2thread, 2)
BENCH_REL(folly_flatcombining_no_caching, 2thread, 2)
BENCH_REL(folly_flatcombining_caching, 2thread, 2)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 4thread, 4)
BENCH_REL(google_spin, 4thread, 4)
BENCH_REL(folly_microspin, 4thread, 4)
BENCH_REL(folly_picospin, 4thread, 4)
BENCH_REL(folly_microlock, 4thread, 4)
BENCH_REL(folly_sharedmutex, 4thread, 4)
BENCH_REL(folly_distributedmutex, 4thread, 4)
BENCH_REL(folly_distributedmutex_combining, 4thread, 4)
BENCH_REL(folly_flatcombining_no_caching, 4thread, 4)
BENCH_REL(folly_flatcombining_caching, 4thread, 4)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 8thread, 8)
BENCH_REL(google_spin, 8thread, 8)
BENCH_REL(folly_microspin, 8thread, 8)
BENCH_REL(folly_picospin, 8thread, 8)
BENCH_REL(folly_microlock, 8thread, 8)
BENCH_REL(folly_sharedmutex, 8thread, 8)
BENCH_REL(folly_distributedmutex, 8thread, 8)
BENCH_REL(folly_distributedmutex_combining, 8thread, 8)
BENCH_REL(folly_flatcombining_no_caching, 8thread, 8)
BENCH_REL(folly_flatcombining_caching, 8thread, 8)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 16thread, 16)
BENCH_REL(google_spin, 16thread, 16)
BENCH_REL(folly_microspin, 16thread, 16)
BENCH_REL(folly_picospin, 16thread, 16)
BENCH_REL(folly_microlock, 16thread, 16)
BENCH_REL(folly_sharedmutex, 16thread, 16)
BENCH_REL(folly_distributedmutex, 16thread, 16)
BENCH_REL(folly_distributedmutex_combining, 16thread, 16)
BENCH_REL(folly_flatcombining_no_caching, 16thread, 16)
BENCH_REL(folly_flatcombining_caching, 16thread, 16)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 32thread, 32)
BENCH_REL(google_spin, 32thread, 32)
BENCH_REL(folly_microspin, 32thread, 32)
BENCH_REL(folly_picospin, 32thread, 32)
BENCH_REL(folly_microlock, 32thread, 32)
BENCH_REL(folly_sharedmutex, 32thread, 32)
BENCH_REL(folly_distributedmutex, 32thread, 32)
BENCH_REL(folly_distributedmutex_combining, 32thread, 32)
BENCH_REL(folly_flatcombining_no_caching, 32thread, 32)
BENCH_REL(folly_flatcombining_caching, 32thread, 32)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 64thread, 64)
BENCH_REL(google_spin, 64thread, 64)
BENCH_REL(folly_microspin, 64thread, 64)
BENCH_REL(folly_picospin, 64thread, 64)
BENCH_REL(folly_microlock, 64thread, 64)
BENCH_REL(folly_sharedmutex, 64thread, 64)
BENCH_REL(folly_distributedmutex, 64thread, 64)
BENCH_REL(folly_distributedmutex_combining, 64thread, 64)
BENCH_REL(folly_flatcombining_no_caching, 64thread, 64)
BENCH_REL(folly_flatcombining_caching, 64thread, 64)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 128thread, 128)
BENCH_REL(google_spin, 128thread, 128)
BENCH_REL(folly_microspin, 128thread, 128)
BENCH_REL(folly_picospin, 128thread, 128)
BENCH_REL(folly_microlock, 128thread, 128)
BENCH_REL(folly_sharedmutex, 128thread, 128)
BENCH_REL(folly_distributedmutex, 128thread, 128)
BENCH_REL(folly_distributedmutex_combining, 128thread, 128)
BENCH_REL(folly_flatcombining_no_caching, 128thread, 128)
BENCH_REL(folly_flatcombining_caching, 128thread, 128)

BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 1thread, 1)
BENCH_REL(google_spin_simple, 1thread, 1)
BENCH_REL(folly_microspin_simple, 1thread, 1)
BENCH_REL(folly_picospin_simple, 1thread, 1)
BENCH_REL(folly_microlock_simple, 1thread, 1)
BENCH_REL(folly_sharedmutex_simple, 1thread, 1)
BENCH_REL(folly_distributedmutex_simple, 1thread, 1)
BENCH_REL(folly_distributedmutex_combining_simple, 1thread, 1)
BENCH_REL(folly_flatcombining_no_caching_simple, 1thread, 1)
BENCH_REL(folly_flatcombining_caching_simple, 1thread, 1)
BENCH_REL(atomics_fetch_add, 1thread, 1)
BENCH_REL(atomic_cas, 1thread, 1)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 2thread, 2)
BENCH_REL(google_spin_simple, 2thread, 2)
BENCH_REL(folly_microspin_simple, 2thread, 2)
BENCH_REL(folly_picospin_simple, 2thread, 2)
BENCH_REL(folly_microlock_simple, 2thread, 2)
BENCH_REL(folly_sharedmutex_simple, 2thread, 2)
BENCH_REL(folly_distributedmutex_simple, 2thread, 2)
BENCH_REL(folly_distributedmutex_combining_simple, 2thread, 2)
BENCH_REL(folly_flatcombining_no_caching_simple, 2thread, 2)
BENCH_REL(folly_flatcombining_caching_simple, 2thread, 2)
BENCH_REL(atomics_fetch_add, 2thread, 2)
BENCH_REL(atomic_cas, 2thread, 2)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 4thread, 4)
BENCH_REL(google_spin_simple, 4thread, 4)
BENCH_REL(folly_microspin_simple, 4thread, 4)
BENCH_REL(folly_picospin_simple, 4thread, 4)
BENCH_REL(folly_microlock_simple, 4thread, 4)
BENCH_REL(folly_sharedmutex_simple, 4thread, 4)
BENCH_REL(folly_distributedmutex_simple, 4thread, 4)
BENCH_REL(folly_distributedmutex_combining_simple, 4thread, 4)
BENCH_REL(folly_flatcombining_no_caching_simple, 4thread, 4)
BENCH_REL(folly_flatcombining_caching_simple, 4thread, 4)
BENCH_REL(atomics_fetch_add, 4thread, 4)
BENCH_REL(atomic_cas, 4thread, 4)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 8thread, 8)
BENCH_REL(google_spin_simple, 8thread, 8)
BENCH_REL(folly_microspin_simple, 8thread, 8)
BENCH_REL(folly_picospin_simple, 8thread, 8)
BENCH_REL(folly_microlock_simple, 8thread, 8)
BENCH_REL(folly_sharedmutex_simple, 8thread, 8)
BENCH_REL(folly_distributedmutex_simple, 8thread, 8)
BENCH_REL(folly_distributedmutex_combining_simple, 8thread, 8)
BENCH_REL(folly_flatcombining_no_caching_simple, 8thread, 8)
BENCH_REL(folly_flatcombining_caching_simple, 8thread, 8)
BENCH_REL(atomics_fetch_add, 8thread, 8)
BENCH_REL(atomic_cas, 8thread, 8)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 16thread, 16)
BENCH_REL(google_spin_simple, 16thread, 16)
BENCH_REL(folly_microspin_simple, 16thread, 16)
BENCH_REL(folly_picospin_simple, 16thread, 16)
BENCH_REL(folly_microlock_simple, 16thread, 16)
BENCH_REL(folly_sharedmutex_simple, 16thread, 16)
BENCH_REL(folly_distributedmutex_simple, 16thread, 16)
BENCH_REL(folly_distributedmutex_combining_simple, 16thread, 16)
BENCH_REL(folly_flatcombining_no_caching_simple, 16thread, 16)
BENCH_REL(folly_flatcombining_caching_simple, 16thread, 16)
BENCH_REL(atomics_fetch_add, 16thread, 16)
BENCH_REL(atomic_cas, 16thread, 16)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 32thread, 32)
BENCH_REL(google_spin_simple, 32thread, 32)
BENCH_REL(folly_microspin_simple, 32thread, 32)
BENCH_REL(folly_picospin_simple, 32thread, 32)
BENCH_REL(folly_microlock_simple, 32thread, 32)
BENCH_REL(folly_sharedmutex_simple, 32thread, 32)
BENCH_REL(folly_distributedmutex_simple, 32thread, 32)
BENCH_REL(folly_distributedmutex_combining_simple, 32thread, 32)
BENCH_REL(folly_flatcombining_no_caching_simple, 32thread, 32)
BENCH_REL(folly_flatcombining_caching_simple, 32thread, 32)
BENCH_REL(atomics_fetch_add, 32thread, 32)
BENCH_REL(atomic_cas, 32thread, 32)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 64thread, 64)
BENCH_REL(google_spin_simple, 64thread, 64)
BENCH_REL(folly_microspin_simple, 64thread, 64)
BENCH_REL(folly_picospin_simple, 64thread, 64)
BENCH_REL(folly_microlock_simple, 64thread, 64)
BENCH_REL(folly_sharedmutex_simple, 64thread, 64)
BENCH_REL(folly_distributedmutex_simple, 64thread, 64)
BENCH_REL(folly_distributedmutex_combining_simple, 64thread, 64)
BENCH_REL(folly_flatcombining_no_caching_simple, 64thread, 64)
BENCH_REL(folly_flatcombining_caching_simple, 64thread, 64)
BENCH_REL(atomics_fetch_add, 64thread, 64)
BENCH_REL(atomic_cas, 64thread, 64)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex_simple, 128thread, 128)
BENCH_REL(google_spin_simple, 128thread, 128)
BENCH_REL(folly_microspin_simple, 128thread, 128)
BENCH_REL(folly_picospin_simple, 128thread, 128)
BENCH_REL(folly_microlock_simple, 128thread, 128)
BENCH_REL(folly_sharedmutex_simple, 128thread, 128)
BENCH_REL(folly_distributedmutex_simple, 128thread, 128)
BENCH_REL(folly_distributedmutex_combining_simple, 128thread, 128)
BENCH_REL(folly_flatcombining_no_caching_simple, 128thread, 128)
BENCH_REL(folly_flatcombining_caching_simple, 128thread, 128)
BENCH_REL(atomics_fetch_add, 128thread, 128)
BENCH_REL(atomic_cas, 128thread, 128)

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
StdMutexUncontendedBenchmark                                18.85ns   53.04M
GoogleSpinUncontendedBenchmark                              11.25ns   88.87M
MicroSpinLockUncontendedBenchmark                           10.95ns   91.34M
PicoSpinLockUncontendedBenchmark                            20.38ns   49.06M
MicroLockUncontendedBenchmark                               28.60ns   34.96M
SharedMutexUncontendedBenchmark                             19.51ns   51.25M
DistributedMutexUncontendedBenchmark                        25.27ns   39.58M
AtomicFetchAddUncontendedBenchmark                           5.47ns  182.91M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         797.34ns    1.25M
google_spin(1thread)                             101.28%   787.29ns    1.27M
folly_microspin(1thread)                         118.32%   673.90ns    1.48M
folly_picospin(1thread)                          118.36%   673.66ns    1.48M
folly_microlock(1thread)                         117.98%   675.84ns    1.48M
folly_sharedmutex(1thread)                       118.40%   673.41ns    1.48M
folly_distributedmutex(1thread)                  116.11%   686.74ns    1.46M
folly_distributedmutex_combining(1thread)        115.05%   693.05ns    1.44M
folly_flatcombining_no_caching(1thread)           90.40%   882.05ns    1.13M
folly_flatcombining_caching(1thread)             107.30%   743.08ns    1.35M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.14us  874.72K
google_spin(2thread)                             120.79%   946.42ns    1.06M
folly_microspin(2thread)                         136.28%   838.90ns    1.19M
folly_picospin(2thread)                          133.80%   854.45ns    1.17M
folly_microlock(2thread)                         111.09%     1.03us  971.76K
folly_sharedmutex(2thread)                       109.19%     1.05us  955.10K
folly_distributedmutex(2thread)                  106.62%     1.07us  932.65K
folly_distributedmutex_combining(2thread)        105.45%     1.08us  922.42K
folly_flatcombining_no_caching(2thread)           74.73%     1.53us  653.70K
folly_flatcombining_caching(2thread)              82.78%     1.38us  724.05K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.39us  418.41K
google_spin(4thread)                             128.49%     1.86us  537.63K
folly_microspin(4thread)                         102.60%     2.33us  429.28K
folly_picospin(4thread)                          111.94%     2.14us  468.37K
folly_microlock(4thread)                          78.19%     3.06us  327.16K
folly_sharedmutex(4thread)                        86.30%     2.77us  361.11K
folly_distributedmutex(4thread)                  138.25%     1.73us  578.44K
folly_distributedmutex_combining(4thread)        146.96%     1.63us  614.90K
folly_flatcombining_no_caching(4thread)           87.93%     2.72us  367.90K
folly_flatcombining_caching(4thread)              96.09%     2.49us  402.04K
----------------------------------------------------------------------------
std_mutex(8thread)                                           3.84us  260.54K
google_spin(8thread)                              98.58%     3.89us  256.83K
folly_microspin(8thread)                          64.01%     6.00us  166.77K
folly_picospin(8thread)                           64.76%     5.93us  168.72K
folly_microlock(8thread)                          44.31%     8.66us  115.45K
folly_sharedmutex(8thread)                        50.20%     7.65us  130.78K
folly_distributedmutex(8thread)                  120.38%     3.19us  313.64K
folly_distributedmutex_combining(8thread)        190.44%     2.02us  496.18K
folly_flatcombining_no_caching(8thread)          102.17%     3.76us  266.19K
folly_flatcombining_caching(8thread)             129.25%     2.97us  336.76K
----------------------------------------------------------------------------
std_mutex(16thread)                                          9.09us  110.05K
google_spin(16thread)                            110.38%     8.23us  121.47K
folly_microspin(16thread)                         79.81%    11.39us   87.83K
folly_picospin(16thread)                          33.62%    27.03us   37.00K
folly_microlock(16thread)                         49.93%    18.20us   54.95K
folly_sharedmutex(16thread)                       46.15%    19.69us   50.79K
folly_distributedmutex(16thread)                 145.48%     6.25us  160.10K
folly_distributedmutex_combining(16thread)       275.84%     3.29us  303.56K
folly_flatcombining_no_caching(16thread)         151.81%     5.99us  167.06K
folly_flatcombining_caching(16thread)            153.44%     5.92us  168.86K
----------------------------------------------------------------------------
std_mutex(32thread)                                         26.15us   38.24K
google_spin(32thread)                            111.41%    23.47us   42.60K
folly_microspin(32thread)                         84.76%    30.85us   32.41K
folly_picospin(32thread)                          27.30%    95.80us   10.44K
folly_microlock(32thread)                         48.93%    53.45us   18.71K
folly_sharedmutex(32thread)                       54.64%    47.86us   20.89K
folly_distributedmutex(32thread)                 158.31%    16.52us   60.53K
folly_distributedmutex_combining(32thread)       314.13%     8.33us  120.12K
folly_flatcombining_no_caching(32thread)         175.18%    14.93us   66.99K
folly_flatcombining_caching(32thread)            206.73%    12.65us   79.05K
----------------------------------------------------------------------------
std_mutex(64thread)                                         30.72us   32.55K
google_spin(64thread)                            113.69%    27.02us   37.00K
folly_microspin(64thread)                         87.23%    35.22us   28.39K
folly_picospin(64thread)                          27.66%   111.06us    9.00K
folly_microlock(64thread)                         49.93%    61.53us   16.25K
folly_sharedmutex(64thread)                       54.00%    56.89us   17.58K
folly_distributedmutex(64thread)                 162.10%    18.95us   52.77K
folly_distributedmutex_combining(64thread)       317.85%     9.67us  103.46K
folly_flatcombining_no_caching(64thread)         160.43%    19.15us   52.22K
folly_flatcombining_caching(64thread)            185.57%    16.56us   60.40K
----------------------------------------------------------------------------
std_mutex(128thread)                                        72.86us   13.72K
google_spin(128thread)                           114.50%    63.64us   15.71K
folly_microspin(128thread)                        99.89%    72.95us   13.71K
folly_picospin(128thread)                         31.49%   231.40us    4.32K
folly_microlock(128thread)                        57.76%   126.14us    7.93K
folly_sharedmutex(128thread)                      61.49%   118.50us    8.44K
folly_distributedmutex(128thread)                188.86%    38.58us   25.92K
folly_distributedmutex_combining(128thread)      372.60%    19.56us   51.14K
folly_flatcombining_no_caching(128thread)        149.17%    48.85us   20.47K
folly_flatcombining_caching(128thread)           165.93%    43.91us   22.77K
----------------------------------------------------------------------------
std_mutex_simple(1thread)                                  623.35ns    1.60M
google_spin_simple(1thread)                      103.37%   603.04ns    1.66M
folly_microspin_simple(1thread)                  103.18%   604.15ns    1.66M
folly_picospin_simple(1thread)                   103.27%   603.63ns    1.66M
folly_microlock_simple(1thread)                  102.75%   606.68ns    1.65M
folly_sharedmutex_simple(1thread)                 99.03%   629.43ns    1.59M
folly_distributedmutex_simple(1thread)           100.62%   619.52ns    1.61M
folly_distributedmutex_combining_simple(1thread   99.43%   626.92ns    1.60M
folly_flatcombining_no_caching_simple(1thread)    81.20%   767.71ns    1.30M
folly_flatcombining_caching_simple(1thread)       79.80%   781.15ns    1.28M
atomics_fetch_add(1thread)                       100.67%   619.22ns    1.61M
atomic_cas(1thread)                              104.04%   599.13ns    1.67M
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.13us  884.14K
google_spin_simple(2thread)                      119.42%   947.08ns    1.06M
folly_microspin_simple(2thread)                  118.54%   954.12ns    1.05M
folly_picospin_simple(2thread)                   117.00%   966.67ns    1.03M
folly_microlock_simple(2thread)                  114.90%   984.36ns    1.02M
folly_sharedmutex_simple(2thread)                110.79%     1.02us  979.53K
folly_distributedmutex_simple(2thread)           110.43%     1.02us  976.34K
folly_distributedmutex_combining_simple(2thread  105.80%     1.07us  935.43K
folly_flatcombining_no_caching_simple(2thread)    82.28%     1.37us  727.43K
folly_flatcombining_caching_simple(2thread)       89.85%     1.26us  794.41K
atomics_fetch_add(2thread)                       107.37%     1.05us  949.27K
atomic_cas(2thread)                              173.23%   652.92ns    1.53M
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.12us  471.59K
google_spin_simple(4thread)                      101.25%     2.09us  477.50K
folly_microspin_simple(4thread)                   97.79%     2.17us  461.17K
folly_picospin_simple(4thread)                    98.80%     2.15us  465.92K
folly_microlock_simple(4thread)                   79.65%     2.66us  375.61K
folly_sharedmutex_simple(4thread)                 82.35%     2.57us  388.35K
folly_distributedmutex_simple(4thread)           113.43%     1.87us  534.91K
folly_distributedmutex_combining_simple(4thread  158.22%     1.34us  746.17K
folly_flatcombining_no_caching_simple(4thread)    89.95%     2.36us  424.22K
folly_flatcombining_caching_simple(4thread)       98.86%     2.14us  466.24K
atomics_fetch_add(4thread)                       160.21%     1.32us  755.54K
atomic_cas(4thread)                              283.73%   747.35ns    1.34M
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    3.81us  262.49K
google_spin_simple(8thread)                      118.19%     3.22us  310.23K
folly_microspin_simple(8thread)                   87.11%     4.37us  228.66K
folly_picospin_simple(8thread)                    66.31%     5.75us  174.05K
folly_microlock_simple(8thread)                   61.18%     6.23us  160.59K
folly_sharedmutex_simple(8thread)                 61.65%     6.18us  161.82K
folly_distributedmutex_simple(8thread)           116.66%     3.27us  306.22K
folly_distributedmutex_combining_simple(8thread  222.30%     1.71us  583.53K
folly_flatcombining_no_caching_simple(8thread)   105.97%     3.59us  278.17K
folly_flatcombining_caching_simple(8thread)      119.21%     3.20us  312.92K
atomics_fetch_add(8thread)                       248.65%     1.53us  652.70K
atomic_cas(8thread)                              171.55%     2.22us  450.30K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                   9.02us  110.93K
google_spin_simple(16thread)                     115.67%     7.79us  128.31K
folly_microspin_simple(16thread)                  85.45%    10.55us   94.79K
folly_picospin_simple(16thread)                   46.06%    19.57us   51.09K
folly_microlock_simple(16thread)                  53.34%    16.90us   59.17K
folly_sharedmutex_simple(16thread)                47.16%    19.12us   52.31K
folly_distributedmutex_simple(16thread)          131.65%     6.85us  146.03K
folly_distributedmutex_combining_simple(16threa  353.51%     2.55us  392.13K
folly_flatcombining_no_caching_simple(16thread)  175.03%     5.15us  194.16K
folly_flatcombining_caching_simple(16thread)     169.24%     5.33us  187.73K
atomics_fetch_add(16thread)                      428.31%     2.10us  475.10K
atomic_cas(16thread)                             194.29%     4.64us  215.52K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  22.66us   44.12K
google_spin_simple(32thread)                     114.91%    19.72us   50.70K
folly_microspin_simple(32thread)                  70.53%    32.13us   31.12K
folly_picospin_simple(32thread)                   17.21%   131.71us    7.59K
folly_microlock_simple(32thread)                  39.17%    57.86us   17.28K
folly_sharedmutex_simple(32thread)                46.84%    48.39us   20.67K
folly_distributedmutex_simple(32thread)          128.80%    17.60us   56.83K
folly_distributedmutex_combining_simple(32threa  397.59%     5.70us  175.43K
folly_flatcombining_no_caching_simple(32thread)  205.08%    11.05us   90.49K
folly_flatcombining_caching_simple(32thread)     247.48%     9.16us  109.20K
atomics_fetch_add(32thread)                      466.03%     4.86us  205.63K
atomic_cas(32thread)                             439.89%     5.15us  194.10K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  30.55us   32.73K
google_spin_simple(64thread)                     105.69%    28.91us   34.59K
folly_microspin_simple(64thread)                  83.06%    36.79us   27.18K
folly_picospin_simple(64thread)                   20.28%   150.63us    6.64K
folly_microlock_simple(64thread)                  45.10%    67.75us   14.76K
folly_sharedmutex_simple(64thread)                54.07%    56.50us   17.70K
folly_distributedmutex_simple(64thread)          151.84%    20.12us   49.70K
folly_distributedmutex_combining_simple(64threa  465.77%     6.56us  152.45K
folly_flatcombining_no_caching_simple(64thread)  186.46%    16.39us   61.03K
folly_flatcombining_caching_simple(64thread)     250.81%    12.18us   82.09K
atomics_fetch_add(64thread)                      530.59%     5.76us  173.67K
atomic_cas(64thread)                             510.57%     5.98us  167.12K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 69.85us   14.32K
google_spin_simple(128thread)                     97.54%    71.61us   13.97K
folly_microspin_simple(128thread)                 88.01%    79.36us   12.60K
folly_picospin_simple(128thread)                  22.31%   313.13us    3.19K
folly_microlock_simple(128thread)                 50.49%   138.34us    7.23K
folly_sharedmutex_simple(128thread)               59.30%   117.78us    8.49K
folly_distributedmutex_simple(128thread)         174.90%    39.94us   25.04K
folly_distributedmutex_combining_simple(128thre  531.75%    13.14us   76.13K
folly_flatcombining_no_caching_simple(128thread  212.56%    32.86us   30.43K
folly_flatcombining_caching_simple(128thread)    183.68%    38.03us   26.30K
atomics_fetch_add(128thread)                     629.64%    11.09us   90.15K
atomic_cas(128thread)                            562.01%    12.43us   80.46K
============================================================================

./small_locks_benchmark --bm_min_iters=100000
Intel(R) Xeon(R) D-2191 CPU @ 1.60GHz

============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                37.65ns   26.56M
GoogleSpinUncontendedBenchmark                              21.97ns   45.52M
MicroSpinLockUncontendedBenchmark                           21.97ns   45.53M
PicoSpinLockUncontendedBenchmark                            40.80ns   24.51M
MicroLockUncontendedBenchmark                               57.76ns   17.31M
SharedMutexUncontendedBenchmark                             39.55ns   25.29M
DistributedMutexUncontendedBenchmark                        51.47ns   19.43M
AtomicFetchAddUncontendedBenchmark                          10.67ns   93.73M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                           1.36us  737.48K
google_spin(1thread)                              94.81%     1.43us  699.17K
folly_microspin(1thread)                         100.17%     1.35us  738.74K
folly_picospin(1thread)                          100.40%     1.35us  740.41K
folly_microlock(1thread)                          82.90%     1.64us  611.34K
folly_sharedmutex(1thread)                       101.07%     1.34us  745.36K
folly_distributedmutex(1thread)                  101.50%     1.34us  748.54K
folly_distributedmutex_combining(1thread)         99.09%     1.37us  730.79K
folly_flatcombining_no_caching(1thread)           91.37%     1.48us  673.80K
folly_flatcombining_caching(1thread)              99.19%     1.37us  731.48K
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.65us  605.33K
google_spin(2thread)                             113.28%     1.46us  685.74K
folly_microspin(2thread)                         117.23%     1.41us  709.63K
folly_picospin(2thread)                          113.56%     1.45us  687.40K
folly_microlock(2thread)                         106.92%     1.55us  647.22K
folly_sharedmutex(2thread)                       107.24%     1.54us  649.15K
folly_distributedmutex(2thread)                  114.89%     1.44us  695.47K
folly_distributedmutex_combining(2thread)         83.44%     1.98us  505.10K
folly_flatcombining_no_caching(2thread)           75.89%     2.18us  459.42K
folly_flatcombining_caching(2thread)              76.96%     2.15us  465.86K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.88us  347.43K
google_spin(4thread)                             132.08%     2.18us  458.88K
folly_microspin(4thread)                         160.15%     1.80us  556.43K
folly_picospin(4thread)                          189.27%     1.52us  657.60K
folly_microlock(4thread)                         155.13%     1.86us  538.97K
folly_sharedmutex(4thread)                       148.96%     1.93us  517.55K
folly_distributedmutex(4thread)                  106.64%     2.70us  370.51K
folly_distributedmutex_combining(4thread)        138.83%     2.07us  482.33K
folly_flatcombining_no_caching(4thread)           87.67%     3.28us  304.59K
folly_flatcombining_caching(4thread)              93.32%     3.08us  324.23K
----------------------------------------------------------------------------
std_mutex(8thread)                                           7.01us  142.65K
google_spin(8thread)                             127.58%     5.49us  182.00K
folly_microspin(8thread)                         137.50%     5.10us  196.14K
folly_picospin(8thread)                          114.66%     6.11us  163.56K
folly_microlock(8thread)                         107.90%     6.50us  153.92K
folly_sharedmutex(8thread)                       114.21%     6.14us  162.93K
folly_distributedmutex(8thread)                  129.43%     5.42us  184.63K
folly_distributedmutex_combining(8thread)        271.46%     2.58us  387.23K
folly_flatcombining_no_caching(8thread)          148.27%     4.73us  211.50K
folly_flatcombining_caching(8thread)             170.26%     4.12us  242.88K
----------------------------------------------------------------------------
std_mutex(16thread)                                         13.11us   76.30K
google_spin(16thread)                            122.81%    10.67us   93.71K
folly_microspin(16thread)                         91.61%    14.31us   69.90K
folly_picospin(16thread)                          62.60%    20.94us   47.76K
folly_microlock(16thread)                         73.44%    17.85us   56.04K
folly_sharedmutex(16thread)                       74.68%    17.55us   56.98K
folly_distributedmutex(16thread)                 142.42%     9.20us  108.67K
folly_distributedmutex_combining(16thread)       332.10%     3.95us  253.39K
folly_flatcombining_no_caching(16thread)         177.20%     7.40us  135.21K
folly_flatcombining_caching(16thread)            186.60%     7.02us  142.37K
----------------------------------------------------------------------------
std_mutex(32thread)                                         25.45us   39.30K
google_spin(32thread)                            122.57%    20.76us   48.17K
folly_microspin(32thread)                         73.58%    34.58us   28.92K
folly_picospin(32thread)                          50.29%    50.60us   19.76K
folly_microlock(32thread)                         58.33%    43.63us   22.92K
folly_sharedmutex(32thread)                       55.89%    45.53us   21.96K
folly_distributedmutex(32thread)                 142.80%    17.82us   56.12K
folly_distributedmutex_combining(32thread)       352.23%     7.22us  138.42K
folly_flatcombining_no_caching(32thread)         237.42%    10.72us   93.30K
folly_flatcombining_caching(32thread)            251.05%    10.14us   98.66K
----------------------------------------------------------------------------
std_mutex(64thread)                                         43.02us   23.25K
google_spin(64thread)                            120.68%    35.65us   28.05K
folly_microspin(64thread)                         70.09%    61.38us   16.29K
folly_picospin(64thread)                          42.05%   102.31us    9.77K
folly_microlock(64thread)                         54.50%    78.94us   12.67K
folly_sharedmutex(64thread)                       50.37%    85.40us   11.71K
folly_distributedmutex(64thread)                 135.17%    31.83us   31.42K
folly_distributedmutex_combining(64thread)       319.01%    13.49us   74.15K
folly_flatcombining_no_caching(64thread)         218.18%    19.72us   50.72K
folly_flatcombining_caching(64thread)            211.05%    20.38us   49.06K
----------------------------------------------------------------------------
std_mutex(128thread)                                        84.62us   11.82K
google_spin(128thread)                           120.25%    70.37us   14.21K
folly_microspin(128thread)                        66.54%   127.16us    7.86K
folly_picospin(128thread)                         33.40%   253.38us    3.95K
folly_microlock(128thread)                        51.91%   163.03us    6.13K
folly_sharedmutex(128thread)                      49.51%   170.90us    5.85K
folly_distributedmutex(128thread)                131.90%    64.15us   15.59K
folly_distributedmutex_combining(128thread)      273.55%    30.93us   32.33K
folly_flatcombining_no_caching(128thread)        183.86%    46.02us   21.73K
folly_flatcombining_caching(128thread)           180.95%    46.76us   21.38K
----------------------------------------------------------------------------
std_mutex_simple(1thread)                                    1.20us  833.55K
google_spin_simple(1thread)                      105.03%     1.14us  875.52K
folly_microspin_simple(1thread)                  102.64%     1.17us  855.57K
folly_picospin_simple(1thread)                   101.94%     1.18us  849.74K
folly_microlock_simple(1thread)                  101.01%     1.19us  841.96K
folly_sharedmutex_simple(1thread)                100.82%     1.19us  840.37K
folly_distributedmutex_simple(1thread)           100.15%     1.20us  834.83K
folly_distributedmutex_combining_simple(1thread  102.37%     1.17us  853.32K
folly_flatcombining_no_caching_simple(1thread)    93.19%     1.29us  776.81K
folly_flatcombining_caching_simple(1thread)      100.03%     1.20us  833.80K
atomic_fetch_add(1thread)                         98.13%     1.22us  817.99K
atomic_cas(1thread)                              101.95%     1.18us  849.82K
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.56us  641.79K
google_spin_simple(2thread)                      110.31%     1.41us  707.98K
folly_microspin_simple(2thread)                  115.05%     1.35us  738.35K
folly_picospin_simple(2thread)                   110.28%     1.41us  707.78K
folly_microlock_simple(2thread)                  107.14%     1.45us  687.60K
folly_sharedmutex_simple(2thread)                113.16%     1.38us  726.22K
folly_distributedmutex_simple(2thread)           108.31%     1.44us  695.14K
folly_distributedmutex_combining_simple(2thread  104.39%     1.49us  669.95K
folly_flatcombining_no_caching_simple(2thread)    87.04%     1.79us  558.63K
folly_flatcombining_caching_simple(2thread)       97.59%     1.60us  626.30K
atomic_fetch_add(2thread)                        103.06%     1.51us  661.42K
atomic_cas(2thread)                              123.77%     1.26us  794.32K
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.72us  368.29K
google_spin_simple(4thread)                      122.17%     2.22us  449.96K
folly_microspin_simple(4thread)                  142.12%     1.91us  523.43K
folly_picospin_simple(4thread)                   160.27%     1.69us  590.27K
folly_microlock_simple(4thread)                  143.16%     1.90us  527.24K
folly_sharedmutex_simple(4thread)                139.18%     1.95us  512.61K
folly_distributedmutex_simple(4thread)           111.52%     2.43us  410.71K
folly_distributedmutex_combining_simple(4thread  138.74%     1.96us  510.96K
folly_flatcombining_no_caching_simple(4thread)    96.48%     2.81us  355.34K
folly_flatcombining_caching_simple(4thread)      105.15%     2.58us  387.28K
atomic_fetch_add(4thread)                        148.73%     1.83us  547.75K
atomic_cas(4thread)                              213.49%     1.27us  786.28K
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    7.04us  142.04K
google_spin_simple(8thread)                      127.59%     5.52us  181.23K
folly_microspin_simple(8thread)                  135.94%     5.18us  193.09K
folly_picospin_simple(8thread)                   113.86%     6.18us  161.72K
folly_microlock_simple(8thread)                  112.07%     6.28us  159.18K
folly_sharedmutex_simple(8thread)                113.25%     6.22us  160.86K
folly_distributedmutex_simple(8thread)           124.12%     5.67us  176.30K
folly_distributedmutex_combining_simple(8thread  309.01%     2.28us  438.91K
folly_flatcombining_no_caching_simple(8thread)   134.62%     5.23us  191.21K
folly_flatcombining_caching_simple(8thread)      147.13%     4.79us  208.99K
atomic_fetch_add(8thread)                        347.94%     2.02us  494.21K
atomic_cas(8thread)                              412.06%     1.71us  585.28K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                  12.87us   77.73K
google_spin_simple(16thread)                     122.44%    10.51us   95.17K
folly_microspin_simple(16thread)                  99.49%    12.93us   77.33K
folly_picospin_simple(16thread)                   72.60%    17.72us   56.43K
folly_microlock_simple(16thread)                  80.39%    16.00us   62.48K
folly_sharedmutex_simple(16thread)                78.76%    16.34us   61.22K
folly_distributedmutex_simple(16thread)          118.58%    10.85us   92.17K
folly_distributedmutex_combining_simple(16threa  483.44%     2.66us  375.76K
folly_flatcombining_no_caching_simple(16thread)  194.22%     6.62us  150.96K
folly_flatcombining_caching_simple(16thread)     229.03%     5.62us  178.02K
atomic_fetch_add(16thread)                       617.57%     2.08us  480.01K
atomic_cas(16thread)                             258.86%     4.97us  201.20K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  22.85us   43.77K
google_spin_simple(32thread)                     123.96%    18.43us   54.25K
folly_microspin_simple(32thread)                  73.35%    31.15us   32.11K
folly_picospin_simple(32thread)                   46.43%    49.21us   20.32K
folly_microlock_simple(32thread)                  55.62%    41.08us   24.34K
folly_sharedmutex_simple(32thread)                52.67%    43.38us   23.05K
folly_distributedmutex_simple(32thread)          106.87%    21.38us   46.78K
folly_distributedmutex_combining_simple(32threa  581.80%     3.93us  254.64K
folly_flatcombining_no_caching_simple(32thread)  280.19%     8.15us  122.63K
folly_flatcombining_caching_simple(32thread)     350.87%     6.51us  153.57K
atomic_fetch_add(32thread)                      1031.35%     2.22us  451.41K
atomic_cas(32thread)                             209.10%    10.93us   91.52K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  39.55us   25.28K
google_spin_simple(64thread)                     124.15%    31.86us   31.39K
folly_microspin_simple(64thread)                  72.27%    54.73us   18.27K
folly_picospin_simple(64thread)                   39.96%    98.98us   10.10K
folly_microlock_simple(64thread)                  53.10%    74.48us   13.43K
folly_sharedmutex_simple(64thread)                48.83%    81.00us   12.35K
folly_distributedmutex_simple(64thread)          103.91%    38.06us   26.27K
folly_distributedmutex_combining_simple(64threa  520.61%     7.60us  131.63K
folly_flatcombining_no_caching_simple(64thread)  288.46%    13.71us   72.93K
folly_flatcombining_caching_simple(64thread)     306.57%    12.90us   77.51K
atomic_fetch_add(64thread)                       982.24%     4.03us  248.34K
atomic_cas(64thread)                             191.87%    20.61us   48.51K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 77.79us   12.85K
google_spin_simple(128thread)                    123.39%    63.05us   15.86K
folly_microspin_simple(128thread)                 69.13%   112.53us    8.89K
folly_picospin_simple(128thread)                  30.32%   256.57us    3.90K
folly_microlock_simple(128thread)                 50.78%   153.20us    6.53K
folly_sharedmutex_simple(128thread)               48.00%   162.07us    6.17K
folly_distributedmutex_simple(128thread)         102.79%    75.68us   13.21K
folly_distributedmutex_combining_simple(128thre  433.00%    17.97us   55.66K
folly_flatcombining_no_caching_simple(128thread  186.46%    41.72us   23.97K
folly_flatcombining_caching_simple(128thread)    204.22%    38.09us   26.25K
atomic_fetch_add(128thread)                      965.10%     8.06us  124.06K
atomic_cas(128thread)                            184.01%    42.28us   23.65K
============================================================================
*/
