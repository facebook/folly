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

#include <folly/Baton.h>
#include <folly/test/DeterministicSchedule.h>
#include <thread>
#include <semaphore.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <folly/Benchmark.h>

using namespace folly;
using namespace folly::test;

typedef DeterministicSchedule DSched;

TEST(Baton, basic) {
  Baton<> b;
  b.post();
  b.wait();
}

template <template<typename> class Atom>
void run_pingpong_test(int numRounds) {
  Baton<Atom> batons[17];
  Baton<Atom>& a = batons[0];
  Baton<Atom>& b = batons[16]; // to get it on a different cache line
  auto thr = DSched::thread([&]{
    for (int i = 0; i < numRounds; ++i) {
      a.wait();
      a.reset();
      b.post();
    }
  });
  for (int i = 0; i < numRounds; ++i) {
    a.post();
    b.wait();
    b.reset();
  }
  DSched::join(thr);
}

TEST(Baton, pingpong) {
  DSched sched(DSched::uniform(0));

  run_pingpong_test<DeterministicAtomic>(1000);
}

BENCHMARK(baton_pingpong, iters) {
  run_pingpong_test<std::atomic>(iters);
}

BENCHMARK(posix_sem_pingpong, iters) {
  sem_t sems[3];
  sem_t* a = sems + 0;
  sem_t* b = sems + 2; // to get it on a different cache line

  sem_init(a, 0, 0);
  sem_init(b, 0, 0);
  auto thr = std::thread([=]{
    for (int i = 0; i < iters; ++i) {
      sem_wait(a);
      sem_post(b);
    }
  });
  for (int i = 0; i < iters; ++i) {
    sem_post(a);
    sem_wait(b);
  }
  thr.join();
}

template <template<typename> class Atom>
void run_basic_timed_wait_tests() {
  Baton<Atom> b;
  b.post();
  // tests if early delivery works fine
  EXPECT_TRUE(b.timed_wait(std::chrono::system_clock::now()));
}

template <template<typename> class Atom>
void run_timed_wait_tmo_tests() {
  Baton<Atom> b;

  auto thr = DSched::thread([&]{
    bool rv = b.timed_wait(std::chrono::system_clock::now() +
                           std::chrono::milliseconds(1));
    // main thread is guaranteed to not post until timeout occurs
    EXPECT_FALSE(rv);
  });
  DSched::join(thr);
}

template <template<typename> class Atom>
void run_timed_wait_regular_test() {
  Baton<Atom> b;

  auto thr = DSched::thread([&] {
    bool rv = b.timed_wait(
                std::chrono::time_point<std::chrono::system_clock>::max());
    if (std::is_same<Atom<int>, std::atomic<int>>::value) {
      // We can only ensure this for std::atomic
      EXPECT_TRUE(rv);
    }
  });

  if (std::is_same<Atom<int>, std::atomic<int>>::value) {
    // If we are using std::atomic, then a sleep here guarantees to a large
    // extent that 'thr' will execute wait before we post it, thus testing
    // late delivery. For DeterministicAtomic, we just rely on
    // DeterministicSchedule to do the scheduling
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  b.post();
  DSched::join(thr);
}

TEST(Baton, timed_wait_basic) {
  run_basic_timed_wait_tests<std::atomic>();
  run_basic_timed_wait_tests<DeterministicAtomic>();
}

TEST(Baton, timed_wait_timeout) {
  run_timed_wait_tmo_tests<std::atomic>();
  run_timed_wait_tmo_tests<DeterministicAtomic>();
}

TEST(Baton, timed_wait) {
  run_timed_wait_regular_test<std::atomic>();
  run_timed_wait_regular_test<DeterministicAtomic>();
}

template <template<typename> class Atom>
void run_try_wait_tests() {
  Baton<Atom> b;
  EXPECT_FALSE(b.try_wait());
  b.post();
  EXPECT_TRUE(b.try_wait());
}

TEST(Baton, try_wait) {
  run_try_wait_tests<std::atomic>();
  run_try_wait_tests<DeterministicAtomic>();
}

// I am omitting a benchmark result snapshot because these microbenchmarks
// mainly illustrate that PreBlockAttempts is very effective for rapid
// handoffs.  The performance of Baton and sem_t is essentially identical
// to the required futex calls for the blocking case

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto rv = RUN_ALL_TESTS();
  if (!rv && FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return rv;
}
