/*
 * Copyright 2014-present Facebook, Inc.
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

#include <folly/synchronization/LifoSem.h>

#include <thread>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/portability/Asm.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Semaphore.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly;
using namespace folly::test;

typedef LifoSemImpl<DeterministicAtomic> DLifoSem;
typedef DeterministicSchedule DSched;

LIFOSEM_DECLARE_POOL(DeterministicAtomic, 100000)

class LifoSemTest : public testing::Test {
 private:
  // pre-init the pool to avoid deadlock when using DeterministicAtomic
  using Node = detail::LifoSemRawNode<DeterministicAtomic>;
  Node::Pool& pool_{Node::pool()};
};

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

TEST(LifoSem, multi_try_wait_simple) {
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
      } catch (folly::ShutdownSemError& e) {
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

BENCHMARK(lifo_sem_pingpong, iters) {
  LifoSem a;
  LifoSem b;
  auto thr = std::thread([&] {
    for (size_t i = 0; i < iters; ++i) {
      a.wait();
      b.post();
    }
  });
  for (size_t i = 0; i < iters; ++i) {
    a.post();
    b.wait();
  }
  thr.join();
}

BENCHMARK(lifo_sem_oneway, iters) {
  LifoSem a;
  auto thr = std::thread([&] {
    for (size_t i = 0; i < iters; ++i) {
      a.wait();
    }
  });
  for (size_t i = 0; i < iters; ++i) {
    a.post();
  }
  thr.join();
}

BENCHMARK(single_thread_lifo_post, iters) {
  LifoSem sem;
  for (size_t n = 0; n < iters; ++n) {
    sem.post();
    asm_volatile_memory();
  }
}

BENCHMARK(single_thread_lifo_wait, iters) {
  LifoSem sem(iters);
  for (size_t n = 0; n < iters; ++n) {
    sem.wait();
    asm_volatile_memory();
  }
}

BENCHMARK(single_thread_lifo_postwait, iters) {
  LifoSem sem;
  for (size_t n = 0; n < iters; ++n) {
    sem.post();
    asm_volatile_memory();
    sem.wait();
    asm_volatile_memory();
  }
}

BENCHMARK(single_thread_lifo_trywait, iters) {
  LifoSem sem;
  for (size_t n = 0; n < iters; ++n) {
    EXPECT_FALSE(sem.tryWait());
    asm_volatile_memory();
  }
}

BENCHMARK(single_thread_posix_postwait, iters) {
  sem_t sem;
  EXPECT_EQ(sem_init(&sem, 0, 0), 0);
  for (size_t n = 0; n < iters; ++n) {
    EXPECT_EQ(sem_post(&sem), 0);
    EXPECT_EQ(sem_wait(&sem), 0);
  }
  EXPECT_EQ(sem_destroy(&sem), 0);
}

BENCHMARK(single_thread_posix_trywait, iters) {
  sem_t sem;
  EXPECT_EQ(sem_init(&sem, 0, 0), 0);
  for (size_t n = 0; n < iters; ++n) {
    EXPECT_EQ(sem_trywait(&sem), -1);
  }
  EXPECT_EQ(sem_destroy(&sem), 0);
}

static void contendedUse(uint32_t n, int posters, int waiters) {
  LifoSemImpl<std::atomic> sem;

  std::vector<std::thread> threads;
  std::atomic<bool> go(false);

  BENCHMARK_SUSPEND {
    for (int t = 0; t < waiters; ++t) {
      threads.emplace_back([=, &sem] {
        for (uint32_t i = t; i < n; i += waiters) {
          sem.wait();
        }
      });
    }
    for (int t = 0; t < posters; ++t) {
      threads.emplace_back([=, &sem, &go] {
        while (!go.load()) {
          std::this_thread::yield();
        }
        for (uint32_t i = t; i < n; i += posters) {
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

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(contendedUse, 1_to_1, 1, 1)
BENCHMARK_NAMED_PARAM(contendedUse, 1_to_4, 1, 4)
BENCHMARK_NAMED_PARAM(contendedUse, 1_to_32, 1, 32)
BENCHMARK_NAMED_PARAM(contendedUse, 4_to_1, 4, 1)
BENCHMARK_NAMED_PARAM(contendedUse, 4_to_24, 4, 24)
BENCHMARK_NAMED_PARAM(contendedUse, 8_to_100, 8, 100)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_1, 31, 1)
BENCHMARK_NAMED_PARAM(contendedUse, 16_to_16, 16, 16)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_32, 32, 32)
BENCHMARK_NAMED_PARAM(contendedUse, 32_to_1000, 32, 1000)

// sudo nice -n -20 _build/opt/folly/test/LifoSemTests
//     --benchmark --bm_min_iters=10000000 --gtest_filter=-\*
// ============================================================================
// folly/test/LifoSemTests.cpp                     relative  time/iter  iters/s
// ============================================================================
// lifo_sem_pingpong                                            1.31us  762.40K
// lifo_sem_oneway                                            193.89ns    5.16M
// single_thread_lifo_post                                     15.37ns   65.08M
// single_thread_lifo_wait                                     13.60ns   73.53M
// single_thread_lifo_postwait                                 29.43ns   33.98M
// single_thread_lifo_trywait                                 677.69ps    1.48G
// single_thread_posix_postwait                                25.03ns   39.95M
// single_thread_posix_trywait                                  7.30ns  136.98M
// ----------------------------------------------------------------------------
// contendedUse(1_to_1)                                       158.22ns    6.32M
// contendedUse(1_to_4)                                       574.73ns    1.74M
// contendedUse(1_to_32)                                      592.94ns    1.69M
// contendedUse(4_to_1)                                       118.28ns    8.45M
// contendedUse(4_to_24)                                      667.62ns    1.50M
// contendedUse(8_to_100)                                     701.46ns    1.43M
// contendedUse(32_to_1)                                      165.06ns    6.06M
// contendedUse(16_to_16)                                     238.57ns    4.19M
// contendedUse(32_to_32)                                     219.82ns    4.55M
// contendedUse(32_to_1000)                                   777.42ns    1.29M
// ============================================================================

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rv;
}
