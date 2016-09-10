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

#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

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

/// Test support for auxiliary data and global invariants
///

/// How to use DSched support for auxiliary data and global invariants:
///   1. Forward declare an annotated shared class
///   2. Add the annotated shared class as a friend of the original class(es)
///      to be tested
///   3. Define auxiliary data
///   4. Define function(s) for updating auxiliary data to match shared updates
///   5. Define the annotated shared class
///      It supports an interface to the original class along with auxiliary
///        functions for updating aux data, checking invariants, and/or logging
///      It may have to duplicate the steps of multi-step operations in the
////       original code in order to manage aux data and check invariants after
///        shared accesses other than the first access in an opeeration
///   6. Define function for checking global invariants and/or logging global
///      state
///   7. Define function generator(s) for function object(s) that update aux
///      data, check invariants, and/or log state
///   8. Define TEST using anotated shared data, aux data, and aux functions

using DSched = DeterministicSchedule;

/** forward declaration of annotated shared class */
class AnnotatedAtomicCounter;

/** original shared class to be tested */
template <typename T, template <typename> class Atom = std::atomic>
class AtomicCounter {
  friend AnnotatedAtomicCounter;

 public:
  explicit AtomicCounter(T val) : counter_(val) {}

  void inc() {
    counter_.fetch_add(1);
  }

  void inc_bug() {
    int newval = counter_.load() + 1;
    counter_.store(newval);
  }

  T load() {
    return counter_.load();
  }

 private:
  Atom<T> counter_ = {0};
};

/** auxiliary data */
struct AuxData {
  explicit AuxData(int nthr) {
    local_.resize(nthr, 0);
  }

  std::vector<int> local_;
};

/** aux update function(s) */
void auxUpdateAfterInc(int tid, AuxData& auxdata, bool success) {
  if (success) {
    auxdata.local_[tid]++;
  }
}

/** annotated shared class */
class AnnotatedAtomicCounter {
 public:
  explicit AnnotatedAtomicCounter(int val) : shared_(val) {}

  void inc(AuxAct& auxfn) {
    DSched::setAuxAct(auxfn);
    /* calls the fine-grained original */
    shared_.inc();
  }

  void inc_bug(AuxAct auxfn) {
    /* duplicates the steps of the multi-access original in order to
     * annotate the second access */
    int newval = shared_.counter_.load() + 1;
    DSched::setAuxAct(auxfn);
    shared_.counter_.store(newval);
  }

  int load_direct() {
    return shared_.counter_.load_direct();
  }

 private:
  AtomicCounter<int, DeterministicAtomic> shared_;
};

using Annotated = AnnotatedAtomicCounter;

/** aux log & check function */
void auxCheck(int tid, Annotated& annotated, AuxData& auxdata) {
  /* read shared data */
  int val = annotated.load_direct();
  /* read auxiliary data */
  int sum = 0;
  for (int v : auxdata.local_) {
    sum += v;
  }
  /* log state */
  VLOG(2) << "tid " << tid << " -- shared counter= " << val
          << " -- sum increments= " << sum;
  /* check invariant */
  if (val != sum) {
    LOG(ERROR) << "counter=(" << val << ") expected(" << sum << ")";
    CHECK(false);
  }
}

/** function generator(s) */
AuxAct auxAfterInc(int tid, Annotated& annotated, AuxData& auxdata) {
  return [&annotated, &auxdata, tid](bool success) {
    auxUpdateAfterInc(tid, auxdata, success);
    auxCheck(tid, annotated, auxdata);
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

  Annotated annotated(0);
  AuxData auxdata(nthr);
  DSched sched(DSched::uniform(seed));

  std::vector<std::thread> threads(nthr);
  for (int tid = 0; tid < nthr; ++tid) {
    threads[tid] = DSched::thread([&, tid]() {
      AuxAct auxfn = auxAfterInc(tid, annotated, auxdata);
      for (int i = 0; i < niter; ++i) {
        if (bug && (tid == 0) && (i % 10 == 0)) {
          annotated.inc_bug(auxfn);
        } else {
          annotated.inc(auxfn);
        }
      }
    });
  }
  for (auto& t : threads) {
    DSched::join(t);
  }
  EXPECT_EQ(annotated.load_direct(), nthr * niter);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
