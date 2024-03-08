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

#include <folly/synchronization/LifoSem.h>

#include <thread>

#include <folly/Random.h>
#include <folly/portability/Asm.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/NativeSemaphore.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly;
using namespace folly::test;

typedef LifoSemImpl<DeterministicAtomic> DLifoSem;
typedef DeterministicSchedule DSched;

class LifoSemTest : public testing::Test {
 private:
  // pre-init the pool to avoid deadlock when using DeterministicAtomic
  using Node = detail::LifoSemRawNode<DeterministicAtomic>;
  Node::Pool& pool_{Node::pool()};
};

TEST(LifoSem, basic) {
  LifoSem sem;
  EXPECT_FALSE(sem.tryPost());
  EXPECT_FALSE(sem.tryWait());
  sem.post();
  EXPECT_TRUE(sem.tryWait());
  sem.post();
  sem.wait();
}

TEST(LifoSem, multi) {
  LifoSem sem;

  const int opsPerThread = 10000;
  std::thread threads[10];
  std::atomic<int> blocks(0);

  for (auto& thr : threads) {
    thr = std::thread([&] {
      int b = 0;
      for (int i = 0; i < opsPerThread; ++i) {
        if (!sem.tryWait()) {
          sem.wait();
          ++b;
        }
        sem.post();
      }
      blocks += b;
    });
  }

  // start the flood
  sem.post();

  for (auto& thr : threads) {
    thr.join();
  }

  LOG(INFO) << opsPerThread * sizeof(threads) / sizeof(threads[0])
            << " post/wait pairs, " << blocks << " blocked";
}

TEST_F(LifoSemTest, pingpong) {
  DSched sched(DSched::uniform(0));

  const int iters = 100;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;
    DLifoSem b;

    auto thr = DSched::thread([&] {
      for (int i = 0; i < iters; ++i) {
        a.wait();
        // main thread can't be running here
        EXPECT_EQ(a.valueGuess(), 0);
        EXPECT_EQ(b.valueGuess(), 0);
        b.post();
      }
    });
    for (int i = 0; i < iters; ++i) {
      a.post();
      b.wait();
      // child thread can't be running here
      EXPECT_EQ(a.valueGuess(), 0);
      EXPECT_EQ(b.valueGuess(), 0);
    }
    DSched::join(thr);
  }
}

TEST_F(LifoSemTest, mutex) {
  DSched sched(DSched::uniform(0));

  const int iters = 100;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;

    auto thr = DSched::thread([&] {
      for (int i = 0; i < iters; ++i) {
        a.wait();
        a.post();
      }
    });
    for (int i = 0; i < iters; ++i) {
      a.post();
      a.wait();
    }
    a.post();
    DSched::join(thr);
    a.wait();
  }
}

TEST_F(LifoSemTest, no_blocking) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniform(seed));

  const int iters = 100;
  const int numThreads = 2;
  const int width = 10;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;

    std::vector<std::thread> threads;
    while (threads.size() < numThreads) {
      threads.emplace_back(DSched::thread([&] {
        for (int i = 0; i < iters; ++i) {
          a.post(width);
          for (int w = 0; w < width; ++w) {
            a.wait();
          }
        }
      }));
    }
    for (auto& thr : threads) {
      DSched::join(thr);
    }
  }
}

TEST_F(LifoSemTest, one_way) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniformSubset(seed, 1, 6));

  const int iters = 1000;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;

    auto thr = DSched::thread([&] {
      for (int i = 0; i < iters; ++i) {
        a.wait();
      }
    });
    for (int i = 0; i < iters; ++i) {
      a.post();
    }
    DSched::join(thr);
  }
}

TEST_F(LifoSemTest, shutdown_wait_order) {
  DLifoSem a;
  a.shutdown();
  a.post();
  a.wait();
  EXPECT_THROW(a.wait(), ShutdownSemError);
  EXPECT_TRUE(a.isShutdown());
}

