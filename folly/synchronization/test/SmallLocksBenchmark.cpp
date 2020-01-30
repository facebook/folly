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
#include <folly/SharedMutex.h>
#include <folly/experimental/flat_combining/FlatCombining.h>
#include <folly/lang/Aligned.h>
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
  std::array<folly::cacheline_aligned<std::uint64_t>, 5> ints_;
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
  std::array<folly::cacheline_aligned<std::atomic<std::uint64_t>>, 5> ints_;
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
        auto val = lock_and(
            mutex->mutex, t, [& value = mutex->value, work ]() noexcept {
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
        lock_and(mutex->lock, t, [&]() {
          burn(FLAGS_work);
          value++;
        });
        std::chrono::steady_clock::time_point postlock =
            std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(
            postlock - prelock);
        time += diff;
        timeSq += diff.count() * diff.count();
        if (diff > max) {
          max = diff;
        }
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
    auto lck = std::unique_lock<Mutex>{mutex};
    folly::makeUnpredictable(mutex);
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
      fairnessTest<DistributedMutexFlatCombining>(
          "folly::DistributedMutex (Combining)", numThreads);

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
Sum: 361854376 Mean: 6461685 stddev: 770837
Lock time stats in us: mean 0 stddev 1 max 63002
------- GoogleSpinLock 2 threads
Sum: 463530598 Mean: 8277332 stddev: 759139
Lock time stats in us: mean 0 stddev 9 max 44995
------- folly::MicroSpinLock 2 threads
Sum: 454928254 Mean: 8123718 stddev: 1568978
Lock time stats in us: mean 0 stddev 9 max 118006
------- folly::PicoSpinLock<std::uint16_t> 2 threads
Sum: 376990850 Mean: 6731979 stddev: 1295859
Lock time stats in us: mean 0 stddev 1 max 83007
------- folly::MicroLock 2 threads
Sum: 316081944 Mean: 5644320 stddev: 1249068
Lock time stats in us: mean 0 stddev 13 max 53930
------- folly::SharedMutex 2 threads
Sum: 389298695 Mean: 6951762 stddev: 3031794
Lock time stats in us: mean 0 stddev 2 max 55004
------- folly::DistributedMutex 2 threads
Sum: 512343772 Mean: 9148995 stddev: 1168346
Lock time stats in us: mean 0 stddev 8 max 50830
------- folly::DistributedMutex (Combining) 2 threads
Sum: 475079423 Mean: 8483561 stddev: 899288
Lock time stats in us: mean 0 stddev 1 max 26006
============================================================================
------- std::mutex 4 threads
Sum: 164126417 Mean: 2930828 stddev: 208327
Lock time stats in us: mean 0 stddev 2 max 11759
------- GoogleSpinLock 4 threads
Sum: 200210044 Mean: 3575179 stddev: 472142
Lock time stats in us: mean 0 stddev 21 max 16715
------- folly::MicroSpinLock 4 threads
Sum: 168795789 Mean: 3014210 stddev: 825455
Lock time stats in us: mean 0 stddev 3 max 152163
------- folly::PicoSpinLock<std::uint16_t> 4 threads
Sum: 125788231 Mean: 2246218 stddev: 755074
Lock time stats in us: mean 1 stddev 3 max 151004
------- folly::MicroLock 4 threads
Sum: 109091138 Mean: 1948056 stddev: 465388
Lock time stats in us: mean 1 stddev 39 max 60029
------- folly::SharedMutex 4 threads
Sum: 107870343 Mean: 1926256 stddev: 1039541
Lock time stats in us: mean 1 stddev 2 max 57002
------- folly::DistributedMutex 4 threads
Sum: 207229191 Mean: 3700521 stddev: 182811
Lock time stats in us: mean 0 stddev 21 max 16231
------- folly::DistributedMutex (Combining) 4 threads
Sum: 204144735 Mean: 3645441 stddev: 619224
Lock time stats in us: mean 0 stddev 0 max 27008
============================================================================
------- std::mutex 8 threads
Sum: 82709846 Mean: 1476961 stddev: 173483
Lock time stats in us: mean 2 stddev 52 max 9404
------- GoogleSpinLock 8 threads
Sum: 98373671 Mean: 1756672 stddev: 65326
Lock time stats in us: mean 1 stddev 43 max 20805
------- folly::MicroSpinLock 8 threads
Sum: 94805197 Mean: 1692949 stddev: 633249
Lock time stats in us: mean 1 stddev 3 max 104517
------- folly::PicoSpinLock<std::uint16_t> 8 threads
Sum: 41587796 Mean: 742639 stddev: 191868
Lock time stats in us: mean 4 stddev 103 max 317025
------- folly::MicroLock 8 threads
Sum: 42414128 Mean: 757395 stddev: 234934
Lock time stats in us: mean 4 stddev 101 max 39660
------- folly::SharedMutex 8 threads
Sum: 58861445 Mean: 1051097 stddev: 491231
Lock time stats in us: mean 3 stddev 73 max 34007
------- folly::DistributedMutex 8 threads
Sum: 93377108 Mean: 1667448 stddev: 113502
Lock time stats in us: mean 1 stddev 46 max 11075
------- folly::DistributedMutex (Combining) 8 threads
Sum: 131093487 Mean: 2340955 stddev: 187841
Lock time stats in us: mean 1 stddev 3 max 25004
============================================================================
------- std::mutex 16 threads
Sum: 36606221 Mean: 653682 stddev: 65154
Lock time stats in us: mean 5 stddev 117 max 13603
------- GoogleSpinLock 16 threads
Sum: 29830088 Mean: 532680 stddev: 19614
Lock time stats in us: mean 7 stddev 2 max 10338
------- folly::MicroSpinLock 16 threads
Sum: 27935153 Mean: 498842 stddev: 197304
Lock time stats in us: mean 7 stddev 3 max 257433
------- folly::PicoSpinLock<std::uint16_t> 16 threads
Sum: 12265416 Mean: 219025 stddev: 146399
Lock time stats in us: mean 17 stddev 350 max 471793
------- folly::MicroLock 16 threads
Sum: 18180611 Mean: 324653 stddev: 32123
Lock time stats in us: mean 11 stddev 236 max 40166
------- folly::SharedMutex 16 threads
Sum: 21734734 Mean: 388120 stddev: 190252
Lock time stats in us: mean 9 stddev 197 max 107045
------- folly::DistributedMutex 16 threads
Sum: 42823745 Mean: 764709 stddev: 64251
Lock time stats in us: mean 4 stddev 100 max 19986
------- folly::DistributedMutex (Combining) 16 threads
Sum: 63515255 Mean: 1134200 stddev: 37905
Lock time stats in us: mean 2 stddev 3 max 32005
============================================================================
------- std::mutex 32 threads
Sum: 10307832 Mean: 184068 stddev: 2431
Lock time stats in us: mean 21 stddev 416 max 18397
------- GoogleSpinLock 32 threads
Sum: 10911809 Mean: 194853 stddev: 2968
Lock time stats in us: mean 20 stddev 393 max 10765
------- folly::MicroSpinLock 32 threads
Sum: 7318139 Mean: 130681 stddev: 24742
Lock time stats in us: mean 29 stddev 586 max 230672
------- folly::PicoSpinLock<std::uint16_t> 32 threads
Sum: 6424015 Mean: 114714 stddev: 138460
Lock time stats in us: mean 34 stddev 668 max 879632
------- folly::MicroLock 32 threads
Sum: 4893744 Mean: 87388 stddev: 6935
Lock time stats in us: mean 45 stddev 876 max 14902
------- folly::SharedMutex 32 threads
Sum: 6393363 Mean: 114167 stddev: 80211
Lock time stats in us: mean 34 stddev 671 max 75777
------- folly::DistributedMutex 32 threads
Sum: 14394775 Mean: 257049 stddev: 36723
Lock time stats in us: mean 15 stddev 298 max 54654
------- folly::DistributedMutex (Combining) 32 threads
Sum: 24232845 Mean: 432729 stddev: 11398
Lock time stats in us: mean 8 stddev 177 max 35008
============================================================================
------- std::mutex 64 threads
Sum: 10656640 Mean: 166510 stddev: 3340
Lock time stats in us: mean 23 stddev 402 max 10797
------- GoogleSpinLock 64 threads
Sum: 11263029 Mean: 175984 stddev: 4669
Lock time stats in us: mean 22 stddev 381 max 26844
------- folly::MicroSpinLock 64 threads
Sum: 23284721 Mean: 363823 stddev: 62670
Lock time stats in us: mean 10 stddev 184 max 168470
------- folly::PicoSpinLock<std::uint16_t> 64 threads
Sum: 2322545 Mean: 36289 stddev: 6272
Lock time stats in us: mean 109 stddev 1846 max 1157157
------- folly::MicroLock 64 threads
Sum: 4835136 Mean: 75549 stddev: 3484
Lock time stats in us: mean 52 stddev 887 max 23895
------- folly::SharedMutex 64 threads
Sum: 7047147 Mean: 110111 stddev: 53207
Lock time stats in us: mean 35 stddev 608 max 85181
------- folly::DistributedMutex 64 threads
Sum: 14491662 Mean: 226432 stddev: 27098
Lock time stats in us: mean 17 stddev 296 max 55078
------- folly::DistributedMutex (Combining) 64 threads
Sum: 23885026 Mean: 373203 stddev: 14431
Lock time stats in us: mean 10 stddev 179 max 62008
============================================================================
============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                16.42ns   60.90M
GoogleSpinUncontendedBenchmark                              11.25ns   88.86M
MicroSpinLockUncontendedBenchmark                           10.95ns   91.33M
PicoSpinLockUncontendedBenchmark                            20.38ns   49.07M
MicroLockUncontendedBenchmark                               28.92ns   34.58M
SharedMutexUncontendedBenchmark                             19.47ns   51.36M
DistributedMutexUncontendedBenchmark                        28.89ns   34.62M
AtomicFetchAddUncontendedBenchmark                           5.47ns  182.91M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         900.28ns    1.11M
google_spin(1thread)                              94.91%   948.60ns    1.05M
folly_microspin(1thread)                         109.53%   821.97ns    1.22M
folly_picospin(1thread)                          101.86%   883.88ns    1.13M
folly_microlock(1thread)                         102.54%   878.02ns    1.14M
folly_sharedmutex(1thread)                       132.03%   681.86ns    1.47M
folly_distributedmutex(1thread)                  129.50%   695.23ns    1.44M
folly_distributedmutex_combining(1thread)        130.73%   688.68ns    1.45M
folly_flatcombining_no_caching(1thread)          106.73%   843.49ns    1.19M
folly_flatcombining_caching(1thread)             125.22%   718.96ns    1.39M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.27us  784.90K
google_spin(2thread)                             126.84%     1.00us  995.55K
folly_microspin(2thread)                         147.93%   861.24ns    1.16M
folly_picospin(2thread)                          146.10%   872.06ns    1.15M
folly_microlock(2thread)                         131.35%   970.00ns    1.03M
folly_sharedmutex(2thread)                       135.07%   943.23ns    1.06M
folly_distributedmutex(2thread)                  135.88%   937.63ns    1.07M
folly_distributedmutex_combining(2thread)        130.37%   977.27ns    1.02M
folly_flatcombining_no_caching(2thread)           85.64%     1.49us  672.22K
folly_flatcombining_caching(2thread)              91.98%     1.39us  721.93K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.40us  417.44K
google_spin(4thread)                             111.99%     2.14us  467.49K
folly_microspin(4thread)                         101.55%     2.36us  423.92K
folly_picospin(4thread)                           97.89%     2.45us  408.64K
folly_microlock(4thread)                          79.64%     3.01us  332.45K
folly_sharedmutex(4thread)                        75.10%     3.19us  313.49K
folly_distributedmutex(4thread)                  126.16%     1.90us  526.63K
folly_distributedmutex_combining(4thread)        166.56%     1.44us  695.28K
folly_flatcombining_no_caching(4thread)           91.79%     2.61us  383.17K
folly_flatcombining_caching(4thread)             103.95%     2.30us  433.95K
----------------------------------------------------------------------------
std_mutex(8thread)                                           4.85us  206.37K
google_spin(8thread)                             111.05%     4.36us  229.18K
folly_microspin(8thread)                         105.28%     4.60us  217.28K
folly_picospin(8thread)                           89.06%     5.44us  183.80K
folly_microlock(8thread)                          73.95%     6.55us  152.62K
folly_sharedmutex(8thread)                        67.17%     7.21us  138.62K
folly_distributedmutex(8thread)                  162.16%     2.99us  334.66K
folly_distributedmutex_combining(8thread)        251.93%     1.92us  519.92K
folly_flatcombining_no_caching(8thread)          141.99%     3.41us  293.02K
folly_flatcombining_caching(8thread)             166.26%     2.91us  343.12K
----------------------------------------------------------------------------
std_mutex(16thread)                                         11.36us   88.01K
google_spin(16thread)                             99.95%    11.37us   87.96K
folly_microspin(16thread)                        102.73%    11.06us   90.42K
folly_picospin(16thread)                          44.00%    25.83us   38.72K
folly_microlock(16thread)                         52.42%    21.67us   46.14K
folly_sharedmutex(16thread)                       53.46%    21.26us   47.05K
folly_distributedmutex(16thread)                 166.17%     6.84us  146.24K
folly_distributedmutex_combining(16thread)       352.82%     3.22us  310.52K
folly_flatcombining_no_caching(16thread)         218.07%     5.21us  191.92K
folly_flatcombining_caching(16thread)            217.69%     5.22us  191.58K
----------------------------------------------------------------------------
std_mutex(32thread)                                         32.12us   31.13K
google_spin(32thread)                            115.23%    27.88us   35.87K
folly_microspin(32thread)                        104.52%    30.74us   32.54K
folly_picospin(32thread)                          32.81%    97.91us   10.21K
folly_microlock(32thread)                         57.40%    55.96us   17.87K
folly_sharedmutex(32thread)                       63.68%    50.45us   19.82K
folly_distributedmutex(32thread)                 180.17%    17.83us   56.09K
folly_distributedmutex_combining(32thread)       394.34%     8.15us  122.76K
folly_flatcombining_no_caching(32thread)         216.41%    14.84us   67.37K
folly_flatcombining_caching(32thread)            261.99%    12.26us   81.56K
----------------------------------------------------------------------------
std_mutex(64thread)                                         36.76us   27.20K
google_spin(64thread)                            115.38%    31.86us   31.39K
folly_microspin(64thread)                        112.14%    32.78us   30.51K
folly_picospin(64thread)                          32.34%   113.65us    8.80K
folly_microlock(64thread)                         57.21%    64.26us   15.56K
folly_sharedmutex(64thread)                       60.93%    60.33us   16.57K
folly_distributedmutex(64thread)                 179.79%    20.45us   48.91K
folly_distributedmutex_combining(64thread)       392.64%     9.36us  106.81K
folly_flatcombining_no_caching(64thread)         211.85%    17.35us   57.63K
folly_flatcombining_caching(64thread)            241.45%    15.22us   65.68K
----------------------------------------------------------------------------
std_mutex(128thread)                                        73.05us   13.69K
google_spin(128thread)                           116.19%    62.87us   15.91K
folly_microspin(128thread)                        97.45%    74.96us   13.34K
folly_picospin(128thread)                         31.46%   232.19us    4.31K
folly_microlock(128thread)                        56.50%   129.29us    7.73K
folly_sharedmutex(128thread)                      59.54%   122.69us    8.15K
folly_distributedmutex(128thread)                166.59%    43.85us   22.80K
folly_distributedmutex_combining(128thread)      379.86%    19.23us   52.00K
folly_flatcombining_no_caching(128thread)        179.10%    40.79us   24.52K
folly_flatcombining_caching(128thread)           189.64%    38.52us   25.96K
----------------------------------------------------------------------------
std_mutex_simple(1thread)                                  666.33ns    1.50M
google_spin_simple(1thread)                      110.03%   605.58ns    1.65M
folly_microspin_simple(1thread)                  109.80%   606.87ns    1.65M
folly_picospin_simple(1thread)                   108.89%   611.94ns    1.63M
folly_microlock_simple(1thread)                  108.42%   614.59ns    1.63M
folly_sharedmutex_simple(1thread)                 93.00%   716.47ns    1.40M
folly_distributedmutex_simple(1thread)            90.08%   739.68ns    1.35M
folly_distributedmutex_combining_simple(1thread   90.20%   738.73ns    1.35M
folly_flatcombining_no_caching_simple(1thread)    98.04%   679.68ns    1.47M
folly_flatcombining_caching_simple(1thread)      105.59%   631.04ns    1.58M
atomics_fetch_add(1thread)                       108.30%   615.29ns    1.63M
atomic_fetch_xor(1thread)                        110.52%   602.90ns    1.66M
atomic_cas(1thread)                              109.86%   606.52ns    1.65M
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.19us  841.25K
google_spin_simple(2thread)                      107.33%     1.11us  902.89K
folly_microspin_simple(2thread)                  130.73%   909.27ns    1.10M
folly_picospin_simple(2thread)                   112.39%     1.06us  945.48K
folly_microlock_simple(2thread)                  113.89%     1.04us  958.14K
folly_sharedmutex_simple(2thread)                119.48%   994.86ns    1.01M
folly_distributedmutex_simple(2thread)           112.44%     1.06us  945.91K
folly_distributedmutex_combining_simple(2thread  123.12%   965.48ns    1.04M
folly_flatcombining_no_caching_simple(2thread)    90.56%     1.31us  761.82K
folly_flatcombining_caching_simple(2thread)      100.66%     1.18us  846.83K
atomics_fetch_add(2thread)                       119.15%   997.67ns    1.00M
atomic_fetch_xor(2thread)                        179.85%   660.93ns    1.51M
atomic_cas(2thread)                              179.40%   662.58ns    1.51M
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.37us  422.81K
google_spin_simple(4thread)                      106.35%     2.22us  449.64K
folly_microspin_simple(4thread)                  110.42%     2.14us  466.89K
folly_picospin_simple(4thread)                   111.77%     2.12us  472.58K
folly_microlock_simple(4thread)                   82.17%     2.88us  347.44K
folly_sharedmutex_simple(4thread)                 93.40%     2.53us  394.89K
folly_distributedmutex_simple(4thread)           121.00%     1.95us  511.58K
folly_distributedmutex_combining_simple(4thread  187.65%     1.26us  793.42K
folly_flatcombining_no_caching_simple(4thread)   104.81%     2.26us  443.13K
folly_flatcombining_caching_simple(4thread)      112.90%     2.09us  477.34K
atomics_fetch_add(4thread)                       178.61%     1.32us  755.20K
atomic_fetch_xor(4thread)                        323.62%   730.84ns    1.37M
atomic_cas(4thread)                              300.43%   787.23ns    1.27M
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    5.02us  199.09K
google_spin_simple(8thread)                      108.93%     4.61us  216.88K
folly_microspin_simple(8thread)                  116.44%     4.31us  231.82K
folly_picospin_simple(8thread)                    80.84%     6.21us  160.94K
folly_microlock_simple(8thread)                   77.18%     6.51us  153.66K
folly_sharedmutex_simple(8thread)                 76.09%     6.60us  151.48K
folly_distributedmutex_simple(8thread)           145.27%     3.46us  289.21K
folly_distributedmutex_combining_simple(8thread  310.65%     1.62us  618.48K
folly_flatcombining_no_caching_simple(8thread)   139.83%     3.59us  278.39K
folly_flatcombining_caching_simple(8thread)      163.72%     3.07us  325.95K
atomics_fetch_add(8thread)                       337.67%     1.49us  672.28K
atomic_fetch_xor(8thread)                        380.66%     1.32us  757.87K
atomic_cas(8thread)                              238.04%     2.11us  473.93K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                  12.26us   81.59K
google_spin_simple(16thread)                     104.37%    11.74us   85.16K
folly_microspin_simple(16thread)                 116.32%    10.54us   94.91K
folly_picospin_simple(16thread)                   53.67%    22.83us   43.79K
folly_microlock_simple(16thread)                  66.39%    18.46us   54.17K
folly_sharedmutex_simple(16thread)                65.00%    18.85us   53.04K
folly_distributedmutex_simple(16thread)          171.32%     7.15us  139.79K
folly_distributedmutex_combining_simple(16threa  445.11%     2.75us  363.17K
folly_flatcombining_no_caching_simple(16thread)  206.11%     5.95us  168.17K
folly_flatcombining_caching_simple(16thread)     245.09%     5.00us  199.97K
atomics_fetch_add(16thread)                      494.82%     2.48us  403.73K
atomic_fetch_xor(16thread)                       489.90%     2.50us  399.72K
atomic_cas(16thread)                             232.76%     5.27us  189.91K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  30.28us   33.03K
google_spin_simple(32thread)                     106.34%    28.47us   35.12K
folly_microspin_simple(32thread)                 102.20%    29.62us   33.76K
folly_picospin_simple(32thread)                   31.56%    95.92us   10.43K
folly_microlock_simple(32thread)                  53.99%    56.07us   17.83K
folly_sharedmutex_simple(32thread)                67.49%    44.86us   22.29K
folly_distributedmutex_simple(32thread)          161.63%    18.73us   53.38K
folly_distributedmutex_combining_simple(32threa  605.26%     5.00us  199.92K
folly_flatcombining_no_caching_simple(32thread)  234.62%    12.90us   77.49K
folly_flatcombining_caching_simple(32thread)     332.21%     9.11us  109.73K
atomics_fetch_add(32thread)                      909.18%     3.33us  300.30K
atomic_fetch_xor(32thread)                       779.56%     3.88us  257.49K
atomic_cas(32thread)                             622.19%     4.87us  205.51K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  34.33us   29.13K
google_spin_simple(64thread)                     106.28%    32.30us   30.96K
folly_microspin_simple(64thread)                  99.86%    34.37us   29.09K
folly_picospin_simple(64thread)                   31.37%   109.42us    9.14K
folly_microlock_simple(64thread)                  53.46%    64.21us   15.57K
folly_sharedmutex_simple(64thread)                62.94%    54.54us   18.33K
folly_distributedmutex_simple(64thread)          161.26%    21.29us   46.98K
folly_distributedmutex_combining_simple(64threa  603.87%     5.68us  175.91K
folly_flatcombining_no_caching_simple(64thread)  247.00%    13.90us   71.95K
folly_flatcombining_caching_simple(64thread)     310.66%    11.05us   90.50K
atomics_fetch_add(64thread)                      839.49%     4.09us  244.55K
atomic_fetch_xor(64thread)                       756.48%     4.54us  220.37K
atomic_cas(64thread)                             606.85%     5.66us  176.78K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 67.35us   14.85K
google_spin_simple(128thread)                    106.30%    63.36us   15.78K
folly_microspin_simple(128thread)                 92.58%    72.75us   13.75K
folly_picospin_simple(128thread)                  29.87%   225.47us    4.44K
folly_microlock_simple(128thread)                 52.52%   128.25us    7.80K
folly_sharedmutex_simple(128thread)               59.79%   112.64us    8.88K
folly_distributedmutex_simple(128thread)         151.27%    44.52us   22.46K
folly_distributedmutex_combining_simple(128thre  580.11%    11.61us   86.13K
folly_flatcombining_no_caching_simple(128thread  219.20%    30.73us   32.55K
folly_flatcombining_caching_simple(128thread)    225.39%    29.88us   33.46K
atomics_fetch_add(128thread)                     813.36%     8.28us  120.76K
atomic_fetch_xor(128thread)                      740.02%     9.10us  109.88K
atomic_cas(128thread)                            586.66%    11.48us   87.11K
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
std_mutex(1thread)                                           1.37us  730.43K
google_spin(1thread)                             104.25%     1.31us  761.46K
folly_microspin(1thread)                         102.06%     1.34us  745.45K
folly_picospin(1thread)                          100.68%     1.36us  735.43K
folly_microlock(1thread)                         104.27%     1.31us  761.64K
folly_sharedmutex(1thread)                       101.95%     1.34us  744.65K
folly_distributedmutex(1thread)                   98.63%     1.39us  720.41K
folly_distributedmutex_combining(1thread)        103.78%     1.32us  758.05K
folly_flatcombining_no_caching(1thread)           95.44%     1.43us  697.15K
folly_flatcombining_caching(1thread)              99.11%     1.38us  723.94K
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.65us  605.36K
google_spin(2thread)                             111.27%     1.48us  673.61K
folly_microspin(2thread)                         119.82%     1.38us  725.35K
folly_picospin(2thread)                          112.46%     1.47us  680.81K
folly_microlock(2thread)                         106.47%     1.55us  644.54K
folly_sharedmutex(2thread)                       107.12%     1.54us  648.45K
folly_distributedmutex(2thread)                  110.80%     1.49us  670.76K
folly_distributedmutex_combining(2thread)         97.09%     1.70us  587.77K
folly_flatcombining_no_caching(2thread)           83.37%     1.98us  504.68K
folly_flatcombining_caching(2thread)             108.62%     1.52us  657.54K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.92us  341.96K
google_spin(4thread)                             137.67%     2.12us  470.78K
folly_microspin(4thread)                         165.47%     1.77us  565.85K
folly_picospin(4thread)                          181.92%     1.61us  622.09K
folly_microlock(4thread)                         149.83%     1.95us  512.35K
folly_sharedmutex(4thread)                       158.69%     1.84us  542.66K
folly_distributedmutex(4thread)                  107.42%     2.72us  367.35K
folly_distributedmutex_combining(4thread)        144.34%     2.03us  493.59K
folly_flatcombining_no_caching(4thread)           88.43%     3.31us  302.40K
folly_flatcombining_caching(4thread)              94.20%     3.10us  322.11K
----------------------------------------------------------------------------
std_mutex(8thread)                                           7.04us  142.02K
google_spin(8thread)                             128.50%     5.48us  182.49K
folly_microspin(8thread)                         134.72%     5.23us  191.32K
folly_picospin(8thread)                          112.37%     6.27us  159.58K
folly_microlock(8thread)                         109.65%     6.42us  155.71K
folly_sharedmutex(8thread)                       105.92%     6.65us  150.42K
folly_distributedmutex(8thread)                  127.22%     5.53us  180.67K
folly_distributedmutex_combining(8thread)        275.50%     2.56us  391.26K
folly_flatcombining_no_caching(8thread)          144.99%     4.86us  205.92K
folly_flatcombining_caching(8thread)             156.31%     4.50us  221.99K
----------------------------------------------------------------------------
std_mutex(16thread)                                         13.08us   76.44K
google_spin(16thread)                            121.76%    10.74us   93.07K
folly_microspin(16thread)                         91.47%    14.30us   69.92K
folly_picospin(16thread)                          67.95%    19.25us   51.94K
folly_microlock(16thread)                         73.57%    17.78us   56.24K
folly_sharedmutex(16thread)                       70.59%    18.53us   53.96K
folly_distributedmutex(16thread)                 139.74%     9.36us  106.82K
folly_distributedmutex_combining(16thread)       338.38%     3.87us  258.67K
folly_flatcombining_no_caching(16thread)         194.08%     6.74us  148.36K
folly_flatcombining_caching(16thread)            195.03%     6.71us  149.09K
----------------------------------------------------------------------------
std_mutex(32thread)                                         25.35us   39.45K
google_spin(32thread)                            122.73%    20.66us   48.41K
folly_microspin(32thread)                         73.81%    34.35us   29.11K
folly_picospin(32thread)                          50.66%    50.04us   19.98K
folly_microlock(32thread)                         58.40%    43.41us   23.03K
folly_sharedmutex(32thread)                       55.14%    45.98us   21.75K
folly_distributedmutex(32thread)                 141.36%    17.93us   55.76K
folly_distributedmutex_combining(32thread)       358.52%     7.07us  141.42K
folly_flatcombining_no_caching(32thread)         257.78%     9.83us  101.68K
folly_flatcombining_caching(32thread)            285.82%     8.87us  112.74K
----------------------------------------------------------------------------
std_mutex(64thread)                                         45.03us   22.21K
google_spin(64thread)                            124.58%    36.15us   27.66K
folly_microspin(64thread)                         75.05%    60.00us   16.67K
folly_picospin(64thread)                          44.98%   100.12us    9.99K
folly_microlock(64thread)                         56.99%    79.01us   12.66K
folly_sharedmutex(64thread)                       52.67%    85.49us   11.70K
folly_distributedmutex(64thread)                 139.71%    32.23us   31.02K
folly_distributedmutex_combining(64thread)       343.76%    13.10us   76.34K
folly_flatcombining_no_caching(64thread)         211.67%    21.27us   47.01K
folly_flatcombining_caching(64thread)            222.51%    20.24us   49.41K
----------------------------------------------------------------------------
std_mutex(128thread)                                        88.78us   11.26K
google_spin(128thread)                           125.10%    70.96us   14.09K
folly_microspin(128thread)                        71.00%   125.03us    8.00K
folly_picospin(128thread)                         30.97%   286.63us    3.49K
folly_microlock(128thread)                        54.37%   163.28us    6.12K
folly_sharedmutex(128thread)                      51.69%   171.76us    5.82K
folly_distributedmutex(128thread)                137.37%    64.63us   15.47K
folly_distributedmutex_combining(128thread)      281.23%    31.57us   31.68K
folly_flatcombining_no_caching(128thread)        136.61%    64.99us   15.39K
folly_flatcombining_caching(128thread)           152.32%    58.29us   17.16K
----------------------------------------------------------------------------
std_mutex_simple(1thread)                                    1.63us  611.75K
google_spin_simple(1thread)                      105.70%     1.55us  646.61K
folly_microspin_simple(1thread)                  103.24%     1.58us  631.57K
folly_picospin_simple(1thread)                   109.17%     1.50us  667.87K
folly_microlock_simple(1thread)                  111.22%     1.47us  680.41K
folly_sharedmutex_simple(1thread)                136.79%     1.19us  836.83K
folly_distributedmutex_simple(1thread)           107.21%     1.52us  655.88K
folly_distributedmutex_combining_simple(1thread  134.79%     1.21us  824.61K
folly_flatcombining_no_caching_simple(1thread)   127.99%     1.28us  782.99K
folly_flatcombining_caching_simple(1thread)      133.87%     1.22us  818.93K
atomics_fetch_add(1thread)                       138.24%     1.18us  845.70K
atomic_fetch_xor(1thread)                        106.94%     1.53us  654.23K
atomic_cas(1thread)                              124.81%     1.31us  763.52K
----------------------------------------------------------------------------
std_mutex_simple(2thread)                                    1.60us  626.60K
google_spin_simple(2thread)                       96.04%     1.66us  601.80K
folly_microspin_simple(2thread)                  111.88%     1.43us  701.02K
folly_picospin_simple(2thread)                   106.11%     1.50us  664.91K
folly_microlock_simple(2thread)                   88.90%     1.80us  557.04K
folly_sharedmutex_simple(2thread)                 90.93%     1.76us  569.79K
folly_distributedmutex_simple(2thread)            93.93%     1.70us  588.57K
folly_distributedmutex_combining_simple(2thread  106.86%     1.49us  669.61K
folly_flatcombining_no_caching_simple(2thread)    85.92%     1.86us  538.37K
folly_flatcombining_caching_simple(2thread)       98.82%     1.61us  619.24K
atomics_fetch_add(2thread)                       104.61%     1.53us  655.46K
atomic_fetch_xor(2thread)                        126.46%     1.26us  792.40K
atomic_cas(2thread)                              125.92%     1.27us  788.99K
----------------------------------------------------------------------------
std_mutex_simple(4thread)                                    2.71us  368.45K
google_spin_simple(4thread)                      124.52%     2.18us  458.79K
folly_microspin_simple(4thread)                  146.48%     1.85us  539.69K
folly_picospin_simple(4thread)                   163.54%     1.66us  602.57K
folly_microlock_simple(4thread)                  113.17%     2.40us  416.99K
folly_sharedmutex_simple(4thread)                142.36%     1.91us  524.52K
folly_distributedmutex_simple(4thread)           108.22%     2.51us  398.74K
folly_distributedmutex_combining_simple(4thread  141.49%     1.92us  521.30K
folly_flatcombining_no_caching_simple(4thread)    97.27%     2.79us  358.38K
folly_flatcombining_caching_simple(4thread)      106.12%     2.56us  390.99K
atomics_fetch_add(4thread)                       151.10%     1.80us  556.73K
atomic_fetch_xor(4thread)                        213.14%     1.27us  785.32K
atomic_cas(4thread)                              218.93%     1.24us  806.65K
----------------------------------------------------------------------------
std_mutex_simple(8thread)                                    7.02us  142.50K
google_spin_simple(8thread)                      127.47%     5.51us  181.64K
folly_microspin_simple(8thread)                  137.77%     5.09us  196.33K
folly_picospin_simple(8thread)                   119.78%     5.86us  170.69K
folly_microlock_simple(8thread)                  108.08%     6.49us  154.02K
folly_sharedmutex_simple(8thread)                114.77%     6.11us  163.55K
folly_distributedmutex_simple(8thread)           120.24%     5.84us  171.35K
folly_distributedmutex_combining_simple(8thread  316.54%     2.22us  451.07K
folly_flatcombining_no_caching_simple(8thread)   136.43%     5.14us  194.42K
folly_flatcombining_caching_simple(8thread)      145.04%     4.84us  206.68K
atomics_fetch_add(8thread)                       358.98%     1.95us  511.55K
atomic_fetch_xor(8thread)                        505.27%     1.39us  720.02K
atomic_cas(8thread)                              389.32%     1.80us  554.79K
----------------------------------------------------------------------------
std_mutex_simple(16thread)                                  12.78us   78.24K
google_spin_simple(16thread)                     122.66%    10.42us   95.96K
folly_microspin_simple(16thread)                  98.10%    13.03us   76.75K
folly_picospin_simple(16thread)                   72.52%    17.62us   56.74K
folly_microlock_simple(16thread)                  70.12%    18.23us   54.86K
folly_sharedmutex_simple(16thread)                76.81%    16.64us   60.09K
folly_distributedmutex_simple(16thread)          113.84%    11.23us   89.06K
folly_distributedmutex_combining_simple(16threa  498.99%     2.56us  390.39K
folly_flatcombining_no_caching_simple(16thread)  193.05%     6.62us  151.04K
folly_flatcombining_caching_simple(16thread)     220.47%     5.80us  172.49K
atomics_fetch_add(16thread)                      611.70%     2.09us  478.58K
atomic_fetch_xor(16thread)                       515.51%     2.48us  403.32K
atomic_cas(16thread)                             239.86%     5.33us  187.66K
----------------------------------------------------------------------------
std_mutex_simple(32thread)                                  23.80us   42.02K
google_spin_simple(32thread)                     125.41%    18.98us   52.69K
folly_microspin_simple(32thread)                  76.32%    31.18us   32.07K
folly_picospin_simple(32thread)                   48.82%    48.75us   20.51K
folly_microlock_simple(32thread)                  52.99%    44.92us   22.26K
folly_sharedmutex_simple(32thread)                54.03%    44.05us   22.70K
folly_distributedmutex_simple(32thread)          108.28%    21.98us   45.49K
folly_distributedmutex_combining_simple(32threa  697.71%     3.41us  293.15K
folly_flatcombining_no_caching_simple(32thread)  291.70%     8.16us  122.56K
folly_flatcombining_caching_simple(32thread)     412.51%     5.77us  173.32K
atomics_fetch_add(32thread)                     1074.64%     2.21us  451.52K
atomic_fetch_xor(32thread)                       577.90%     4.12us  242.81K
atomic_cas(32thread)                             193.87%    12.28us   81.46K
----------------------------------------------------------------------------
std_mutex_simple(64thread)                                  41.40us   24.16K
google_spin_simple(64thread)                     125.42%    33.01us   30.30K
folly_microspin_simple(64thread)                  75.30%    54.98us   18.19K
folly_picospin_simple(64thread)                   42.87%    96.57us   10.35K
folly_microlock_simple(64thread)                  50.88%    81.37us   12.29K
folly_sharedmutex_simple(64thread)                50.08%    82.67us   12.10K
folly_distributedmutex_simple(64thread)          105.81%    39.12us   25.56K
folly_distributedmutex_combining_simple(64threa  604.86%     6.84us  146.11K
folly_flatcombining_no_caching_simple(64thread)  269.82%    15.34us   65.18K
folly_flatcombining_caching_simple(64thread)     334.78%    12.37us   80.87K
atomics_fetch_add(64thread)                     1061.21%     3.90us  256.34K
atomic_fetch_xor(64thread)                       551.00%     7.51us  133.10K
atomic_cas(64thread)                             183.75%    22.53us   44.39K
----------------------------------------------------------------------------
std_mutex_simple(128thread)                                 80.97us   12.35K
google_spin_simple(128thread)                    124.75%    64.90us   15.41K
folly_microspin_simple(128thread)                 70.93%   114.16us    8.76K
folly_picospin_simple(128thread)                  32.81%   246.78us    4.05K
folly_microlock_simple(128thread)                 48.00%   168.69us    5.93K
folly_sharedmutex_simple(128thread)               49.03%   165.15us    6.06K
folly_distributedmutex_simple(128thread)         103.96%    77.88us   12.84K
folly_distributedmutex_combining_simple(128thre  460.68%    17.58us   56.90K
folly_flatcombining_no_caching_simple(128thread  211.10%    38.35us   26.07K
folly_flatcombining_caching_simple(128thread)    220.02%    36.80us   27.17K
atomics_fetch_add(128thread)                    1031.88%     7.85us  127.45K
atomic_fetch_xor(128thread)                      543.67%    14.89us   67.15K
atomic_cas(128thread)                            179.37%    45.14us   22.15K
============================================================================
*/
