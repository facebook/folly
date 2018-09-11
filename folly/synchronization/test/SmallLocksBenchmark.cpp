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
BENCH_BASE(google_spin, 1thread, 1)
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
Sum: 3108396 Mean: 55507 stddev: 381
Lock time stats in us: mean 18 stddev 1382 max 11421
GLock<SpinLock>:
Sum: 3975385 Mean: 70989 stddev: 4719
Lock time stats in us: mean 11 stddev 1080 max 11956
InitLock<folly::MicroSpinLock>:
Sum: 2827915 Mean: 50498 stddev: 21612
Lock time stats in us: mean 38 stddev 1518 max 575420
InitLock<folly::PicoSpinLock<uint16_t>>:
Sum: 1018989 Mean: 18196 stddev: 13122
Lock time stats in us: mean 107 stddev 4214 max 414541
folly::MicroLock:
Sum: 1733925 Mean: 30962 stddev: 9727
Lock time stats in us: mean 51 stddev 2476 max 14267
============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                23.00ns   43.47M
GoogleSpinUncontendedBenchmark                              15.47ns   64.65M
MicroSpinLockUncontendedBenchmark                           13.80ns   72.48M
PicoSpinLockUncontendedBenchmark                            15.47ns   64.65M
MicroLockUncontendedBenchmark                               29.70ns   33.67M
VirtualFunctionCall                                        104.66ps    9.55G
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                         855.49ns    1.17M
google_spin(1thread)                                       919.71ns    1.09M
folly_microspin(1thread)                         100.07%   919.12ns    1.09M
folly_picospin(1thread)                           99.08%   928.24ns    1.08M
folly_microlock(1thread)                         108.74%   845.77ns    1.18M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.35us  739.03K
google_spin(2thread)                             123.06%     1.10us  909.43K
folly_microspin(2thread)                         130.02%     1.04us  960.87K
folly_picospin(2thread)                          129.37%     1.05us  956.10K
folly_microlock(2thread)                         122.24%     1.11us  903.42K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.96us  338.26K
google_spin(4thread)                             130.26%     2.27us  440.63K
folly_microspin(4thread)                         124.37%     2.38us  420.71K
folly_picospin(4thread)                          139.97%     2.11us  473.46K
folly_microlock(4thread)                         109.60%     2.70us  370.75K
----------------------------------------------------------------------------
std_mutex(8thread)                                           6.27us  159.56K
google_spin(8thread)                             121.56%     5.16us  193.95K
folly_microspin(8thread)                         112.33%     5.58us  179.23K
folly_picospin(8thread)                           89.25%     7.02us  142.41K
folly_microlock(8thread)                          77.43%     8.09us  123.55K
----------------------------------------------------------------------------
std_mutex(16thread)                                         12.86us   77.77K
google_spin(16thread)                            114.33%    11.25us   88.91K
folly_microspin(16thread)                        104.78%    12.27us   81.49K
folly_picospin(16thread)                          43.77%    29.38us   34.04K
folly_microlock(16thread)                         52.30%    24.59us   40.68K
----------------------------------------------------------------------------
std_mutex(32thread)                                         35.31us   28.32K
google_spin(32thread)                            122.32%    28.87us   34.64K
folly_microspin(32thread)                         96.66%    36.53us   27.37K
folly_picospin(32thread)                          32.20%   109.66us    9.12K
folly_microlock(32thread)                         55.95%    63.11us   15.84K
----------------------------------------------------------------------------
std_mutex(64thread)                                         41.84us   23.90K
google_spin(64thread)                            126.88%    32.97us   30.33K
folly_microspin(64thread)                         99.29%    42.14us   23.73K
folly_picospin(64thread)                          31.63%   132.26us    7.56K
folly_microlock(64thread)                         57.99%    72.15us   13.86K
============================================================================
*/
