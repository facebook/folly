/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Baton.h>
#include <folly/test/BatonTestHelpers.h>
#include <folly/test/DeterministicSchedule.h>
#include <folly/portability/GTest.h>

#include <thread>
#include <semaphore.h>

using namespace folly;
using namespace folly::test;
using folly::detail::EmulatedFutexAtomic;

TEST(Baton, basic) {
  Baton<> b;
  b.post();
  b.wait();
}

TEST(Baton, pingpong) {
  DSched sched(DSched::uniform(0));

  run_pingpong_test<DeterministicAtomic>(1000);
}

TEST(Baton, timed_wait_basic_system_clock) {
  run_basic_timed_wait_tests<std::atomic, std::chrono::system_clock>();
  run_basic_timed_wait_tests<EmulatedFutexAtomic, std::chrono::system_clock>();
  run_basic_timed_wait_tests<DeterministicAtomic, std::chrono::system_clock>();
}

TEST(Baton, timed_wait_timeout_system_clock) {
  run_timed_wait_tmo_tests<std::atomic, std::chrono::system_clock>();
  run_timed_wait_tmo_tests<EmulatedFutexAtomic, std::chrono::system_clock>();
  run_timed_wait_tmo_tests<DeterministicAtomic, std::chrono::system_clock>();
}

TEST(Baton, timed_wait_system_clock) {
  run_timed_wait_regular_test<std::atomic, std::chrono::system_clock>();
  run_timed_wait_regular_test<EmulatedFutexAtomic, std::chrono::system_clock>();
  run_timed_wait_regular_test<DeterministicAtomic, std::chrono::system_clock>();
}

TEST(Baton, timed_wait_basic_steady_clock) {
  run_basic_timed_wait_tests<std::atomic, std::chrono::steady_clock>();
  run_basic_timed_wait_tests<EmulatedFutexAtomic, std::chrono::steady_clock>();
  run_basic_timed_wait_tests<DeterministicAtomic, std::chrono::steady_clock>();
}

TEST(Baton, timed_wait_timeout_steady_clock) {
  run_timed_wait_tmo_tests<std::atomic, std::chrono::steady_clock>();
  run_timed_wait_tmo_tests<EmulatedFutexAtomic, std::chrono::steady_clock>();
  run_timed_wait_tmo_tests<DeterministicAtomic, std::chrono::steady_clock>();
}

TEST(Baton, timed_wait_steady_clock) {
  run_timed_wait_regular_test<std::atomic, std::chrono::steady_clock>();
  run_timed_wait_regular_test<EmulatedFutexAtomic, std::chrono::steady_clock>();
  run_timed_wait_regular_test<DeterministicAtomic, std::chrono::steady_clock>();
}

TEST(Baton, try_wait) {
  run_try_wait_tests<std::atomic>();
  run_try_wait_tests<EmulatedFutexAtomic>();
  run_try_wait_tests<DeterministicAtomic>();
}
