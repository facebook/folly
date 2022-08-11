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

#include <folly/Random.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/SaturatingSemaphore.h>

TEST(ThrottledLifoSem, Basic) {
  folly::ThrottledLifoSem sem;
  EXPECT_TRUE(sem.post(0));
  EXPECT_FALSE(sem.post());
  EXPECT_TRUE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait());
  EXPECT_FALSE(sem.try_wait_for(std::chrono::milliseconds(1)));
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

    while (sem.numWaiters() != kNumWaiters) {
    }
    ASSERT_EQ(sem.numWaiters(), kNumWaiters);

    // Use the batch post() for half the rounds.
    if (round % 2 == 0) {
      EXPECT_TRUE(sem.post(kNumWaiters));
    } else {
      for (size_t i = 0; i < kNumWaiters; ++i) {
        EXPECT_TRUE(sem.post());
        while (sem.numWaiters() != kNumWaiters - i - 1) {
        }
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
  folly::SaturatingSemaphore</* MayBlock */ true> done;
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
