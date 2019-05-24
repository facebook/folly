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
  folly::makeUnpredictable(sum);
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
  auto value = atomic.integer_.load(std::memory_order_relaxed);
  folly::makeUnpredictable(value);
  while (!atomic.integer_.compare_exchange_strong(value, value + 1)) {
  }
  return value;
}
void initialize(AtomicCas&) {}

class alignas(folly::hardware_destructive_interference_size) AtomicFetchXor {
 public:
  std::atomic<std::uint64_t> integer_{0};
};
std::uint64_t write(AtomicFetchXor& atomic) {
  auto value = std::numeric_limits<std::uint64_t>::max();
  folly::makeUnpredictable(value);

  // XOR is a good choice here because it allows us to simulate random
  // operation in the hardware.  For example, if we were to use the same value
  // to do something like a bitwise or, the hardware is allowed to coalesce
  // the operations into one by treating all of the ones after the first as
  // idempotent, and then it only needs to transfer data across the bus
  // without actually needing to move the cacheline to the remote cores.  Very
  // much like the implementation here, but done in the hardware.  Coalescing
  // XORs is hard because it requires knowledge of the previous state and is
  // mutating on every operation
  value = atomic.integer_.fetch_xor(value, std::memory_order_acq_rel);
  return value;
}
void initialize(AtomicFetchXor&) {}

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
static void atomic_fetch_xor(size_t numOps, size_t numThreads) {
  runContended<NoLock, AtomicFetchXor>(numOps, numThreads, 0);
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
BENCH_REL(atomic_fetch_xor, 1thread, 1)
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
BENCH_REL(atomic_fetch_xor, 2thread, 2)
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
BENCH_REL(atomic_fetch_xor, 4thread, 4)
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
BENCH_REL(atomic_fetch_xor, 8thread, 8)
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
BENCH_REL(atomic_fetch_xor, 16thread, 16)
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
BENCH_REL(atomic_fetch_xor, 32thread, 32)
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
BENCH_REL(atomic_fetch_xor, 64thread, 64)
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
BENCH_REL(atomic_fetch_xor, 128thread, 128)
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
StdMutexUncontendedBenchmark                                16.40ns   60.98M
GoogleSpinUncontendedBenchmark                              11.23ns   89.02M
MicroSpinLockUncontendedBenchmark                           10.94ns   91.45M
PicoSpinLockUncontendedBenchmark                            20.37ns   49.08M
MicroLockUncontendedBenchmark                               29.21ns   34.24M
SharedMutexUncontendedBenchmark                             19.44ns   51.45M
DistributedMutexUncontendedBenchmark                        29.49ns   33.91M
AtomicFetchAddUncontendedBenchmark                           5.45ns  183.56M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         706.81ns    1.41M
google_spin(1thread)                             103.09%   685.63ns    1.46M
folly_microspin(1thread)                         117.03%   603.96ns    1.66M
folly_picospin(1thread)                          102.72%   688.12ns    1.45M
folly_microlock(1thread)                         103.40%   683.59ns    1.46M
folly_sharedmutex(1thread)                       103.64%   682.01ns    1.47M
folly_distributedmutex(1thread)                  101.07%   699.32ns    1.43M
folly_distributedmutex_combining(1thread)        102.75%   687.89ns    1.45M
folly_flatcombining_no_caching(1thread)           94.78%   745.77ns    1.34M
folly_flatcombining_caching(1thread)             100.95%   700.15ns    1.43M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.28us  779.95K
google_spin(2thread)                             137.96%   929.38ns    1.08M
folly_microspin(2thread)                         151.64%   845.52ns    1.18M
folly_picospin(2thread)                          140.81%   910.52ns    1.10M
folly_microlock(2thread)                         131.62%   974.11ns    1.03M
folly_sharedmutex(2thread)                       143.97%   890.53ns    1.12M
folly_distributedmutex(2thread)                  129.20%   992.39ns    1.01M
folly_distributedmutex_combining(2thread)        131.27%   976.71ns    1.02M
folly_flatcombining_no_caching(2thread)           93.85%     1.37us  732.01K
folly_flatcombining_caching(2thread)              97.05%     1.32us  756.98K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.65us  376.96K
google_spin(4thread)                             125.03%     2.12us  471.33K
folly_microspin(4thread)                         118.43%     2.24us  446.44K
folly_picospin(4thread)                          122.04%     2.17us  460.05K
folly_microlock(4thread)                         102.38%     2.59us  385.94K
folly_sharedmutex(4thread)                       101.76%     2.61us  383.60K
folly_distributedmutex(4thread)                  137.07%     1.94us  516.71K
folly_distributedmutex_combining(4thread)        191.98%     1.38us  723.71K
folly_flatcombining_no_caching(4thread)          106.91%     2.48us  403.02K
folly_flatcombining_caching(4thread)             111.66%     2.38us  420.91K
----------------------------------------------------------------------------
std_mutex(8thread)                                           5.21us  191.97K
google_spin(8thread)                             102.12%     5.10us  196.05K
folly_microspin(8thread)                          97.02%     5.37us  186.26K
folly_picospin(8thread)                           83.62%     6.23us  160.53K
folly_microlock(8thread)                          69.32%     7.51us  133.08K
folly_sharedmutex(8thread)                        64.22%     8.11us  123.29K
folly_distributedmutex(8thread)                  175.50%     2.97us  336.91K
folly_distributedmutex_combining(8thread)        258.13%     2.02us  495.55K
folly_flatcombining_no_caching(8thread)          137.21%     3.80us  263.41K
folly_flatcombining_caching(8thread)             174.75%     2.98us  335.48K
----------------------------------------------------------------------------
std_mutex(16thread)                                         10.06us   99.37K
google_spin(16thread)                             97.24%    10.35us   96.63K
folly_microspin(16thread)                         91.23%    11.03us   90.65K
folly_picospin(16thread)                          58.31%    17.26us   57.94K
folly_microlock(16thread)                         51.59%    19.51us   51.26K
folly_sharedmutex(16thread)                       49.87%    20.18us   49.56K
folly_distributedmutex(16thread)                 155.47%     6.47us  154.49K
folly_distributedmutex_combining(16thread)       316.70%     3.18us  314.70K
folly_flatcombining_no_caching(16thread)         198.94%     5.06us  197.68K
folly_flatcombining_caching(16thread)            184.72%     5.45us  183.55K
----------------------------------------------------------------------------
std_mutex(32thread)                                         33.80us   29.59K
google_spin(32thread)                            109.19%    30.95us   32.31K
folly_microspin(32thread)                        110.23%    30.66us   32.62K
folly_picospin(32thread)                          39.94%    84.62us   11.82K
folly_microlock(32thread)                         56.56%    59.75us   16.74K
folly_sharedmutex(32thread)                       73.92%    45.72us   21.87K
folly_distributedmutex(32thread)                 192.60%    17.55us   56.99K
folly_distributedmutex_combining(32thread)       402.79%     8.39us  119.19K
folly_flatcombining_no_caching(32thread)         235.30%    14.36us   69.63K
folly_flatcombining_caching(32thread)            259.02%    13.05us   76.64K
----------------------------------------------------------------------------
std_mutex(64thread)                                         38.86us   25.73K
google_spin(64thread)                            109.06%    35.63us   28.06K
folly_microspin(64thread)                        109.92%    35.36us   28.28K
folly_picospin(64thread)                          37.02%   104.99us    9.53K
folly_microlock(64thread)                         56.33%    68.99us   14.49K
folly_sharedmutex(64thread)                       69.39%    56.00us   17.86K
folly_distributedmutex(64thread)                 194.31%    20.00us   50.00K
folly_distributedmutex_combining(64thread)       397.54%     9.78us  102.29K
folly_flatcombining_no_caching(64thread)         230.64%    16.85us   59.35K
folly_flatcombining_caching(64thread)            254.03%    15.30us   65.37K
----------------------------------------------------------------------------
std_mutex(128thread)                                        76.62us   13.05K
google_spin(128thread)                           109.31%    70.09us   14.27K
folly_microspin(128thread)                       102.86%    74.49us   13.43K
folly_picospin(128thread)                         42.23%   181.42us    5.51K
folly_microlock(128thread)                        55.01%   139.29us    7.18K
folly_sharedmutex(128thread)                      63.50%   120.65us    8.29K
folly_distributedmutex(128thread)                183.63%    41.72us   23.97K
folly_distributedmutex_combining(128thread)      388.41%    19.73us   50.69K
folly_flatcombining_no_caching(128thread)        183.56%    41.74us   23.96K
folly_flatcombining_caching(128thread)           198.02%    38.69us   25.84K
----------------------------------------------------------------------------
std_mutex_simple(1thread)                                  634.77ns    1.58M
google_spin_simple(1thread)                      104.06%   610.01ns    1.64M
folly_microspin_simple(1thread)                  104.59%   606.89ns    1.65M
folly_picospin_simple(1thread)                    99.37%   638.81ns    1.57M
folly_microlock_simple(1thread)                  104.08%   609.86ns    1.64M
folly_sharedmutex_simple(1thread)                 91.77%   691.73ns    1.45M
folly_distributedmutex_simple(1thread)            98.10%   647.04ns    1.55M
folly_distributedmutex_combining_simple(1thread  101.90%   622.93ns    1.61M
folly_flatcombining_no_caching_simple(1thread)    93.71%   677.40ns    1.48M
folly_flatcombining_caching_simple(1thread)      101.81%   623.46ns    1.60M
atomics_fetch_add(1thread)                       102.23%   620.90ns    1.61M
atomic_fetch_xor(1thread)                        104.67%   606.43ns    1.65M
atomic_cas(1thread)                               84.68%   749.58ns    1.33M
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.24us  803.81K
google_spin_simple(2thread)                      123.09%     1.01us  989.38K
folly_microspin_simple(2thread)                  138.46%   898.48ns    1.11M
folly_picospin_simple(2thread)                   121.05%     1.03us  973.01K
folly_microlock_simple(2thread)                  112.54%     1.11us  904.60K
folly_sharedmutex_simple(2thread)                112.16%     1.11us  901.60K
folly_distributedmutex_simple(2thread)           119.86%     1.04us  963.47K
folly_distributedmutex_combining_simple(2thread  130.78%   951.25ns    1.05M
folly_flatcombining_no_caching_simple(2thread)    93.25%     1.33us  749.54K
folly_flatcombining_caching_simple(2thread)      102.34%     1.22us  822.65K
atomics_fetch_add(2thread)                       113.81%     1.09us  914.83K
atomic_fetch_xor(2thread)                        161.97%   768.09ns    1.30M
atomic_cas(2thread)                              150.00%   829.41ns    1.21M
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.39us  418.75K
google_spin_simple(4thread)                      109.55%     2.18us  458.74K
folly_microspin_simple(4thread)                  110.15%     2.17us  461.26K
folly_picospin_simple(4thread)                   115.62%     2.07us  484.17K
folly_microlock_simple(4thread)                   88.54%     2.70us  370.77K
folly_sharedmutex_simple(4thread)                100.50%     2.38us  420.86K
folly_distributedmutex_simple(4thread)           114.93%     2.08us  481.26K
folly_distributedmutex_combining_simple(4thread  161.11%     1.48us  674.64K
folly_flatcombining_no_caching_simple(4thread)   106.27%     2.25us  445.02K
folly_flatcombining_caching_simple(4thread)      113.01%     2.11us  473.23K
atomics_fetch_add(4thread)                       156.29%     1.53us  654.48K
atomic_fetch_xor(4thread)                        285.69%   835.89ns    1.20M
atomic_cas(4thread)                              270.31%   883.45ns    1.13M
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    4.83us  207.09K
google_spin_simple(8thread)                      117.15%     4.12us  242.60K
folly_microspin_simple(8thread)                  106.41%     4.54us  220.37K
folly_picospin_simple(8thread)                    88.31%     5.47us  182.88K
folly_microlock_simple(8thread)                   77.90%     6.20us  161.33K
folly_sharedmutex_simple(8thread)                 72.21%     6.69us  149.55K
folly_distributedmutex_simple(8thread)           138.98%     3.47us  287.83K
folly_distributedmutex_combining_simple(8thread  289.79%     1.67us  600.12K
folly_flatcombining_no_caching_simple(8thread)   134.25%     3.60us  278.03K
folly_flatcombining_caching_simple(8thread)      149.74%     3.22us  310.10K
atomics_fetch_add(8thread)                       318.11%     1.52us  658.78K
atomic_fetch_xor(8thread)                        373.98%     1.29us  774.47K
atomic_cas(8thread)                              241.00%     2.00us  499.09K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                  12.03us   83.13K
google_spin_simple(16thread)                      98.34%    12.23us   81.75K
folly_microspin_simple(16thread)                 115.19%    10.44us   95.76K
folly_picospin_simple(16thread)                   54.50%    22.07us   45.31K
folly_microlock_simple(16thread)                  58.38%    20.61us   48.53K
folly_sharedmutex_simple(16thread)                69.90%    17.21us   58.11K
folly_distributedmutex_simple(16thread)          155.15%     7.75us  128.97K
folly_distributedmutex_combining_simple(16threa  463.66%     2.59us  385.43K
folly_flatcombining_no_caching_simple(16thread)  279.15%     4.31us  232.05K
folly_flatcombining_caching_simple(16thread)     207.72%     5.79us  172.67K
atomics_fetch_add(16thread)                      538.64%     2.23us  447.76K
atomic_fetch_xor(16thread)                       570.85%     2.11us  474.53K
atomic_cas(16thread)                             334.73%     3.59us  278.25K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  30.92us   32.34K
google_spin_simple(32thread)                     107.22%    28.84us   34.68K
folly_microspin_simple(32thread)                 106.48%    29.04us   34.44K
folly_picospin_simple(32thread)                   32.90%    93.97us   10.64K
folly_microlock_simple(32thread)                  55.77%    55.44us   18.04K
folly_sharedmutex_simple(32thread)                63.85%    48.42us   20.65K
folly_distributedmutex_simple(32thread)          170.50%    18.13us   55.14K
folly_distributedmutex_combining_simple(32threa  562.55%     5.50us  181.94K
folly_flatcombining_no_caching_simple(32thread)  296.57%    10.43us   95.92K
folly_flatcombining_caching_simple(32thread)     295.25%    10.47us   95.49K
atomics_fetch_add(32thread)                      952.20%     3.25us  307.96K
atomic_fetch_xor(32thread)                       818.15%     3.78us  264.61K
atomic_cas(32thread)                             634.91%     4.87us  205.34K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  35.29us   28.33K
google_spin_simple(64thread)                     107.33%    32.88us   30.41K
folly_microspin_simple(64thread)                 106.02%    33.29us   30.04K
folly_picospin_simple(64thread)                   32.93%   107.17us    9.33K
folly_microlock_simple(64thread)                  54.76%    64.45us   15.52K
folly_sharedmutex_simple(64thread)                63.74%    55.37us   18.06K
folly_distributedmutex_simple(64thread)          170.45%    20.71us   48.30K
folly_distributedmutex_combining_simple(64threa  558.99%     6.31us  158.38K
folly_flatcombining_no_caching_simple(64thread)  311.86%    11.32us   88.36K
folly_flatcombining_caching_simple(64thread)     327.64%    10.77us   92.83K
atomics_fetch_add(64thread)                      858.61%     4.11us  243.28K
atomic_fetch_xor(64thread)                       738.35%     4.78us  209.20K
atomic_cas(64thread)                             623.72%     5.66us  176.72K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 69.21us   14.45K
google_spin_simple(128thread)                    107.42%    64.43us   15.52K
folly_microspin_simple(128thread)                 96.36%    71.82us   13.92K
folly_picospin_simple(128thread)                  31.07%   222.75us    4.49K
folly_microlock_simple(128thread)                 53.97%   128.25us    7.80K
folly_sharedmutex_simple(128thread)               60.56%   114.29us    8.75K
folly_distributedmutex_simple(128thread)         165.16%    41.91us   23.86K
folly_distributedmutex_combining_simple(128thre  542.63%    12.75us   78.40K
folly_flatcombining_no_caching_simple(128thread  246.16%    28.12us   35.57K
folly_flatcombining_caching_simple(128thread)    232.56%    29.76us   33.60K
atomics_fetch_add(128thread)                     839.43%     8.24us  121.29K
atomic_fetch_xor(128thread)                      761.39%     9.09us  110.01K
atomic_cas(128thread)                            598.53%    11.56us   86.48K
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
std_mutex_simple(1thread)                                    1.19us  839.60K
google_spin_simple(1thread)                      100.96%     1.18us  847.68K
folly_microspin_simple(1thread)                  101.35%     1.18us  850.96K
folly_picospin_simple(1thread)                   101.04%     1.18us  848.31K
folly_microlock_simple(1thread)                  100.58%     1.18us  844.50K
folly_sharedmutex_simple(1thread)                100.75%     1.18us  845.88K
folly_distributedmutex_simple(1thread)            98.62%     1.21us  828.05K
folly_distributedmutex_combining_simple(1thread   99.58%     1.20us  836.07K
folly_flatcombining_no_caching_simple(1thread)    95.63%     1.25us  802.87K
folly_flatcombining_caching_simple(1thread)       99.37%     1.20us  834.27K
atomics_fetch_add(1thread)                       101.98%     1.17us  856.25K
atomic_fetch_xor(1thread)                        101.29%     1.18us  850.43K
atomic_cas(1thread)                              101.73%     1.17us  854.11K
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.60us  623.66K
google_spin_simple(2thread)                      113.06%     1.42us  705.12K
folly_microspin_simple(2thread)                  114.38%     1.40us  713.32K
folly_picospin_simple(2thread)                   112.84%     1.42us  703.74K
folly_microlock_simple(2thread)                   97.27%     1.65us  606.66K
folly_sharedmutex_simple(2thread)                111.31%     1.44us  694.20K
folly_distributedmutex_simple(2thread)           109.21%     1.47us  681.11K
folly_distributedmutex_combining_simple(2thread  107.91%     1.49us  672.98K
folly_flatcombining_no_caching_simple(2thread)    89.48%     1.79us  558.04K
folly_flatcombining_caching_simple(2thread)       98.95%     1.62us  617.14K
atomics_fetch_add(2thread)                       106.88%     1.50us  666.58K
atomic_fetch_xor(2thread)                        126.82%     1.26us  790.91K
atomic_cas(2thread)                              130.34%     1.23us  812.86K
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.74us  364.72K
google_spin_simple(4thread)                      123.43%     2.22us  450.16K
folly_microspin_simple(4thread)                  153.56%     1.79us  560.07K
folly_picospin_simple(4thread)                   146.03%     1.88us  532.59K
folly_microlock_simple(4thread)                  116.28%     2.36us  424.10K
folly_sharedmutex_simple(4thread)                142.39%     1.93us  519.33K
folly_distributedmutex_simple(4thread)           111.84%     2.45us  407.89K
folly_distributedmutex_combining_simple(4thread  140.61%     1.95us  512.83K
folly_flatcombining_no_caching_simple(4thread)   101.22%     2.71us  369.17K
folly_flatcombining_caching_simple(4thread)      105.38%     2.60us  384.35K
atomics_fetch_add(4thread)                       150.95%     1.82us  550.52K
atomic_fetch_xor(4thread)                        223.43%     1.23us  814.87K
atomic_cas(4thread)                              217.57%     1.26us  793.52K
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    6.99us  142.98K
google_spin_simple(8thread)                      128.58%     5.44us  183.84K
folly_microspin_simple(8thread)                  131.98%     5.30us  188.69K
folly_picospin_simple(8thread)                   121.81%     5.74us  174.16K
folly_microlock_simple(8thread)                  100.06%     6.99us  143.06K
folly_sharedmutex_simple(8thread)                115.88%     6.04us  165.69K
folly_distributedmutex_simple(8thread)           123.11%     5.68us  176.02K
folly_distributedmutex_combining_simple(8thread  307.74%     2.27us  439.99K
folly_flatcombining_no_caching_simple(8thread)   136.00%     5.14us  194.45K
folly_flatcombining_caching_simple(8thread)      148.43%     4.71us  212.22K
atomics_fetch_add(8thread)                       358.67%     1.95us  512.81K
atomic_fetch_xor(8thread)                        466.73%     1.50us  667.32K
atomic_cas(8thread)                              371.61%     1.88us  531.31K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                  12.83us   77.96K
google_spin_simple(16thread)                     122.19%    10.50us   95.26K
folly_microspin_simple(16thread)                  99.14%    12.94us   77.30K
folly_picospin_simple(16thread)                   62.74%    20.44us   48.91K
folly_microlock_simple(16thread)                  75.01%    17.10us   58.48K
folly_sharedmutex_simple(16thread)                79.92%    16.05us   62.31K
folly_distributedmutex_simple(16thread)          118.18%    10.85us   92.14K
folly_distributedmutex_combining_simple(16threa  482.27%     2.66us  376.00K
folly_flatcombining_no_caching_simple(16thread)  191.45%     6.70us  149.26K
folly_flatcombining_caching_simple(16thread)     227.12%     5.65us  177.07K
atomics_fetch_add(16thread)                      612.80%     2.09us  477.77K
atomic_fetch_xor(16thread)                       551.00%     2.33us  429.58K
atomic_cas(16thread)                             282.79%     4.54us  220.47K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  23.09us   43.30K
google_spin_simple(32thread)                     125.07%    18.46us   54.16K
folly_microspin_simple(32thread)                  76.39%    30.23us   33.08K
folly_picospin_simple(32thread)                   46.54%    49.62us   20.16K
folly_microlock_simple(32thread)                  52.84%    43.71us   22.88K
folly_sharedmutex_simple(32thread)                53.06%    43.52us   22.98K
folly_distributedmutex_simple(32thread)          107.10%    21.56us   46.38K
folly_distributedmutex_combining_simple(32threa  596.57%     3.87us  258.33K
folly_flatcombining_no_caching_simple(32thread)  274.44%     8.41us  118.84K
folly_flatcombining_caching_simple(32thread)     312.83%     7.38us  135.46K
atomics_fetch_add(32thread)                     1082.13%     2.13us  468.59K
atomic_fetch_xor(32thread)                       552.82%     4.18us  239.39K
atomic_cas(32thread)                             203.03%    11.37us   87.92K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  39.95us   25.03K
google_spin_simple(64thread)                     124.75%    32.02us   31.23K
folly_microspin_simple(64thread)                  73.49%    54.36us   18.40K
folly_picospin_simple(64thread)                   39.80%   100.37us    9.96K
folly_microlock_simple(64thread)                  50.07%    79.78us   12.53K
folly_sharedmutex_simple(64thread)                49.52%    80.66us   12.40K
folly_distributedmutex_simple(64thread)          104.56%    38.20us   26.18K
folly_distributedmutex_combining_simple(64threa  532.34%     7.50us  133.26K
folly_flatcombining_no_caching_simple(64thread)  279.23%    14.31us   69.90K
folly_flatcombining_caching_simple(64thread)     325.10%    12.29us   81.39K
atomics_fetch_add(64thread)                     1031.51%     3.87us  258.23K
atomic_fetch_xor(64thread)                       525.68%     7.60us  131.60K
atomic_cas(64thread)                             187.67%    21.28us   46.98K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 78.65us   12.71K
google_spin_simple(128thread)                    124.05%    63.40us   15.77K
folly_microspin_simple(128thread)                 70.00%   112.36us    8.90K
folly_picospin_simple(128thread)                  29.72%   264.60us    3.78K
folly_microlock_simple(128thread)                 47.74%   164.73us    6.07K
folly_sharedmutex_simple(128thread)               48.87%   160.93us    6.21K
folly_distributedmutex_simple(128thread)         104.04%    75.59us   13.23K
folly_distributedmutex_combining_simple(128thre  426.02%    18.46us   54.17K
folly_flatcombining_no_caching_simple(128thread  210.85%    37.30us   26.81K
folly_flatcombining_caching_simple(128thread)    241.48%    32.57us   30.70K
atomics_fetch_add(128thread)                     992.30%     7.93us  126.17K
atomic_fetch_xor(128thread)                      525.32%    14.97us   66.79K
atomic_cas(128thread)                            181.89%    43.24us   23.13K
============================================================================
*/
