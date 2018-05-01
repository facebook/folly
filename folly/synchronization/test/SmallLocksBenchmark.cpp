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
BENCH_REL(folly_microspin, 1thread, 1)
BENCH_REL(folly_picospin, 1thread, 1)
BENCH_REL(folly_microlock, 1thread, 1)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 2thread, 2)
BENCH_REL(folly_microspin, 2thread, 2)
BENCH_REL(folly_picospin, 2thread, 2)
BENCH_REL(folly_microlock, 2thread, 2)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 4thread, 4)
BENCH_REL(folly_microspin, 4thread, 4)
BENCH_REL(folly_picospin, 4thread, 4)
BENCH_REL(folly_microlock, 4thread, 4)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 8thread, 8)
BENCH_REL(folly_microspin, 8thread, 8)
BENCH_REL(folly_picospin, 8thread, 8)
BENCH_REL(folly_microlock, 8thread, 8)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 16thread, 16)
BENCH_REL(folly_microspin, 16thread, 16)
BENCH_REL(folly_picospin, 16thread, 16)
BENCH_REL(folly_microlock, 16thread, 16)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 32thread, 32)
BENCH_REL(folly_microspin, 32thread, 32)
BENCH_REL(folly_picospin, 32thread, 32)
BENCH_REL(folly_microlock, 32thread, 32)
BENCHMARK_DRAW_LINE();
BENCH_BASE(std_mutex, 64thread, 64)
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
  FairnessTest(InitLock<folly::MicroSpinLock>);
  FairnessTest(InitLock<folly::PicoSpinLock<uint16_t>>);
  FairnessTest(folly::MicroLock);

  folly::runBenchmarks();

  return 0;
}

/*
locks_benchmark --bm_min_iters=1000000

std::mutex:
Sum: 2879853 Mean: 89995 stddev: 1906
Lock time stats in us: mean 11 stddev 1491 max 12271
InitLock<folly::MicroSpinLock>:
Sum: 3148551 Mean: 98392 stddev: 74339
Lock time stats in us: mean 19 stddev 1364 max 180125
InitLock<folly::PicoSpinLock<uint16_t>>:
Sum: 1137238 Mean: 35538 stddev: 28156
Lock time stats in us: mean 54 stddev 3776 max 322180
folly::MicroLock:
Sum: 1897729 Mean: 59304 stddev: 25571
Lock time stats in us: mean 30 stddev 2263 max 23898
============================================================================
folly/synchronization/test/SmallLocksBenchmark.cpprelative  time/iter  iters/s
============================================================================
StdMutexUncontendedBenchmark                                27.15ns   36.83M
MicroSpinLockUncontendedBenchmark                            9.70ns  103.11M
PicoSpinLockUncontendedBenchmark                            15.19ns   65.83M
MicroLockUncontendedBenchmark                               26.73ns   37.41M
VirtualFunctionCall                                        333.36ps    3.00G
----------------------------------------------------------------------------
----------------------------------------------------------------------------
std_mutex(1thread)                                           1.07us  931.07K
folly_microspin(1thread)                         107.98%   994.71ns    1.01M
folly_picospin(1thread)                          104.65%     1.03us  974.35K
folly_microlock(1thread)                         127.27%   843.90ns    1.18M
----------------------------------------------------------------------------
std_mutex(2thread)                                           1.38us  722.80K
folly_microspin(2thread)                         124.79%     1.11us  901.99K
folly_picospin(2thread)                          123.67%     1.12us  893.87K
folly_microlock(2thread)                         123.19%     1.12us  890.42K
----------------------------------------------------------------------------
std_mutex(4thread)                                           2.80us  357.26K
folly_microspin(4thread)                          79.24%     3.53us  283.10K
folly_picospin(4thread)                          122.78%     2.28us  438.65K
folly_microlock(4thread)                          96.59%     2.90us  345.09K
----------------------------------------------------------------------------
std_mutex(8thread)                                           5.49us  182.15K
folly_microspin(8thread)                          94.14%     5.83us  171.48K
folly_picospin(8thread)                           64.53%     8.51us  117.55K
folly_microlock(8thread)                          74.02%     7.42us  134.83K
----------------------------------------------------------------------------
std_mutex(16thread)                                         11.86us   84.30K
folly_microspin(16thread)                        121.77%     9.74us  102.65K
folly_picospin(16thread)                          62.15%    19.09us   52.39K
folly_microlock(16thread)                         65.02%    18.25us   54.81K
----------------------------------------------------------------------------
std_mutex(32thread)                                         22.66us   44.13K
folly_microspin(32thread)                        116.62%    19.43us   51.46K
folly_picospin(32thread)                          48.54%    46.69us   21.42K
folly_microlock(32thread)                         66.88%    33.89us   29.51K
----------------------------------------------------------------------------
std_mutex(64thread)                                         45.13us   22.16K
folly_microspin(64thread)                        114.88%    39.28us   25.46K
folly_picospin(64thread)                          38.82%   116.25us    8.60K
folly_microlock(64thread)                         64.43%    70.05us   14.28K
============================================================================
*/
