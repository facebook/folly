/*
 * Copyright 2013 Facebook, Inc.
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

#include <thread>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

using namespace folly::detail;
using namespace folly::test;

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


TEST(Futex, basic_live) {
  run_basic_tests<std::atomic>();
}

TEST(Futex, basic_deterministic) {
  DSched sched(DSched::uniform(0));
  run_basic_tests<DeterministicAtomic>();
}

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

