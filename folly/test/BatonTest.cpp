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

// I am omitting a benchmark result snapshot because these microbenchmarks
// mainly illustrate that PreBlockAttempts is very effective for rapid
// handoffs.  The performance of Baton and sem_t is essentially identical
// to the required futex calls for the blocking case

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto rv = RUN_ALL_TESTS();
  if (!rv && FLAGS_benchmark) {
    folly::runBenchmarks();
  }
  return rv;
}
