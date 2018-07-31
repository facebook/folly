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

#include <boost/thread.hpp>
#include <folly/Random.h>
#include <folly/SpinLock.h>
#include <folly/experimental/ConcurrentPriorityQueue.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/test/DeterministicSchedule.h>
#include <glog/logging.h>

DEFINE_int32(reps, 1, "number of reps");
DEFINE_int64(ops, 32, "number of operations per rep");
DEFINE_int64(elems, 64, "number of elements");

static std::vector<int> nthr = {1, 2, 4, 8};
// threads number
static uint32_t nthreads;

template <class PriorityQueue>
void basicOpsTest() {
  int res;
  PriorityQueue pq;

  EXPECT_TRUE(pq.empty());
  EXPECT_EQ(pq.size(), 0);
  pq.push(1);
  pq.push(2);
  pq.push(3);

  EXPECT_FALSE(pq.empty());
  EXPECT_EQ(pq.size(), 3);
  pq.pop(res);
  EXPECT_EQ(res, 3);
  pq.pop(res);
  EXPECT_EQ(res, 2);
  pq.pop(res);
  EXPECT_EQ(res, 1);
  EXPECT_TRUE(pq.empty());
  EXPECT_EQ(pq.size(), 0);

  pq.push(3);
  pq.push(2);
  pq.push(1);

  pq.pop(res);
  EXPECT_EQ(res, 3);
  pq.pop(res);
  EXPECT_EQ(res, 2);
  pq.pop(res);
  EXPECT_EQ(res, 1);
  EXPECT_TRUE(pq.empty());
  EXPECT_EQ(pq.size(), 0);
}

TEST(CPQ, BasicOpsTest) {
  // Spinning
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true>>();
  // Strict
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 0>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 0, 1>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 0, 3>>();
  // Relaxed
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 1, 1>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 2, 1>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, false, true, 3, 3>>();
  // Blocking
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, true, true>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, true, true, 0>>();
  basicOpsTest<RelaxedConcurrentPriorityQueue<int, true, true, 2>>();
}

/// execute the function for nthreads
template <typename Func>
static void run_once(const Func& fn) {
  boost::barrier barrier_start{nthreads + 1};
  std::vector<std::thread> threads(nthreads);
  for (uint32_t tid = 0; tid < nthreads; ++tid) {
    threads[tid] = std::thread([&, tid] {
      barrier_start.wait();
      fn(tid);
    });
  }

  barrier_start.wait(); // start the execution
  for (auto& t : threads) {
    t.join();
  }
}

template <class PriorityQueue>
void singleThreadTest() {
  PriorityQueue pq;

  folly::Random::DefaultGenerator rng;
  rng.seed(FLAGS_elems);
  uint64_t expect_sum = 0;
  // random push
  for (int i = 0; i < FLAGS_elems; i++) {
    int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
    expect_sum += val;
    pq.push(val);
  }

  int val = 0;
  int counter = 0;
  uint64_t sum = 0;
  while (counter < FLAGS_elems) {
    pq.pop(val);
    sum += val;
    counter++;
  }

  EXPECT_EQ(sum, expect_sum);
}

TEST(CPQ, SingleThrStrictImplTest) {
  // spinning
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 0>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 0, 1>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 0, 8>>();
  // blocking
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, true, false, 0>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, true, false, 0, 1>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, true, false, 0, 8>>();
}

TEST(CPQ, SingleThrRelaxedImplTest) {
  // spinning
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 1>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 8>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 8, 2>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 8, 8>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 2, 128>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, false, false, 100, 8>>();
  // blocking
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, true, false, 1>>();
  singleThreadTest<RelaxedConcurrentPriorityQueue<int, true, false, 8>>();
}

