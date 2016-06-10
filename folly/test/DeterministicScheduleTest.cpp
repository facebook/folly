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

#include <folly/test/DeterministicSchedule.h>

#include <gtest/gtest.h>

#include <folly/portability/GFlags.h>

using namespace folly::test;

TEST(DeterministicSchedule, uniform) {
  auto p = DeterministicSchedule::uniform(0);
  int buckets[10] = {};
  for (int i = 0; i < 100000; ++i) {
    buckets[p(10)]++;
  }
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(buckets[i] > 9000);
  }
}

TEST(DeterministicSchedule, uniformSubset) {
  auto ps = DeterministicSchedule::uniformSubset(0, 3, 100);
  int buckets[10] = {};
  std::set<int> seen;
  for (int i = 0; i < 100000; ++i) {
    if (i > 0 && (i % 100) == 0) {
      EXPECT_EQ(seen.size(), 3);
      seen.clear();
    }
    int x = ps(10);
    seen.insert(x);
    EXPECT_TRUE(seen.size() <= 3);
    buckets[x]++;
  }
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(buckets[i] > 9000);
  }
}

TEST(DeterministicSchedule, buggyAdd) {
  for (bool bug : {false, true}) {
    DeterministicSchedule sched(DeterministicSchedule::uniform(0));
    if (bug) {
      FOLLY_TEST_DSCHED_VLOG("Test with race condition");
    } else {
      FOLLY_TEST_DSCHED_VLOG("Test without race condition");
    }
    DeterministicMutex m;
    // The use of DeterinisticAtomic is not needed here, but it makes
    // it easier to understand the sequence of events in logs.
    DeterministicAtomic<int> test{0};
    DeterministicAtomic<int> baseline{0};
    int numThreads = 10;
    std::vector<std::thread> threads(numThreads);
    for (int t = 0; t < numThreads; ++t) {
      threads[t] = DeterministicSchedule::thread([&, t] {
        baseline.fetch_add(1);
        // Atomic increment of test protected by mutex m
        do {
          // Some threads use lock() others use try_lock()
          if ((t & 1) == 0) {
            m.lock();
          } else {
            if (!m.try_lock()) {
              continue;
            }
          }
          int newval = test.load() + 1;
          if (bug) {
            // Break the atomicity of the increment operation
            m.unlock();
            m.lock();
          }
          test.store(newval);
          m.unlock();
          break;
        } while (true);
      }); // thread lambda
    } // for t
    for (auto& t : threads) {
      DeterministicSchedule::join(t);
    }
    if (!bug) {
      EXPECT_EQ(test.load(), baseline.load());
    } else {
      if (test.load() == baseline.load()) {
        FOLLY_TEST_DSCHED_VLOG("Didn't catch the bug");
      } else {
        FOLLY_TEST_DSCHED_VLOG("Caught the bug");
      }
    }
  } // for bug
} // TEST

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
