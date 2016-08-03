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

/// Testing support for auxiliary variables and global invariants

/** auxiliary variables for atomic counter test */
struct AtomicCounterAux {
  std::vector<int> local_;

  explicit AtomicCounterAux(int nthr) {
    local_.resize(nthr, 0);
  }
};

/** auxiliary function for checking global invariants and logging
 *  steps of atomic counter test */
void checkAtomicCounter(
    int tid,
    uint64_t step,
    DeterministicAtomic<int>& shared,
    AtomicCounterAux& aux) {
  /* read shared data */
  int val = shared.load_direct();
  /* read auxiliary variables */
  int sum = 0;
  for (int v : aux.local_) {
    sum += v;
  }
  /* log state */
  VLOG(2) << "Step " << step << " -- tid " << tid << " -- shared counter "
          << val << " -- sum increments " << sum;
  /* check invariant */
  if (val != sum) {
    LOG(ERROR) << "Failed after step " << step;
    LOG(ERROR) << "counter=(" << val << ") expected(" << sum << ")";
    CHECK(false);
  }
}

std::function<void(uint64_t, bool)> auxAtomicCounter(
    DeterministicAtomic<int>& shared,
    AtomicCounterAux& aux,
    int tid) {
  return [&shared, &aux, tid](uint64_t step, bool success) {
    // update auxiliary data
    if (success) {
      aux.local_[tid]++;
    }
    // check invariants
    checkAtomicCounter(tid, step, shared, aux);
  };
}

DEFINE_bool(bug, false, "Introduce bug");
DEFINE_int64(seed, 0, "Seed for random number generator");
DEFINE_int32(num_threads, 2, "Number of threads");
DEFINE_int32(num_iterations, 10, "Number of iterations");

TEST(DSchedCustom, atomic_add) {
  bool bug = FLAGS_bug;
  long seed = FLAGS_seed;
  int nthr = FLAGS_num_threads;
  int niter = FLAGS_num_iterations;

  CHECK_GT(nthr, 0);

  DeterministicAtomic<int> counter{0};
  AtomicCounterAux auxData(nthr);
  DeterministicSchedule sched(DeterministicSchedule::uniform(seed));

  std::vector<std::thread> threads(nthr);
  for (int tid = 0; tid < nthr; ++tid) {
    threads[tid] = DeterministicSchedule::thread([&, tid]() {
      auto auxFn = auxAtomicCounter(counter, auxData, tid);
      for (int i = 0; i < niter; ++i) {
        if (bug && (tid == 0) && (i % 10 == 0)) {
          int newval = counter.load() + 1;
          DeterministicSchedule::setAux(auxFn);
          counter.store(newval);
        } else {
          DeterministicSchedule::setAux(auxFn);
          counter.fetch_add(1);
        }
      }
    });
  }
  for (auto& t : threads) {
    DeterministicSchedule::join(t);
  }
  EXPECT_EQ(counter.load_direct(), nthr * niter);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