/// concurrent pop should made the queue empty
/// with executing the eaqual elements pop function
template <class PriorityQueue>
void concurrentPopforSharedBuffer() {
  for (int t : nthr) {
    PriorityQueue pq;

    folly::Random::DefaultGenerator rng;
    rng.seed(FLAGS_elems);
    uint64_t check_sum = 0;
    // random push
    for (int i = 0; i < FLAGS_elems; i++) {
      int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
      pq.push(val);
      check_sum += val;
    }

    std::atomic<uint64_t> pop_sum(0);
    std::atomic<int> to_end(0);

    nthreads = t;
    auto fn = [&](uint32_t tid) {
      int val = tid;
      while (true) {
        int index = to_end.fetch_add(1, std::memory_order_acq_rel);
        if (index < FLAGS_elems) {
          pq.pop(val);
        } else {
          break;
        }
        pop_sum.fetch_add(val, std::memory_order_acq_rel);
      }
    };
    run_once(fn);
    // check the sum of returned values of successful pop
    EXPECT_EQ(pop_sum, check_sum);
  }
}

TEST(CPQ, ConcurrentPopStrictImplTest) {
  // spinning
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 0>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 0, 1>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 0, 128>>();
  // blocking
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 0>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 0, 1>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 0, 128>>();
}

TEST(CPQ, ConcurrentPopRelaxedImplTest) {
  // spinning
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 1, 8>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 8, 2>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, false, false, 128, 2>>();
  // blocking
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 1>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 128>>();
  concurrentPopforSharedBuffer<
      RelaxedConcurrentPriorityQueue<int, true, false, 8, 128>>();
}

/// executing fixed number of push, counting the
/// element number & total value.
template <class PriorityQueue>
void concurrentPush() {
  for (int t : nthr) {
    PriorityQueue pq;
    std::atomic<uint64_t> counter_sum(0);
    nthreads = t;

    auto fn = [&](uint32_t tid) {
      folly::Random::DefaultGenerator rng;
      rng.seed(tid);
      uint64_t local_sum = 0;
      for (int i = tid; i < FLAGS_elems; i += nthreads) {
        int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
        local_sum += val;
        pq.push(val);
      }
      counter_sum.fetch_add(local_sum, std::memory_order_acq_rel);
    };
    run_once(fn);

    // check the total number of elements
    uint64_t actual_sum = 0;
    for (int i = 0; i < FLAGS_elems; i++) {
      int res = 0;
      pq.pop(res);
      actual_sum += res;
    }
    EXPECT_EQ(actual_sum, counter_sum);
  }
}

TEST(CPQ, ConcurrentPushStrictImplTest) {
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 0>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 0, 8>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 0, 128>>();
}

TEST(CPQ, ConcurrentPushRelaxedImplTest) {
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 1, 8>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 2, 128>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 128, 8>>();
  concurrentPush<RelaxedConcurrentPriorityQueue<int, false, false, 8, 8>>();
}

template <class PriorityQueue>
void concurrentOps(int ops) {
  for (int t : nthr) {
    PriorityQueue pq;
    std::atomic<uint64_t> counter_push(0);
    std::atomic<uint64_t> counter_pop(0);
    nthreads = t;

    boost::barrier rb0{nthreads};
    boost::barrier rb1{nthreads};
    boost::barrier rb2{nthreads};
    std::atomic<int> to_end(0);

    auto fn = [&](uint32_t tid) {
      folly::Random::DefaultGenerator rng;
      rng.seed(tid);
      uint64_t local_push = 0;
      uint64_t local_pop = 0;
      int res;

      /// initialize the queue
      for (int i = tid; i < FLAGS_elems; i += nthreads) {
        int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
        local_push++;
        pq.push(val);
      }
      rb0.wait();

      /// operations
      for (int i = 0; i < ops; i++) {
        if (ops % 2 == 0) {
          int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
          local_push++;
          pq.push(val);
        } else {
          pq.pop(res);
          local_pop++;
        }
      }
      rb1.wait();
      // collecting the ops info for checking purpose
      counter_push.fetch_add(local_push, std::memory_order_seq_cst);
      counter_pop.fetch_add(local_pop, std::memory_order_seq_cst);
      rb2.wait();
      /// clean up work
      uint64_t r = counter_push.load(std::memory_order_seq_cst) -
          counter_pop.load(std::memory_order_seq_cst);
      while (true) {
        uint64_t index = to_end.fetch_add(1, std::memory_order_acq_rel);
        if (index < r) {
          pq.pop(res);
        } else {
          break;
        }
      }
    };
    run_once(fn);
    // the total push and pop ops should be the same
  }
}

