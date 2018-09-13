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
#include <numeric>
#include <thread>
#include <vector>

#include <google/base/spinlock.h>

#include <folly/Benchmark.h>
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

static void burn(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(i);
  }
}

namespace {

struct SimpleBarrier {
  explicit SimpleBarrier(size_t count) : lock_(), cv_(), count_(count) {}

  void wait() {
    std::unique_lock<std::mutex> lockHeld(lock_);
    if (++num_ == count_) {
      cv_.notify_all();
    } else {
      cv_.wait(lockHeld, [&]() { return num_ >= count_; });
    }
  }

 private:
  std::mutex lock_;
  std::condition_variable cv_;
  size_t num_{0};
  size_t count_;
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
    Lock lock;
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
      lockstruct* lock = &locks[t % threadgroups];
      runbarrier.wait();
      for (size_t op = 0; op < numOps; op += 1) {
        lock->lock.lock();
        burn(FLAGS_work);
        lock->value++;
        lock->lock.unlock();
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
static void runFairness() {
  size_t numThreads = FLAGS_threads;
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
      lockstruct* lock = &locks[t % threadgroups];
      long value = 0;
      std::chrono::microseconds max(0);
      std::chrono::microseconds time(0);
      unsigned long timeSq(0);
      runbarrier.wait();
      while (!stop) {
        std::chrono::steady_clock::time_point prelock =
            std::chrono::steady_clock::now();
        lock->lock.lock();
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
        lock->lock.unlock();
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
  std::this_thread::sleep_for(std::chrono::seconds(2));
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

BENCHMARK(StdMutexUncontendedBenchmark, iters) {
  std::mutex lock;
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(GoogleSpinUncontendedBenchmark, iters) {
  SpinLock lock;
  while (iters--) {
    lock.Lock();
    lock.Unlock();
  }
}

BENCHMARK(MicroSpinLockUncontendedBenchmark, iters) {
  folly::MicroSpinLock lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(PicoSpinLockUncontendedBenchmark, iters) {
  // uint8_t would be more fair, but PicoSpinLock needs at lesat two bytes
  folly::PicoSpinLock<uint16_t> lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(MicroLockUncontendedBenchmark, iters) {
  folly::MicroLock lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
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

BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 1thread, 1)
BENCH_REL(google_spin, 1thread, 1)
BENCH_REL(folly_microspin, 1thread, 1)
BENCH_REL(folly_picospin, 1thread, 1)
BENCH_REL(folly_microlock, 1thread, 1)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 2thread, 2)
BENCH_REL(google_spin, 2thread, 2)
BENCH_REL(folly_microspin, 2thread, 2)
BENCH_REL(folly_picospin, 2thread, 2)
BENCH_REL(folly_microlock, 2thread, 2)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 4thread, 4)
BENCH_REL(google_spin, 4thread, 4)
BENCH_REL(folly_microspin, 4thread, 4)
BENCH_REL(folly_picospin, 4thread, 4)
BENCH_REL(folly_microlock, 4thread, 4)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 8thread, 8)
BENCH_REL(google_spin, 8thread, 8)
BENCH_REL(folly_microspin, 8thread, 8)
BENCH_REL(folly_picospin, 8thread, 8)
BENCH_REL(folly_microlock, 8thread, 8)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 16thread, 16)
BENCH_REL(google_spin, 16thread, 16)
BENCH_REL(folly_microspin, 16thread, 16)
BENCH_REL(folly_picospin, 16thread, 16)
BENCH_REL(folly_microlock, 16thread, 16)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 32thread, 32)
BENCH_REL(google_spin, 32thread, 32)
BENCH_REL(folly_microspin, 32thread, 32)
BENCH_REL(folly_picospin, 32thread, 32)
BENCH_REL(folly_microlock, 32thread, 32)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 64thread, 64)
BENCH_REL(google_spin, 64thread, 64)
BENCH_REL(folly_microspin, 64thread, 64)
BENCH_REL(folly_picospin, 64thread, 64)
BENCH_REL(folly_microlock, 64thread, 64)

#define FairnessTest(type) \
  {                        \
    printf(#type ": \n");  \
    runFairness<type>();   \
  }

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FairnessTest(std::mutex);
  FairnessTest(GoogleSpinLockAdapter);
  FairnessTest(InitLock<folly::MicroSpinLock>);
  FairnessTest(InitLock<folly::PicoSpinLock<uint16_t>>);
  FairnessTest(folly::MicroLock);

  folly::runBenchmarks();

  return 0;
}

/*
./small_locks_benchmark --bm_min_iters=100000
Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz

std::mutex:
Sum: 3762980 Mean: 67196 stddev: 916
Lock time stats in us: mean 15 stddev 1141 max 22384
GoogleSpinLockAdapter:
Sum: 4263945 Mean: 76141 stddev: 2072
Lock time stats in us: mean 10 stddev 1007 max 10132
InitLock<folly::MicroSpinLock>:
Sum: 3469284 Mean: 61951 stddev: 15693
Lock time stats in us: mean 31 stddev 1237 max 334200
InitLock<folly::PicoSpinLock<uint16_t>>:
Sum: 1011034 Mean: 18054 stddev: 3819
Lock time stats in us: mean 108 stddev 4247 max 691707
folly::MicroLock:
Sum: 1712173 Mean: 30574 stddev: 4032
Lock time stats in us: mean 53 stddev 2508 max 11554
============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                16.74ns   59.72M
GoogleSpinUncontendedBenchmark                              11.26ns   88.78M
MicroSpinLockUncontendedBenchmark                           10.62ns   94.16M
PicoSpinLockUncontendedBenchmark                            11.83ns   84.54M
MicroLockUncontendedBenchmark                               20.62ns   48.50M
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         745.50ns    1.34M
google_spin(1thread)                             103.88%   717.66ns    1.39M
folly_microspin(1thread)                         102.61%   726.55ns    1.38M
folly_picospin(1thread)                           90.78%   821.20ns    1.22M
folly_microlock(1thread)                          96.64%   771.42ns    1.30M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.26us  796.48K
google_spin(2thread)                             136.08%   922.64ns    1.08M
folly_microspin(2thread)                         145.00%   865.87ns    1.15M
folly_picospin(2thread)                          163.80%   766.49ns    1.30M
folly_microlock(2thread)                         137.96%   910.06ns    1.10M
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.16us  462.09K
google_spin(4thread)                             107.52%     2.01us  496.84K
folly_microspin(4thread)                         103.81%     2.08us  479.71K
folly_picospin(4thread)                          105.20%     2.06us  486.14K
folly_microlock(4thread)                          77.07%     2.81us  356.15K
----------------------------------------------------------------------------
std_mutex(8thread)                                           5.55us  180.33K
google_spin(8thread)                             110.44%     5.02us  199.16K
folly_microspin(8thread)                         105.13%     5.27us  189.58K
folly_picospin(8thread)                           98.81%     5.61us  178.19K
folly_microlock(8thread)                          81.95%     6.77us  147.78K
----------------------------------------------------------------------------
std_mutex(16thread)                                         11.30us   88.48K
google_spin(16thread)                            109.33%    10.34us   96.74K
folly_microspin(16thread)                        105.86%    10.68us   93.67K
folly_picospin(16thread)                          43.61%    25.92us   38.58K
folly_microlock(16thread)                         52.82%    21.40us   46.73K
----------------------------------------------------------------------------
std_mutex(32thread)                                         32.24us   31.02K
google_spin(32thread)                            100.57%    32.06us   31.19K
folly_microspin(32thread)                        102.32%    31.51us   31.73K
folly_picospin(32thread)                          36.63%    88.02us   11.36K
folly_microlock(32thread)                         57.61%    55.97us   17.87K
----------------------------------------------------------------------------
std_mutex(64thread)                                         35.67us   28.04K
google_spin(64thread)                            111.44%    32.01us   31.24K
folly_microspin(64thread)                         94.45%    37.76us   26.48K
folly_picospin(64thread)                          36.01%    99.05us   10.10K
folly_microlock(64thread)                         54.11%    65.92us   15.17K
============================================================================
*/
