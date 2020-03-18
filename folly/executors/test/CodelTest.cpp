/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/executors/Codel.h>
#include <folly/portability/GTest.h>
#include <chrono>
#include <thread>

using std::chrono::milliseconds;
using std::this_thread::sleep_for;

TEST(CodelTest, Basic) {
  folly::Codel c;
  std::this_thread::sleep_for(milliseconds(110));
  // This interval is overloaded
  EXPECT_FALSE(c.overloaded(milliseconds(100)));
  std::this_thread::sleep_for(milliseconds(90));
  // At least two requests must happen in an interval before they will fail
  EXPECT_FALSE(c.overloaded(milliseconds(50)));
  EXPECT_TRUE(c.overloaded(milliseconds(50)));
  std::this_thread::sleep_for(milliseconds(110));
  // Previous interval is overloaded, but 2ms isn't enough to fail
  EXPECT_FALSE(c.overloaded(milliseconds(2)));
  std::this_thread::sleep_for(milliseconds(90));
  // 20 ms > target interval * 2
  EXPECT_TRUE(c.overloaded(milliseconds(20)));
}

TEST(CodelTest, highLoad) {
  folly::Codel c;
  c.overloaded(milliseconds(40));
  EXPECT_EQ(100, c.getLoad());
}

TEST(CodelTest, mediumLoad) {
  folly::Codel c;
  c.overloaded(milliseconds(20));
  sleep_for(milliseconds(90));
  // this is overloaded but this request shouldn't drop because it's not >
  // slough timeout
  EXPECT_FALSE(c.overloaded(milliseconds(8)));
  EXPECT_GT(100, c.getLoad());
}

TEST(CodelTest, reducingLoad) {
  folly::Codel c;
  c.overloaded(milliseconds(20));
  sleep_for(milliseconds(90));
  EXPECT_FALSE(c.overloaded(milliseconds(4)));
}

TEST(CodelTest, oneRequestNoDrop) {
  folly::Codel c;
  EXPECT_FALSE(c.overloaded(milliseconds(20)));
}

TEST(CodelTest, getLoadSanity) {
  folly::Codel c;
  // should be 100% but leave a litte wiggle room.
  c.overloaded(milliseconds(10));
  EXPECT_LT(99, c.getLoad());
  EXPECT_GT(101, c.getLoad());

  // should be 70% but leave a litte wiggle room.
  c.overloaded(milliseconds(7));
  EXPECT_LT(60, c.getLoad());
  EXPECT_GT(80, c.getLoad());

  // should be 20% but leave a litte wiggle room.
  c.overloaded(milliseconds(2));
  EXPECT_LT(10, c.getLoad());
  EXPECT_GT(30, c.getLoad());

  // this test demonstrates how silly getLoad() is, but silly isn't
  // necessarily useless
}

TEST(CodelTest, updateTargetDelay) {
  folly::Codel c;
  folly::Codel::Options opts;
  c.overloaded(milliseconds(40));
  EXPECT_EQ(100, c.getLoad());
  EXPECT_EQ(milliseconds(5), c.getOptions().targetDelay());

  // Increase the target delay and test again.
  opts.setTargetDelay(std::chrono::milliseconds(40));
  opts.setInterval(std::chrono::milliseconds(100));
  c.setOptions(opts);
  EXPECT_EQ(milliseconds(40), c.getOptions().targetDelay());
  EXPECT_FALSE(c.overloaded(milliseconds(40)));

  // Decrease the target delay and test again.
  opts.setTargetDelay(std::chrono::milliseconds(5));
  c.setOptions(opts);
  EXPECT_EQ(milliseconds(5), c.getOptions().targetDelay());
  sleep_for(milliseconds(110));
  EXPECT_FALSE(c.overloaded(milliseconds(40)));
  EXPECT_TRUE(c.overloaded(milliseconds(40)));
}

TEST(CodelTest, updateInterval) {
  folly::Codel c;
  folly::Codel::Options opts;
  c.overloaded(milliseconds(50));
  EXPECT_EQ(100, c.getLoad());

  // Make sure the default interval is correct.
  EXPECT_EQ(milliseconds(100), c.getOptions().interval());
  sleep_for(milliseconds(110));

  // Two delayed requests lead to overload.
  EXPECT_FALSE(c.overloaded(milliseconds(50)));
  EXPECT_TRUE(c.overloaded(milliseconds(50)));

  // Increase the interval to 200 ms and test again.
  opts.setInterval(std::chrono::milliseconds(200));
  opts.setTargetDelay(std::chrono::milliseconds(FLAGS_codel_target_delay));

  c.setOptions(opts);
  EXPECT_EQ(milliseconds(200), c.getOptions().interval());
  sleep_for(milliseconds(100));
  EXPECT_FALSE(c.overloaded(milliseconds(20)));
  EXPECT_TRUE(c.overloaded(milliseconds(20)));
}

TEST(CodelTest, invalidParamUpdates) {
  folly::Codel c;
  folly::Codel::Options opts = c.getOptions();
  EXPECT_EQ(milliseconds(5), c.getOptions().targetDelay());
  EXPECT_EQ(milliseconds(100), c.getOptions().interval());

  // Set target delay to an invalid value.
  // Can't be greater than the existing interval period.
  opts.setTargetDelay(std::chrono::milliseconds(110));
  try {
    c.setOptions(opts);
    FAIL() << "Expected a std::runtime_error";
  } catch (std::invalid_argument const& err) {
    std::string error = err.what();
    EXPECT_EQ("Invalid arguments provided", error);
  }
  EXPECT_EQ(milliseconds(5), c.getOptions().targetDelay());

  // Set the target delay to a valid value.
  opts.setTargetDelay(std::chrono::milliseconds(20));
  opts.setInterval(std::chrono::milliseconds(100));
  c.setOptions(opts);
  EXPECT_EQ(milliseconds(20), c.getOptions().targetDelay());

  // Set the interval to a value smaller than the target delay.
  opts.setInterval(std::chrono::milliseconds(5));
  try {
    c.setOptions(opts);
    FAIL() << "Expected a std::runtime_error";
  } catch (std::invalid_argument const& err) {
    std::string error = err.what();
    EXPECT_EQ("Invalid arguments provided", error);
  }
  EXPECT_EQ(milliseconds(100), c.getOptions().interval());

  // Set the params to a valid combination.
  opts.setInterval(std::chrono::milliseconds(200));
  opts.setTargetDelay(std::chrono::milliseconds(10));

  c.setOptions(opts);
  EXPECT_EQ(milliseconds(10), c.getOptions().targetDelay());
  EXPECT_EQ(milliseconds(200), c.getOptions().interval());
}
