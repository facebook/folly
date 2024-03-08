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

#include <folly/futures/ManualTimekeeper.h>

#include <chrono>

#include <folly/portability/GTest.h>

using namespace std::literals;

namespace folly {

class ManualTimekeeperTest : public ::testing::Test {};

TEST_F(ManualTimekeeperTest, Basic) {
  auto timekeeper = folly::ManualTimekeeper{};
  auto future = timekeeper.after(100s);
  timekeeper.advance(100s);
  EXPECT_TRUE(future.isReady());
}

TEST_F(ManualTimekeeperTest, AdvanceWithoutAnyFutures) {
  auto timekeeper = folly::ManualTimekeeper{};
  timekeeper.advance(100s);
  auto future = timekeeper.after(100s);
  EXPECT_FALSE(future.isReady());
  timekeeper.advance(100s);
  EXPECT_TRUE(future.isReady());
}

TEST_F(ManualTimekeeperTest, AdvanceWithManyFutures) {
  auto timekeeper = folly::ManualTimekeeper{};

  auto one = timekeeper.after(100s);
  auto two = timekeeper.after(200s);
  auto three = timekeeper.after(300s);

  EXPECT_FALSE(one.isReady());
  EXPECT_FALSE(two.isReady());
  EXPECT_FALSE(three.isReady());

  timekeeper.advance(100s);

  EXPECT_TRUE(one.isReady());
  EXPECT_FALSE(two.isReady());
  EXPECT_FALSE(three.isReady());

  timekeeper.advance(100s);

  EXPECT_TRUE(one.isReady());
  EXPECT_TRUE(two.isReady());
  EXPECT_FALSE(three.isReady());

  timekeeper.advance(100s);

  EXPECT_TRUE(one.isReady());
  EXPECT_TRUE(two.isReady());
  EXPECT_TRUE(three.isReady());

  timekeeper.advance(100s);

  EXPECT_TRUE(one.isReady());
  EXPECT_TRUE(two.isReady());
  EXPECT_TRUE(three.isReady());

  auto four = timekeeper.after(100s);

  EXPECT_FALSE(four.isReady());

  timekeeper.advance(100s);

  EXPECT_TRUE(one.isReady());
  EXPECT_TRUE(two.isReady());
  EXPECT_TRUE(three.isReady());
  EXPECT_TRUE(four.isReady());
}

TEST_F(ManualTimekeeperTest, Cancel) {
  auto timekeeper = folly::ManualTimekeeper{};
  auto future = timekeeper.after(100s);
  future.cancel();
  ASSERT_TRUE(future.isReady());
  EXPECT_TRUE(future.result().hasException<FutureCancellation>());
  timekeeper.advance(100s);
  EXPECT_TRUE(future.result().hasException<FutureCancellation>());
}
} // namespace folly
