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

#include <folly/LifoSem.h>
#include <folly/test/DeterministicSchedule.h>

#include <thread>
#include <semaphore.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/Random.h>

using namespace folly;
using namespace folly::test;

typedef LifoSemImpl<DeterministicAtomic> DLifoSem;
typedef DeterministicSchedule DSched;

LIFOSEM_DECLARE_POOL(DeterministicAtomic, 100000)

TEST(LifoSem, basic) {
  LifoSem sem;
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
    thr = std::thread([&]{
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

  LOG(INFO) << opsPerThread * sizeof(threads)/sizeof(threads[0])
            << " post/wait pairs, " << blocks << " blocked";
}

TEST(LifoSem, pingpong) {
  DSched sched(DSched::uniform(0));

  const int iters = 100;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;
    DLifoSem b;

    auto thr = DSched::thread([&]{
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

TEST(LifoSem, mutex) {
  DSched sched(DSched::uniform(0));

  const int iters = 100;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;

    auto thr = DSched::thread([&]{
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

TEST(LifoSem, no_blocking) {
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
      threads.emplace_back(DSched::thread([&]{
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

TEST(LifoSem, one_way) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniformSubset(seed, 1, 6));

  const int iters = 1000;

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;

    auto thr = DSched::thread([&]{
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

TEST(LifoSem, shutdown_race) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;

  bool shutdownWon = false;
  bool shutdownLost = false;
  for (int pass = 0; pass < 1000; ++pass) {
    DSched sched(DSched::uniformSubset(seed + pass, 1, 1 + (pass % 50)));

    DLifoSem a;
    int waitCount = 0;
    auto thr = DSched::thread([&]{
      try {
        while (true) {
          a.wait();
          ++waitCount;
        }
      } catch (ShutdownSemError& x) {
        // expected
        EXPECT_TRUE(a.isShutdown());
      }
    });
    EXPECT_TRUE(!a.isShutdown());
    a.shutdown();
    EXPECT_TRUE(a.isShutdown());
    a.post();
    DSched::join(thr);
    EXPECT_EQ(1, waitCount + a.valueGuess());
    if (waitCount == 0) {
      shutdownWon = true;
    } else {
      shutdownLost = true;
    }
  }
  EXPECT_TRUE(shutdownWon);
  EXPECT_TRUE(shutdownLost);
}

TEST(LifoSem, shutdown_multi) {
  DSched sched(DSched::uniform(0));

  for (int pass = 0; pass < 10; ++pass) {
    DLifoSem a;
    std::vector<std::thread> threads;
    while (threads.size() < 20) {
      threads.push_back(DSched::thread([&]{
        try {
          a.wait();
          EXPECT_TRUE(false);
        } catch (ShutdownSemError& x) {
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

TEST(LifoSem, multi_try_wait_simple) {
  LifoSem sem;
  sem.post(5);
  auto n = sem.tryWait(10);     // this used to trigger an assert
  ASSERT_EQ(5, n);
}

TEST(LifoSem, multi_try_wait) {
  long seed = folly::randomNumberSeed() % 10000;
  LOG(INFO) << "seed=" << seed;
  DSched sched(DSched::uniform(seed));
  DLifoSem sem;

  const int NPOSTS = 1000;

  auto producer = [&]{
    for (int i=0; i<NPOSTS; ++i) {
      sem.post();
    }
  };

  std::atomic<bool> consumer_stop;
  int consumed = 0;

  auto consumer = [&]{
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

BENCHMARK(lifo_sem_pingpong, iters) {
  LifoSem a;
  LifoSem b;
  auto thr = std::thread([&]{
    for (int i = 0; i < iters; ++i) {
      a.wait();
      b.post();
    }
  });
  for (int i = 0; i < iters; ++i) {
    a.post();
    b.wait();
  }
  thr.join();
}

BENCHMARK(lifo_sem_oneway, iters) {
  LifoSem a;
  auto thr = std::thread([&]{
    for (int i = 0; i < iters; ++i) {
      a.wait();
    }
  });
  for (int i = 0; i < iters; ++i) {
    a.post();
  }
  thr.join();
}

BENCHMARK(single_thread_lifo_post, iters) {
  LifoSem sem;
  for (int n = 0; n < iters; ++n) {
    sem.post();
    asm volatile ("":::"memory");
  }
}

BENCHMARK(single_thread_lifo_wait, iters) {
  LifoSem sem(iters);
  for (int n = 0; n < iters; ++n) {
    sem.wait();
    asm volatile ("":::"memory");
  }
}

BENCHMARK(single_thread_lifo_postwait, iters) {
  LifoSem sem;
  for (int n = 0; n < iters; ++n) {
    sem.post();
    asm volatile ("":::"memory");
    sem.wait();
    asm volatile ("":::"memory");
  }
}

BENCHMARK(single_thread_lifo_trywait, iters) {
  LifoSem sem;
  for (int n = 0; n < iters; ++n) {
    EXPECT_FALSE(sem.tryWait());
    asm volatile ("":::"memory");
  }
}

BENCHMARK(single_thread_posix_postwait, iters) {
  sem_t sem;
  EXPECT_EQ(sem_init(&sem, 0, 0), 0);
  for (int n = 0; n < iters; ++n) {
    EXPECT_EQ(sem_post(&sem), 0);
    EXPECT_EQ(sem_wait(&sem), 0);
  }
  EXPECT_EQ(sem_destroy(&sem), 0);
}

BENCHMARK(single_thread_posix_trywait, iters) {
  sem_t sem;
  EXPECT_EQ(sem_init(&sem, 0, 0), 0);
  for (int n = 0; n < iters; ++n) {
    EXPECT_EQ(sem_trywait(&sem), -1);
  }
  EXPECT_EQ(sem_destroy(&sem), 0);
}

static void contendedUse(uint n, int posters, int waiters) {
  LifoSemImpl<std::atomic> sem;

  std::vector<std::thread> threads;
  std::atomic<bool> go(false);

  BENCHMARK_SUSPEND {
    for (int t = 0; t < waiters; ++t) {
      threads.emplace_back([=,&sem] {
        for (uint i = t; i < n; i += waiters) {
          sem.wait();
        }
      });
    }
    for (int t = 0; t < posters; ++t) {
      threads.emplace_back([=,&sem,&go] {
        while (!go.load()) {
          std::this_thread::yield();
        }
        for (uint i = t; i < n; i += posters) {
          sem.post();
        }
      });
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    thr.join();
  }
}

BENCHMARK_DRAW_LINE()
BENCHMARK_NAMED_PARAM(contendedUse, 1_to_1, 1, 1)
BENCHMARK_NAMED_PARAM(contendedUse, 1_to_32, 1, 32)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_1, 31, 1)
BENCHMARK_NAMED_PARAM(contendedUse, 16_to_16, 16, 16)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_32, 32, 32)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_1000, 32, 1000)

// sudo nice -n -20 tao/queues/LifoSemTests --benchmark --bm_min_iters=10000000
// ============================================================================
// tao/queues/LifoSemTests.cpp                     relative  time/iter  iters/s
// ============================================================================
// lifo_sem_pingpong                                            1.91us  522.92K
// lifo_sem_oneway                                            211.18ns    4.74M
// single_thread_lifo_post                                     19.71ns   50.75M
// single_thread_lifo_wait                                     18.84ns   53.09M
// single_thread_lifo_postwait                                 39.41ns   25.37M
// single_thread_lifo_trywait                                 912.10ps    1.10G
// single_thread_posix_postwait                                32.93ns   30.37M
// single_thread_posix_trywait                                 10.06ns   99.36M
// ----------------------------------------------------------------------------
// contendedUse(1_to_1)                                       208.21ns    4.80M
// contendedUse(1_to_32)                                      532.41ns    1.88M
// contendedUse(32_to_1)                                      153.74ns    6.50M
// contendedUse(16_to_16)                                     301.86ns    3.31M
// contendedUse(32_to_32)                                     268.32ns    3.73M
// contendedUse(32_to_1000)                                   966.27ns    1.03M
// ============================================================================

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rv;
}
