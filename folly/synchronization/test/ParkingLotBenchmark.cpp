/*
 * Copyright 2018-present Facebook, Inc.
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
#include <thread>

#include <folly/synchronization/ParkingLot.h>

#include <folly/Benchmark.h>
#include <folly/detail/Futex.h>
#include <folly/synchronization/Baton.h>

DEFINE_uint64(threads, 32, "Number of threads for benchmark");

using namespace folly;

namespace {
struct SimpleBarrier {
  explicit SimpleBarrier(size_t count) : lock_(), cv_(), count_(count) {}

  void wait() {
    std::unique_lock<std::mutex> lockHeld(lock_);
    auto gen = gen_;
    if (++num_ == count_) {
      num_ = 0;
      gen_++;
      cv_.notify_all();
    } else {
      cv_.wait(lockHeld, [&]() { return gen == gen_; });
    }
  }

 private:
  std::mutex lock_;
  std::condition_variable cv_;
  size_t num_{0};
  size_t count_;
  size_t gen_{0};
};
} // namespace

ParkingLot<> lot;

BENCHMARK(FutexNoWaitersWake, iters) {
  BenchmarkSuspender susp;
  folly::detail::Futex<> fu;
  SimpleBarrier b(FLAGS_threads + 1);

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      for (auto i = 0u; i < iters; i++) {
        fu.futexWake(1);
      }
    });
  }
  susp.dismiss();
  b.wait();

  for (auto& t : threads) {
    t.join();
  }
}

BENCHMARK_RELATIVE(ParkingLotNoWaitersWake, iters) {
  BenchmarkSuspender susp;
  SimpleBarrier b(FLAGS_threads + 1);

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      for (auto i = 0u; i < iters; i++) {
        lot.unpark(&lot, [](Unit) { return UnparkControl::RetainContinue; });
      }
    });
  }
  susp.dismiss();
  b.wait();

  for (auto& t : threads) {
    t.join();
  }
}

BENCHMARK(FutexWakeOne, iters) {
  BenchmarkSuspender susp;
  folly::detail::Futex<> fu;
  SimpleBarrier b(FLAGS_threads + 1);

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      while (true) {
        fu.futexWait(0);
        if (fu.load(std::memory_order_relaxed)) {
          return;
        }
      }
    });
  }
  susp.dismiss();
  b.wait();
  for (auto i = 0u; i < iters; i++) {
    fu.futexWake(1);
  }
  fu.store(1);
  fu.futexWake(threads.size());

  for (auto& t : threads) {
    t.join();
  }
}

BENCHMARK_RELATIVE(ParkingLotWakeOne, iters) {
  BenchmarkSuspender susp;
  std::atomic<bool> done{false};
  SimpleBarrier b(FLAGS_threads + 1);

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      while (true) {
        Unit f;
        lot.park(
            &done,
            f,
            [&] { return done.load(std::memory_order_relaxed) == 0; },
            [] {});
        if (done.load(std::memory_order_relaxed)) {
          return;
        }
      }
    });
  }
  susp.dismiss();
  b.wait();
  for (auto i = 0u; i < iters; i++) {
    lot.unpark(&done, [](Unit) { return UnparkControl::RemoveBreak; });
  }
  done = true;
  lot.unpark(&done, [](Unit) { return UnparkControl::RemoveContinue; });

  for (auto& t : threads) {
    t.join();
  }
}

BENCHMARK(FutexWakeAll, iters) {
  BenchmarkSuspender susp;
  SimpleBarrier b(FLAGS_threads + 1);
  folly::detail::Futex<> fu;
  std::atomic<bool> done{false};

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      while (true) {
        fu.futexWait(0);
        if (done.load(std::memory_order_relaxed)) {
          return;
        }
      }
    });
  }
  susp.dismiss();
  b.wait();
  for (auto i = 0u; i < iters; i++) {
    fu.futexWake(threads.size());
  }
  fu.store(1);
  done = true;
  fu.futexWake(threads.size());

  for (auto& t : threads) {
    t.join();
  }
}

BENCHMARK_RELATIVE(ParkingLotWakeAll, iters) {
  BenchmarkSuspender susp;
  SimpleBarrier b(FLAGS_threads + 1);
  std::atomic<bool> done{false};

  std::vector<std::thread> threads{FLAGS_threads};
  for (auto& t : threads) {
    t = std::thread([&]() {
      b.wait();
      while (true) {
        Unit f;
        lot.park(
            &done,
            f,
            [&] { return done.load(std::memory_order_relaxed) == 0; },
            [] {});
        if (done.load(std::memory_order_relaxed)) {
          return;
        }
      }
    });
  }
  susp.dismiss();
  b.wait();
  for (auto i = 0u; i < iters; i++) {
    lot.unpark(&done, [](Unit) { return UnparkControl::RemoveContinue; });
  }
  done = true;
  lot.unpark(&done, [](Unit) { return UnparkControl::RemoveContinue; });

  for (auto& t : threads) {
    t.join();
  }
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  folly::runBenchmarks();
}
/*

./buck-out/gen/folly/synchronization/test/parking_lot_test --benchmark
--bm_min_iters=10000 --threads=4
============================================================================
folly/synchronization/test/ParkingLotBenchmark.cpprelative  time/iter  iters/s
============================================================================
FutexNoWaitersWake                                         163.43ns    6.12M
ParkingLotNoWaitersWake                           29.64%   551.43ns    1.81M
FutexWakeOne                                               156.78ns    6.38M
ParkingLotWakeOne                                 37.49%   418.21ns    2.39M
FutexWakeAll                                                 1.82us  549.52K
ParkingLotWakeAll                                449.63%   404.73ns    2.47M
============================================================================

./buck-out/gen/folly/synchronization/test/parking_lot_test --benchmark
--bm_min_iters=10000 --threads=32
============================================================================
folly/synchronization/test/ParkingLotBenchmark.cpprelative  time/iter  iters/s
============================================================================
FutexNoWaitersWake                                         379.59ns    2.63M
ParkingLotNoWaitersWake                            7.94%     4.78us  209.08K
FutexWakeOne                                               163.59ns    6.11M
ParkingLotWakeOne                                  6.41%     2.55us  392.07K
FutexWakeAll                                                12.46us   80.27K
ParkingLotWakeAll                                784.76%     1.59us  629.92K
============================================================================ */
