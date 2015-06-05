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

#include <gtest/gtest.h>

#include <folly/futures/Future.h>

#include <vector>

using namespace folly;

TEST(Window, basic) {
  // int -> Future<int>
  auto fn = [](std::vector<int> input, size_t window_size, size_t expect) {
    auto res = reduce(
      window(
        input,
        [](int i) { return makeFuture(i); },
        2),
      0,
      [](int sum, const Try<int>& b) {
        return sum + *b;
      }).get();
    EXPECT_EQ(expect, res);
  };
  {
    // 2 in-flight at a time
    std::vector<int> input = {1, 2, 3};
    fn(input, 2, 6);
  }
  {
    // 4 in-flight at a time
    std::vector<int> input = {1, 2, 3};
    fn(input, 4, 6);
  }
  {
    // empty inpt
    std::vector<int> input;
    fn(input, 1, 0);
  }
  {
    // int -> Future<void>
    auto res = reduce(
      window(
        std::vector<int>({1, 2, 3}),
        [](int i) { return makeFuture(); },
        2),
      0,
      [](int sum, const Try<void>& b) {
        EXPECT_TRUE(b.hasValue());
        return sum + 1;
      }).get();
    EXPECT_EQ(3, res);
  }
  {
    // string -> return Future<int>
    auto res = reduce(
      window(
        std::vector<std::string>{"1", "2", "3"},
        [](std::string s) { return makeFuture<int>(folly::to<int>(s)); },
        2),
      0,
      [](int sum, const Try<int>& b) {
        return sum + *b;
      }).get();
    EXPECT_EQ(6, res);
  }
}