TEST_F(LifoSemTest, shutdown_multi) {
  DSched sched(DSched::uniform(0));

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;
    std::vector<std::thread> threads;
    while (threads.size() < 20) {
      threads.push_back(DSched::thread([&] {
        try {
          a.wait();
          ADD_FAILURE();
        } catch (ShutdownSemError&) {
          // expected
          EXPECT_TRUE(a.isShutdown());
        }
      }));
    }
    a.shutdown();
    for (auto& thr : threads) {
      DSched::join(thr);
    }
  }
}

TEST(LifoSem, multiTryWaitSimple) {
  LifoSem sem;
  sem.post(5);
  auto n = sem.tryWait(10); // this used to trigger an assert
  ASSERT_EQ(5, n);
}

TEST_F(LifoSemTest, multi_try_wait) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniform(seed));
  DLifoSem sem;

  const int NPOSTS = 1000;

  auto producer = [&] {
    for (int i = 0; i < NPOSTS; ++i) {
      sem.post();
    }
  };

  DeterministicAtomic<bool> consumer_stop(false);
  int consumed = 0;

  auto consumer = [&] {
    bool stop;
    do {
      stop = consumer_stop.load();
      int n;
      do {
        n = sem.tryWait(10);
        consumed += n;
      } while (n > 0);
    } while (!stop);
  };

  std::thread producer_thread(DSched::thread(producer));
  std::thread consumer_thread(DSched::thread(consumer));
  DSched::join(producer_thread);
  consumer_stop.store(true);
  DSched::join(consumer_thread);

  ASSERT_EQ(NPOSTS, consumed);
}

TEST_F(LifoSemTest, timeout) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniform(seed));
  DeterministicAtomic<uint32_t> handoffs{0};

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;
    std::vector<std::thread> threads;
    while (threads.size() < 20) {
      threads.push_back(DSched::thread([&] {
        for (int i = 0; i < 10; i++) {
          try {
            if (a.try_wait_for(std::chrono::milliseconds(1))) {
              handoffs--;
            }
          } catch (ShutdownSemError&) {
            // expected
            EXPECT_TRUE(a.isShutdown());
          }
        }
      }));
    }
    std::vector<std::thread> threads2;
    while (threads2.size() < 20) {
      threads2.push_back(DSched::thread([&] {
        for (int i = 0; i < 10; i++) {
          a.post();
          handoffs++;
        }
      }));
    }
    if (pass > 5) {
      a.shutdown();
    }
    for (auto& thr : threads) {
      DSched::join(thr);
    }
    for (auto& thr : threads2) {
      DSched::join(thr);
    }
    // At least one timeout must occur.
    EXPECT_GT(handoffs.load(), 0);
  }
}

TEST_F(LifoSemTest, shutdown_try_wait_for) {
  long seed = folly::randomNumberSeed() % 1000000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniform(seed));

  DLifoSem stopped;
  std::thread worker1 = DSched::thread([&stopped] {
    while (!stopped.isShutdown()) {
      // i.e. poll for messages with timeout
      LOG(INFO) << "thread polled";
    }
  });
  std::thread worker2 = DSched::thread([&stopped] {
    while (!stopped.isShutdown()) {
      // Do some work every 1 second

      try {
        // this is normally 1 second in prod use case.
        stopped.try_wait_for(std::chrono::milliseconds(1));
      } catch (folly::ShutdownSemError&) {
        LOG(INFO) << "try_wait_for shutdown";
      }
    }
  });

  std::thread shutdown = DSched::thread([&stopped] {
    LOG(INFO) << "LifoSem shutdown";
    stopped.shutdown();
    LOG(INFO) << "LifoSem shutdown done";
  });

  DSched::join(shutdown);
  DSched::join(worker1);
  DSched::join(worker2);
  LOG(INFO) << "Threads joined";
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  return rv;
}
