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

#include <folly/synchronization/StripedThrottledLifoSem.h>

#include <thread>

#include <folly/Random.h>
#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>

TEST(StripedThrottledLifoSem, Simple) {
  folly::StripedThrottledLifoSem<> sem(2);

  sem.post(0);
  sem.post(1);

  // Waiting on stripe 0 we get both posts, but we always get the local one
  // first.
  EXPECT_EQ(sem.wait(0), 0);
  EXPECT_EQ(sem.wait(0), 1);

  EXPECT_FALSE(sem.try_wait(0));
  EXPECT_FALSE(sem.try_wait(1));
}

TEST(StripedThrottledLifoSem, Payload) {
  folly::StripedThrottledLifoSem<int> sem(2, std::tuple{-1});
  EXPECT_EQ(sem.payload(0), -1);
  EXPECT_EQ(sem.payload(1), -1);
  sem.payload(0) = 0;
  sem.payload(1) = 1;
  EXPECT_EQ(sem.payload(0), 0);
  EXPECT_EQ(sem.payload(1), 1);
}

TEST(StripedThrottledLifoSem, MPMCStress) {
  // Same number of producers and consumers.
  constexpr size_t kNumThreads = 16;
  constexpr size_t kNumPostsPerThread = 10000;
  constexpr size_t kExpectedHandoffs = kNumThreads * kNumPostsPerThread;
  constexpr size_t kNumStripes = 8;

  auto randomStripe = [&] { return folly::Random::rand64(kNumStripes); };

  folly::StripedThrottledLifoSem<> sem(kNumStripes);
  folly::StripedThrottledLifoSemBalancer::subscribe(sem);
  SCOPE_EXIT {
    folly::StripedThrottledLifoSemBalancer::unsubscribe(sem);
  };

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;
  folly::SaturatingSemaphore<> done;
  std::atomic<size_t> handoffs = 0;
  std::array<std::atomic<uint32_t>, kNumStripes> values;
  for (size_t t = 0; t < kNumThreads; ++t) {
    producers.emplace_back([&] {
      for (size_t i = 0; i < kNumPostsPerThread; ++i) {
        // Force the consumers to go to sleep sometimes.
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::microseconds{500} * folly::Random::randDouble01());
        auto stripe = randomStripe();
        ++values[stripe];
        sem.post(stripe);
      }
    });
    consumers.emplace_back([&] {
      while (true) {
        auto stripe = sem.wait(randomStripe());
        CHECK_GT(values[stripe].fetch_sub(1), 0);
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
  for (size_t i = 0; i < kNumThreads; ++i) {
    auto stripe = randomStripe();
    ++values[stripe];
    sem.post(stripe);
  }

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
