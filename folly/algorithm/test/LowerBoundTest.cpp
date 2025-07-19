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

#include <folly/algorithm/LowerBound.h>

#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include <folly/portability/GTest.h>

TEST(LowerBoundTest, Empty) {
  const std::vector<int> v;

  EXPECT_EQ(folly::lower_bound(v.begin(), v.end(), 0), v.end());
}

TEST(LowerBoundTest, One) {
  const std::vector<int> v = {1};

  auto actual = folly::lower_bound(v.begin(), v.end(), 0);

  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 1);

  EXPECT_EQ(folly::lower_bound(v.begin(), v.end(), 2), v.end());
}

TEST(LowerBoundTest, Boundaries) {
  const std::vector<int> v = {0, 1, 2, 3};

  auto actual = folly::lower_bound(v.begin(), v.end(), 0);

  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 0);

  actual = folly::lower_bound(v.begin(), v.end(), 3);

  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 3);
}

TEST(LowerBoundTest, ExactNotFound) {
  const std::vector<int> v = {0, 2, 3};

  auto actual = folly::lower_bound(v.begin(), v.end(), 1);

  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 2);
}

TEST(LowerBoundTest, Repeating) {
  const std::vector<int> v = {0, 1, 2, 2, 3, 3, 3};

  auto actual = folly::lower_bound(v.begin(), v.end(), 3);

  auto expected = std::find(v.begin(), v.end(), 3);
  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 3);
  EXPECT_EQ(actual, expected);
}

TEST(LowerBoundTest, AllEqual) {
  const std::vector<int> v = {3, 3, 3};

  auto actual = folly::lower_bound(v.begin(), v.end(), 3);

  auto expected = std::find(v.begin(), v.end(), 3);
  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 3);
  EXPECT_EQ(actual, expected);
}

TEST(LowerBoundTest, Comparator) {
  const std::vector<int> v = {0, 1, 2, 3};
  auto less = [](int x, int y) { return x < y; };

  auto actual = folly::lower_bound(v.begin(), v.end(), 1, less);

  auto expected = std::find(v.begin(), v.end(), 1);
  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, 1);
  EXPECT_EQ(actual, expected);
}

TEST(LowerBoundTest, ForwardIter) {
  const std::list<int> l = {0, 1, 2, 4};

  auto actual = folly::lower_bound(l.begin(), l.end(), 4);

  auto expected = std::find(l.begin(), l.end(), 4);
  EXPECT_NE(actual, l.end());
  EXPECT_EQ(*actual, 4);
  EXPECT_EQ(actual, expected);
}

TEST(LowerBoundTest, Lexicographical) {
  const std::vector<std::string> v = {"0", "1", "10", "11", "111", "2", "3"};

  auto actual = folly::lower_bound(v.begin(), v.end(), "10");

  auto expected = std::find(v.begin(), v.end(), "10");
  EXPECT_NE(actual, v.end());
  EXPECT_EQ(*actual, "10");
  EXPECT_EQ(actual, expected);
}
