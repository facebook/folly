/*
 * Copyright 2015 Facebook, Inc.
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

#include <chrono>
#include <folly/wangle/concurrent/Codel.h>
#include <gtest/gtest.h>
#include <thread>

TEST(CodelTest, Basic) {
  using std::chrono::milliseconds;
  folly::wangle::Codel c;
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
