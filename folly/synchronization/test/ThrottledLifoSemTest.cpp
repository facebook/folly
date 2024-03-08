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

#include <folly/synchronization/ThrottledLifoSem.h>

#include <chrono>
#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/SaturatingSemaphore.h>

namespace folly {

class ThrottledLifoSemTestHelper {
 public:
  static void spinUntilWaiters(
      folly::ThrottledLifoSem& sem, size_t n, bool assertExact = false) {
    while (sem.numWaiters() != n) {
    }
    if (assertExact) {
      ASSERT_EQ(sem.numWaiters(), n);
    }
  }
};

} // namespace folly

const auto kNoSpin =
    folly::WaitOptions{}.spin_max(std::chrono::nanoseconds::zero());

TEST(ThrottledLifoSem, Basic) {
  folly::ThrottledLifoSem sem;
  EXPECT_TRUE(sem.post(0));
  EXPECT_FALSE(sem.post());
  EXPECT_TRUE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait_for(std::chrono::milliseconds(1)));
}

TEST(ThrottledLifoSem, Timeouts) {
  constexpr auto kWakeUpInterval = std::chrono::milliseconds(200);

  folly::ThrottledLifoSem sem({.wakeUpInterval = kWakeUpInterval});
  std::atomic<size_t> timeouts = 0;
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 3; ++i) {
    threads.emplace_back([&] {
      // One waiter will succeed, one will timeout while waiting on the baton,
      // one while sleeping though the wakeup interval. The purpose of this test
      // is to ensure that the invariants (checked at destruction) are restored
      // in both timeout cases.
      if (!sem.try_wait_for(kWakeUpInterval / 2, kNoSpin)) {
        ++timeouts;
      }
    });
  }

  // May hang forever if any waiters time out before we get here, but the large
  // timeout should make this unlikely unless the system is heavily loaded.
  folly::ThrottledLifoSemTestHelper::spinUntilWaiters(
      sem, 3, /* assertExact */ true);

  EXPECT_TRUE(sem.post());
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(timeouts.load(), 2);
}

// Exercise the race between post() and waiters that have just timed out.
TEST(ThrottledLifoSem, TimeoutsStress) {
  constexpr size_t kNumWaiters = 64;
  constexpr size_t kNumPosts = 100000;
  folly::ThrottledLifoSem sem(
      {.wakeUpInterval = std::chrono::nanoseconds::zero()});

  std::vector<std::thread> threads;
  std::atomic<size_t> received = 0;
  for (size_t i = 0; i < kNumWaiters; ++i) {
    threads.emplace_back([&] {
      while (received.load() != kNumPosts) {
        if (sem.try_wait_for(std::chrono::microseconds{100}, kNoSpin)) {
          ++received;
        }
      }
    });
  }

  for (size_t i = 0; i < kNumPosts; ++i) {
    sem.post();
    // Wait until the post is consumed.
    while (sem.valueGuess() > 0) {
    }
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST(ThrottledLifoSem, MultipleWaiters) {
  constexpr size_t kNumRounds = 10;
  constexpr size_t kNumWaiters = 64;
  constexpr auto kWakeUpInterval = std::chrono::microseconds(100);

  folly::ThrottledLifoSem sem({.wakeUpInterval = kWakeUpInterval});

  for (size_t round = 0; round < kNumRounds; ++round) {
    std::vector<std::thread> threads;
    for (size_t i = 0; i < kNumWaiters; ++i) {
      threads.emplace_back([&] { sem.wait(); });
    }

    folly::ThrottledLifoSemTestHelper::spinUntilWaiters(
        sem, kNumWaiters, /* assertExact */ true);

    // Use the batch post() for half the rounds.
    if (round % 2 == 0) {
      EXPECT_TRUE(sem.post(kNumWaiters));
    } else {
      for (size_t i = 0; i < kNumWaiters; ++i) {
        EXPECT_TRUE(sem.post());
        folly::ThrottledLifoSemTestHelper::spinUntilWaiters(
            sem, kNumWaiters - i - 1);
        /* sleep override */ std::this_thread::sleep_for(
            i * kWakeUpInterval / 10);
      }
    }

    for (auto& t : threads) {
      t.join();
    }
    // No more waiters.
    EXPECT_FALSE(sem.post());
    ASSERT_TRUE(sem.try_wait());
  }
}

TEST(ThrottledLifoSem, MPMCStress) {
  // Same number of producers and consumers.
  constexpr size_t kNumThreads = 16;
  constexpr size_t kNumPostsPerThread = 10000;
  constexpr size_t kExpectedHandoffs = kNumThreads * kNumPostsPerThread;
  constexpr auto kWakeUpInterval = std::chrono::microseconds(100);

  folly::ThrottledLifoSem sem({.wakeUpInterval = kWakeUpInterval});

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;
  folly::SaturatingSemaphore<> done;
  std::atomic<size_t> handoffs = 0;
  for (size_t t = 0; t < kNumThreads; ++t) {
    producers.emplace_back([&] {
      for (size_t i = 0; i < kNumPostsPerThread; ++i) {
        // Force the consumers to go to sleep sometimes.
        /* sleep override */ std::this_thread::sleep_for(
            kWakeUpInterval * folly::Random::randDouble01());
        sem.post();
      }
    });
    consumers.emplace_back([&] {
      while (true) {
        sem.wait();
        if (done.ready()) {
          return;
        }
        if (++handoffs == kExpectedHandoffs) {
          done.post();
        }
      }
    });
  }

  done.wait();
  EXPECT_EQ(sem.valueGuess(), 0);

  // Wake up the consumers.
  sem.post(kNumThreads);

  for (auto& t : consumers) {
    t.join();
  }
  for (auto& t : producers) {
    t.join();
  }

  // Re-check that nothing has changed while joining the threads.
  EXPECT_EQ(handoffs.load(), kExpectedHandoffs);
  EXPECT_EQ(sem.valueGuess(), 0);
}

namespace {

// Benchmark the cost of post() under contention when no wakeup is performed (by
// not having waiters), which simulates the case where the waking chain is
// already active or the thread pool is saturated.
void post(size_t iters, size_t numThreads) {
  folly::ThrottledLifoSem sem;

  std::vector<std::thread> threads;
  for (size_t t = 0; t < numThreads; ++t) {
    threads.emplace_back([&] {
      for (size_t i = 0; i < iters / numThreads; ++i) {
        sem.post();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

} // namespace

BENCHMARK_NAMED_PARAM(post, 1_thread, 1)
BENCHMARK_NAMED_PARAM(post, 2_threads, 2)
BENCHMARK_NAMED_PARAM(post, 4_threads, 4)
BENCHMARK_NAMED_PARAM(post, 8_threads, 8)

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rv;
}