template <class PriorityQueue>
void concurrentSizeTest(int ops) {
  for (int t : nthr) {
    PriorityQueue pq;
    nthreads = t;
    EXPECT_TRUE(pq.empty());
    auto fn = [&](uint32_t tid) {
      folly::Random::DefaultGenerator rng;
      rng.seed(tid);
      uint64_t local_push = 0;
      int res;

      /// initialize the queue
      for (int i = tid; i < FLAGS_elems; i += nthreads) {
        int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
        local_push++;
        pq.push(val);
      }

      /// operations
      for (int i = 0; i < ops; i++) {
        int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
        pq.push(val);
        pq.pop(res);
      }
    };
    run_once(fn);
    // the total push and pop ops should be the same
    EXPECT_EQ(pq.size(), FLAGS_elems);
  }
}

static std::vector<int> sizes = {0, 1024};

TEST(CPQ, ConcurrentMixedStrictImplTest) {
  for (auto size : sizes) {
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 0>>(size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 0, 1>>(
        size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 0, 32>>(
        size);
  }
}

TEST(CPQ, ConcurrentMixedRelaxedImplTest) {
  for (auto size : sizes) {
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false>>(size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 1, 32>>(
        size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 32, 1>>(
        size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, false, false, 8, 8>>(
        size);
    concurrentOps<RelaxedConcurrentPriorityQueue<int, true, false, 8, 8>>(size);
  }
}

TEST(CPQ, StrictImplSizeTest) {
  for (auto size : sizes) {
    concurrentSizeTest<RelaxedConcurrentPriorityQueue<int, false, true, 0>>(
        size);
    concurrentSizeTest<RelaxedConcurrentPriorityQueue<int, true, true, 0>>(
        size);
  }
}

TEST(CPQ, RelaxedImplSizeTest) {
  for (auto size : sizes) {
    concurrentSizeTest<RelaxedConcurrentPriorityQueue<int, false, true>>(size);
    concurrentSizeTest<RelaxedConcurrentPriorityQueue<int, true, true, 2, 8>>(
        size);
    concurrentSizeTest<RelaxedConcurrentPriorityQueue<int, false, true, 8, 2>>(
        size);
  }
}

template <class PriorityQueue>
void multiPusherPopper(int PushThr, int PopThr) {
  int ops = FLAGS_ops;
  uint32_t total_threads = PushThr + PopThr;

  PriorityQueue pq;
  std::atomic<uint64_t> sum_push(0);
  std::atomic<uint64_t> sum_pop(0);

  auto fn_popthr = [&](uint32_t tid) {
    for (int i = tid; i < ops; i += PopThr) {
      int val;
      pq.pop(val);
      sum_pop.fetch_add(val, std::memory_order_acq_rel);
    }
  };

  auto fn_pushthr = [&](uint32_t tid) {
    folly::Random::DefaultGenerator rng_t;
    rng_t.seed(tid);
    for (int i = tid; i < ops; i += PushThr) {
      int val = folly::Random::rand32(rng_t);
      pq.push(val);
      sum_push.fetch_add(val, std::memory_order_acq_rel);
    }
  };
  boost::barrier barrier_start{total_threads + 1};

  std::vector<std::thread> threads_push(PushThr);
  for (int tid = 0; tid < PushThr; ++tid) {
    threads_push[tid] = std::thread([&, tid] {
      barrier_start.wait();
      fn_pushthr(tid);
    });
  }
  std::vector<std::thread> threads_pop(PopThr);
  for (int tid = 0; tid < PopThr; ++tid) {
    threads_pop[tid] = std::thread([&, tid] {
      barrier_start.wait();
      fn_popthr(tid);
    });
  }

  barrier_start.wait(); // start the execution
  // begin time measurement
  for (auto& t : threads_push) {
    t.join();
  }
  for (auto& t : threads_pop) {
    t.join();
  }
  EXPECT_EQ(sum_pop, sum_push);
}

TEST(CPQ, PusherPopperBlockingTest) {
  for (auto i : nthr) {
    for (auto j : nthr) {
      // Original
      multiPusherPopper<RelaxedConcurrentPriorityQueue<int, true, false, 0, 1>>(
          i, j);
      // Relaxed
      multiPusherPopper<RelaxedConcurrentPriorityQueue<int, true, false, 1, 8>>(
          i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, true, false, 8, 128>>(i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, true, false, 128, 8>>(i, j);
      multiPusherPopper<RelaxedConcurrentPriorityQueue<int, true, false>>(i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, true, false, 16, 16>>(i, j);
    }
  }
}

TEST(CPQ, PusherPopperSpinningTest) {
  for (auto i : nthr) {
    for (auto j : nthr) {
      // Original
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, false, false, 0, 1>>(i, j);
      // Relaxed
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, false, false, 1, 8>>(i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, false, false, 8, 128>>(i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, false, false, 128, 8>>(i, j);
      multiPusherPopper<RelaxedConcurrentPriorityQueue<int, false, false>>(
          i, j);
      multiPusherPopper<
          RelaxedConcurrentPriorityQueue<int, false, false, 16, 16>>(i, j);
    }
  }
}

template <class PriorityQueue>
void blockingFirst() {
  PriorityQueue pq;
  int nPop = 16;

  boost::barrier b{static_cast<unsigned int>(nPop + 1)};
  std::atomic<int> finished{0};

  std::vector<std::thread> threads_pop(nPop);
  for (int tid = 0; tid < nPop; ++tid) {
    threads_pop[tid] = std::thread([&] {
      int val;
      b.wait();
      pq.pop(val);
      finished.fetch_add(1, std::memory_order_acq_rel);
    });
  }

  b.wait();
  int c = 0;
  // push to Mound one by one
  // the popping threads should wake up one by one
  do {
    pq.push(1);
    c++;
    while (finished.load(std::memory_order_acquire) != c)
      ;
    EXPECT_EQ(finished.load(std::memory_order_acquire), c);
  } while (c < nPop);

  for (auto& t : threads_pop) {
    t.join();
  }
}

template <class PriorityQueue>
void concurrentBlocking() {
  uint32_t nThrs = 16;

  for (int iter = 0; iter < FLAGS_reps * 10; iter++) {
    PriorityQueue pq;
    boost::barrier b{static_cast<unsigned int>(nThrs + nThrs + 1)};
    std::atomic<uint32_t> finished{0};
    std::vector<std::thread> threads_pop(nThrs);
    for (uint32_t tid = 0; tid < nThrs; ++tid) {
      threads_pop[tid] = std::thread([&] {
        b.wait();
        int val;
        pq.pop(val);
        finished.fetch_add(1, std::memory_order_acq_rel);
      });
    }

    std::vector<std::thread> threads_push(nThrs);
    for (uint32_t tid = 0; tid < nThrs; ++tid) {
      threads_push[tid] = std::thread([&, tid] {
        b.wait();
        pq.push(tid);
        while (finished.load(std::memory_order_acquire) != nThrs)
          ;
        EXPECT_EQ(finished.load(std::memory_order_acquire), nThrs);
      });
    }

    b.wait();
    for (auto& t : threads_pop) {
      t.join();
    }
    for (auto& t : threads_push) {
      t.join();
    }
  }
}

TEST(CPQ, PopBlockingTest) {
  // strict
  blockingFirst<RelaxedConcurrentPriorityQueue<int, true, false, 0, 1>>();
  blockingFirst<RelaxedConcurrentPriorityQueue<int, true, false, 0, 16>>();
  // relaxed
  blockingFirst<RelaxedConcurrentPriorityQueue<int, true, false>>();
  blockingFirst<RelaxedConcurrentPriorityQueue<int, true, false, 8, 8>>();
  blockingFirst<RelaxedConcurrentPriorityQueue<int, true, false, 16, 1>>();
  // Spinning
  blockingFirst<RelaxedConcurrentPriorityQueue<int, false, false>>();
  blockingFirst<RelaxedConcurrentPriorityQueue<int, false, false, 8, 8>>();
  blockingFirst<RelaxedConcurrentPriorityQueue<int, false, false, 16, 1>>();
}

TEST(CPQ, MixedBlockingTest) {
  // strict
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, true, false, 0, 1>>();
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, true, false, 0, 16>>();
  // relaxed
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, true, false>>();
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, true, false, 8, 8>>();
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, true, false, 16, 1>>();
  // Spinning
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, false, false>>();
  concurrentBlocking<RelaxedConcurrentPriorityQueue<int, false, false, 8, 8>>();
  concurrentBlocking<
      RelaxedConcurrentPriorityQueue<int, false, false, 16, 1>>();
}

using DSched = folly::test::DeterministicSchedule;
using folly::test::DeterministicAtomic;
using folly::test::DeterministicMutex;

template <class PriorityQueue, template <typename> class Atom = std::atomic>
static void DSchedMixedTest() {
  for (int r = 0; r < FLAGS_reps; r++) {
    // the thread number is  32
    int thr = 8;
    PriorityQueue pq;
    folly::Random::DefaultGenerator rng;
    rng.seed(thr);
    uint64_t pre_sum = 0;
    uint64_t pre_size = 0;
    for (int i = 0; i < FLAGS_elems; i++) {
      int val = folly::Random::rand32(rng) % FLAGS_elems + 1;
      pq.push(val);
      pre_sum += val;
    }
    pre_size = FLAGS_elems;
    Atom<uint64_t> atom_push_sum{0};
    Atom<uint64_t> atom_pop_sum{0};
    std::vector<std::thread> threads(thr);
    for (int tid = 0; tid < thr; ++tid) {
      threads[tid] = DSched::thread([&]() {
        folly::Random::DefaultGenerator tl_rng;
        tl_rng.seed(thr);
        uint64_t pop_sum = 0;
        uint64_t push_sum = 0;
        for (int i = 0; i < FLAGS_ops; i++) {
          int val = folly::Random::rand32(tl_rng) % FLAGS_elems + 1;
          pq.push(val);
          push_sum += val;
          pq.pop(val);
          pop_sum += val;
        }
        atom_push_sum.fetch_add(push_sum, std::memory_order_acq_rel);
        atom_pop_sum.fetch_add(pop_sum, std::memory_order_acq_rel);
      });
    }

    for (auto& t : threads) {
      DSched::join(t);
    }

    // It checks the number of elements remain in Mound
    while (pre_size > 0) {
      pre_size--;
      int val = -1;
      pq.pop(val);
      atom_pop_sum += val;
    }
    // Check the accumulation of popped and pushed priorities
    EXPECT_EQ(atom_pop_sum, pre_sum + atom_push_sum);
  }
}

TEST(CPQ, DSchedMixedStrictTest) {
  DSched sched(DSched::uniform(0));
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          0,
          25,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          0,
          25,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          0,
          1,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          0,
          1,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          0,
          128,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          0,
          128,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
}

TEST(CPQ, DSchedMixedRelaxedTest) {
  DSched sched(DSched::uniform(0));
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          16,
          25,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          16,
          25,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          1,
          16,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          1,
          16,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          16,
          1,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          16,
          1,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          false,
          false,
          16,
          16,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
  DSchedMixedTest<
      RelaxedConcurrentPriorityQueue<
          int,
          true,
          false,
          16,
          16,
          DeterministicMutex,
          DeterministicAtomic>,
      DeterministicAtomic>();
}
