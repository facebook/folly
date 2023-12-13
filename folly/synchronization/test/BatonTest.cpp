/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/synchronization/Baton.h>

#include <thread>

#include <folly/portability/GTest.h>
#include <folly/synchronization/test/BatonTestHelpers.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly::test;
using folly::detail::EmulatedFutexAtomic;
using std::chrono::steady_clock;
using std::chrono::system_clock;

/// Basic test

TEST(Baton, basicBlocking) {
  run_basic_test<true, std::atomic>();
  run_basic_test<true, EmulatedFutexAtomic>();
  run_basic_test<true, DeterministicAtomic>();
}

TEST(Baton, basicNonblocking) {
  run_basic_test<false, std::atomic>();
  run_basic_test<false, EmulatedFutexAtomic>();
  run_basic_test<false, DeterministicAtomic>();
}

/// Ping pong tests

TEST(Baton, pingpongBlocking) {
  DSched sched(DSched::uniform(0));

  run_pingpong_test<true, DeterministicAtomic>(1000);
}

TEST(Baton, pingpongNonblocking) {
  DSched sched(DSched::uniform(0));

  run_pingpong_test<false, DeterministicAtomic>(1000);
}

// Timed wait basic system clock tests

TEST(Baton, timedWaitBasicSystemClockBlocking) {
  run_basic_timed_wait_tests<true, std::atomic, system_clock>();
  run_basic_timed_wait_tests<true, EmulatedFutexAtomic, system_clock>();
  run_basic_timed_wait_tests<true, DeterministicAtomic, system_clock>();
}

TEST(Baton, timedWaitBasicSystemClockNonblocking) {
  run_basic_timed_wait_tests<false, std::atomic, system_clock>();
  run_basic_timed_wait_tests<false, EmulatedFutexAtomic, system_clock>();
  run_basic_timed_wait_tests<false, DeterministicAtomic, system_clock>();
}

// Timed wait timeout system clock tests

TEST(Baton, timedWaitTimeoutSystemClockBlocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_tmo_tests<true, std::atomic, system_clock>();
  run_timed_wait_tmo_tests<true, EmulatedFutexAtomic, system_clock>();
  run_timed_wait_tmo_tests<true, DeterministicAtomic, system_clock>();
}

TEST(Baton, timedWaitTimeoutSystemClockNonblocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_tmo_tests<false, std::atomic, system_clock>();
  run_timed_wait_tmo_tests<false, EmulatedFutexAtomic, system_clock>();
  run_timed_wait_tmo_tests<false, DeterministicAtomic, system_clock>();
}

// Timed wait regular system clock tests

TEST(Baton, timedWaitSystemClockBlocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_regular_test<true, std::atomic, system_clock>();
  run_timed_wait_regular_test<true, EmulatedFutexAtomic, system_clock>();
  run_timed_wait_regular_test<true, DeterministicAtomic, system_clock>();
}

TEST(Baton, timedWaitSystemClockNonblocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_regular_test<false, std::atomic, system_clock>();
  run_timed_wait_regular_test<false, EmulatedFutexAtomic, system_clock>();
  run_timed_wait_regular_test<false, DeterministicAtomic, system_clock>();
}

// Timed wait basic steady clock tests

TEST(Baton, timedWaitBasicSteadyClockBlocking) {
  DSched sched(DSched::uniform(0));
  run_basic_timed_wait_tests<true, std::atomic, steady_clock>();
  run_basic_timed_wait_tests<true, EmulatedFutexAtomic, steady_clock>();
  run_basic_timed_wait_tests<true, DeterministicAtomic, steady_clock>();
}

TEST(Baton, timedWaitBasicSteadyClockNonblocking) {
  DSched sched(DSched::uniform(0));
  run_basic_timed_wait_tests<false, std::atomic, steady_clock>();
  run_basic_timed_wait_tests<false, EmulatedFutexAtomic, steady_clock>();
  run_basic_timed_wait_tests<false, DeterministicAtomic, steady_clock>();
}

// Timed wait timeout steady clock tests

TEST(Baton, timedWaitTimeoutSteadyClockBlocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_tmo_tests<true, std::atomic, steady_clock>();
  run_timed_wait_tmo_tests<true, EmulatedFutexAtomic, steady_clock>();
  run_timed_wait_tmo_tests<true, DeterministicAtomic, steady_clock>();
}

TEST(Baton, timedWaitTimeoutSteadyClockNonblocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_tmo_tests<false, std::atomic, steady_clock>();
  run_timed_wait_tmo_tests<false, EmulatedFutexAtomic, steady_clock>();
  run_timed_wait_tmo_tests<false, DeterministicAtomic, steady_clock>();
}

// Timed wait regular steady clock tests

TEST(Baton, timedWaitSteadyClockBlocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_regular_test<true, std::atomic, steady_clock>();
  run_timed_wait_regular_test<true, EmulatedFutexAtomic, steady_clock>();
  run_timed_wait_regular_test<true, DeterministicAtomic, steady_clock>();
}

TEST(Baton, timedWaitSteadyClockNonblocking) {
  DSched sched(DSched::uniform(0));
  run_timed_wait_regular_test<false, std::atomic, steady_clock>();
  run_timed_wait_regular_test<false, EmulatedFutexAtomic, steady_clock>();
  run_timed_wait_regular_test<false, DeterministicAtomic, steady_clock>();
}

/// Try wait tests

TEST(Baton, tryWaitBlocking) {
  run_try_wait_tests<true, std::atomic>();
  run_try_wait_tests<true, EmulatedFutexAtomic>();
  run_try_wait_tests<true, DeterministicAtomic>();
}

TEST(Baton, tryWaitNonblocking) {
  run_try_wait_tests<false, std::atomic>();
  run_try_wait_tests<false, EmulatedFutexAtomic>();
  run_try_wait_tests<false, DeterministicAtomic>();
}
