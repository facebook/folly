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

#include <numeric>
#include <thread>

#include <folly/portability/GTest.h>
#include <folly/synchronization/Latch.h>

TEST(LatchTest, ExpectedZero) {
  folly::Latch latch(0);
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, ExpectedOne) {
  folly::Latch latch(1);
  EXPECT_FALSE(latch.try_wait());
  latch.count_down();
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, ExpectedMax) {
  const auto max = folly::Latch::max();
  folly::Latch latch(max);
  EXPECT_FALSE(latch.try_wait());
  latch.count_down(max);
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, WaitFor) {
  folly::Latch latch(1);
  EXPECT_FALSE(latch.try_wait_for(std::chrono::seconds(1)));
  latch.count_down();
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(1)));
}

TEST(LatchTest, WaitUntil) {
  folly::Latch latch(1);
  EXPECT_FALSE(latch.try_wait_until(
      std::chrono::steady_clock::now() + std::chrono::seconds(1)));
  latch.count_down();
  EXPECT_TRUE(latch.try_wait_until(
      std::chrono::steady_clock::now() + std::chrono::seconds(1)));
}

TEST(LatchTest, Ready) {
  folly::Latch latch(1);
  auto ready = [](const folly::Latch& l) { return l.ready(); };
  EXPECT_FALSE(ready(latch));
  latch.count_down();
  EXPECT_TRUE(ready(latch));
}

TEST(LatchTest, CountDown) {
  folly::Latch latch(3);
  EXPECT_FALSE(latch.try_wait());
  latch.count_down(0); // (noop)
  EXPECT_FALSE(latch.try_wait());
  latch.count_down();
  EXPECT_FALSE(latch.try_wait());
  latch.count_down();
  EXPECT_FALSE(latch.try_wait());
  latch.count_down();
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, CountDownZero) {
  folly::Latch latch(0);
  EXPECT_TRUE(latch.try_wait());
  latch.count_down(0);
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, CountDownN) {
  folly::Latch latch(5);
  EXPECT_FALSE(latch.try_wait());
  latch.count_down(5);
  EXPECT_TRUE(latch.try_wait());
}

TEST(LatchTest, CountDownThreads) {
  const int N = 32;
  std::vector<int> done(N);
  folly::Latch latch(N);
  std::vector<std::thread> threads;
  for (int i = 0; i < N; i++) {
    threads.emplace_back([&, i] {
      done[i] = 1;
      latch.count_down();
    });
  }
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
  EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(LatchTest, CountDownThreadsTwice1) {
  const int N = 32;
  std::vector<int> done(N);
  folly::Latch latch(N * 2);
  std::vector<std::thread> threads;
  for (int i = 0; i < N; i++) {
    threads.emplace_back([&, i] {
      done[i] = 1;
      // count_down() multiple times within same thread
      latch.count_down();
      latch.count_down();
    });
  }
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
  EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(LatchTest, CountDownThreadsTwice2) {
  const int N = 32;
  std::vector<int> done(N);
  folly::Latch latch(N * 2);
  std::vector<std::thread> threads;
  for (int i = 0; i < N; i++) {
    threads.emplace_back([&, i] {
      done[i] = 1;
      // count_down() multiple times within same thread
      latch.count_down(2);
    });
  }
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
  EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(LatchTest, CountDownThreadsWait) {
  const int N = 32;
  std::vector<int> done(N);
  folly::Latch latch(N);
  std::vector<std::thread> threads;
  for (int i = 0; i < N; i++) {
    threads.emplace_back([&, i] {
      done[i] = 1;
      // count_down() and wait() within thread
      latch.count_down();
      EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
      EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
    });
  }
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
  EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(LatchTest, CountDownThreadsArriveAndWait) {
  const int N = 32;
  std::vector<int> done(N);
  folly::Latch latch(N);
  std::vector<std::thread> threads;
  for (int i = 0; i < N; i++) {
    threads.emplace_back([&, i] {
      done[i] = 1;
      // count_down() and wait() within thread
      latch.arrive_and_wait();
      EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
    });
  }
  EXPECT_TRUE(latch.try_wait_for(std::chrono::seconds(60)));
  EXPECT_EQ(std::accumulate(done.begin(), done.end(), 0), N);
  for (auto& t : threads) {
    t.join();
  }
}

TEST(LatchTest, OutOfScopeStillArmed) {
  folly::Latch latch(3);
  latch.count_down();
  // mainly checking for blocking behavior which will result in a test timeout
  // (latch should not block in this case)
}

TEST(LatchTest, InvalidInit) {
  // latch initialized with a negative value
  EXPECT_DEATH(folly::Latch latch(-1), ".*");
  // latch initialized with a value bigger than max
  const int64_t init = folly::Latch::max() + 1;
  EXPECT_DEATH(folly::Latch latch(init), ".*");
}

TEST(LatchTest, InvalidCountDown) {
  folly::Latch latch(1);
  latch.count_down();
  // count_down() called more times than expected
  EXPECT_DEATH(latch.count_down(), ".*");
}

TEST(LatchTest, InvalidCountDownN) {
  folly::Latch latch(5);
  // count_down() called with a bigger value than expected
  EXPECT_DEATH(latch.count_down(6), ".*");
}

TEST(LatchTest, InvalidCountDownNegative) {
  folly::Latch latch(1);
  // count_down() called with a negative value
  EXPECT_DEATH(latch.count_down(-1), ".*");
}
