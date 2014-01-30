/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/detail/Futex.h"
#include "folly/test/DeterministicSchedule.h"

#include <chrono>
#include <thread>

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <common/logging/logging.h>
#include <time.h>

using namespace folly::detail;
using namespace folly::test;
using namespace std::chrono;

typedef DeterministicSchedule DSched;

template <template<typename> class Atom>
void run_basic_tests() {
  Futex<Atom> f(0);

  EXPECT_FALSE(f.futexWait(1));
  EXPECT_EQ(f.futexWake(), 0);

  auto thr = DSched::thread([&]{
    EXPECT_TRUE(f.futexWait(0));
  });

  while (f.futexWake() != 1) {
    std::this_thread::yield();
  }

  DSched::join(thr);
}

template<template<typename> class Atom>
void run_wait_until_tests();

template <typename Clock>
void stdAtomicWaitUntilTests() {
  Futex<std::atomic> f(0);

  auto thrA = DSched::thread([&]{
    while (true) {
      typename Clock::time_point nowPlus2s = Clock::now() + seconds(2);
      auto res = f.futexWaitUntil(0, nowPlus2s);
      EXPECT_TRUE(res == FutexResult::TIMEDOUT || res == FutexResult::AWOKEN);
      if (res == FutexResult::AWOKEN) {
        break;
      }
    }
  });

  while (f.futexWake() != 1) {
    std::this_thread::yield();
  }

  DSched::join(thrA);

  auto start = Clock::now();
  EXPECT_EQ(f.futexWaitUntil(0, start + milliseconds(100)),
            FutexResult::TIMEDOUT);
  LOG(INFO) << "Futex wait timed out after waiting for "
            << duration_cast<milliseconds>(Clock::now() - start).count()
            << "ms";
}

template <typename Clock>
void deterministicAtomicWaitUntilTests() {
  Futex<DeterministicAtomic> f(0);

  // Futex wait must eventually fail with either FutexResult::TIMEDOUT or
  // FutexResult::INTERRUPTED
  auto res = f.futexWaitUntil(0, Clock::now() + milliseconds(100));
  EXPECT_TRUE(res == FutexResult::TIMEDOUT || res == FutexResult::INTERRUPTED);
}

template <>
void run_wait_until_tests<std::atomic>() {
  stdAtomicWaitUntilTests<system_clock>();
  stdAtomicWaitUntilTests<steady_clock>();
}

template <>
void run_wait_until_tests<DeterministicAtomic>() {
  deterministicAtomicWaitUntilTests<system_clock>();
  deterministicAtomicWaitUntilTests<steady_clock>();
}

uint64_t diff(uint64_t a, uint64_t b) {
  return a > b ? a - b : b - a;
}

void run_system_clock_test() {
  /* Test to verify that system_clock uses clock_gettime(CLOCK_REALTIME, ...)
   * for the time_points */
  struct timespec ts;
  const int maxIters = 1000;
  int iter = 0;
  uint64_t delta = 10000000 /* 10 ms */;

  /** The following loop is only to make the test more robust in the presence of
   * clock adjustments that can occur. We just run the loop maxIter times and
   * expect with very high probability that there will be atleast one iteration
   * of the test during which clock adjustments > delta have not occurred. */
  while (iter < maxIters) {
    uint64_t a = duration_cast<nanoseconds>(system_clock::now()
                                            .time_since_epoch()).count();

    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t b = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    uint64_t c = duration_cast<nanoseconds>(system_clock::now()
                                            .time_since_epoch()).count();

    if (diff(a, b) <= delta &&
        diff(b, c) <= delta &&
        diff(a, c) <= 2 * delta) {
      /* Success! system_clock uses CLOCK_REALTIME for time_points */
      break;
    }
    iter++;
  }
  EXPECT_TRUE(iter < maxIters);
}

void run_steady_clock_test() {
  /* Test to verify that steady_clock uses clock_gettime(CLOCK_MONOTONIC, ...)
   * for the time_points */
  EXPECT_TRUE(steady_clock::is_steady);

  uint64_t A = duration_cast<nanoseconds>(steady_clock::now()
                                          .time_since_epoch()).count();

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t B = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

  uint64_t C = duration_cast<nanoseconds>(steady_clock::now()
                                          .time_since_epoch()).count();
  EXPECT_TRUE(A <= B && B <= C);
}

TEST(Futex, clock_source) {
  run_system_clock_test();

  /* On some systems steady_clock is just an alias for system_clock. So,
   * we must skip run_steady_clock_test if the two clocks are the same. */
  if (!std::is_same<system_clock,steady_clock>::value) {
    run_steady_clock_test();
  }
}

TEST(Futex, basic_live) {
  run_basic_tests<std::atomic>();
  run_wait_until_tests<std::atomic>();
}

TEST(Futex, basic_deterministic) {
  DSched sched(DSched::uniform(0));
  run_basic_tests<DeterministicAtomic>();
  run_wait_until_tests<DeterministicAtomic>();
}

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

