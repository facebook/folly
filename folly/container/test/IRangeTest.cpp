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

#include <folly/container/irange.h>

#include <gtest/gtest.h>

TEST(IRangeTest, SingleArg) {
  int sum = 0;
  for (const auto i : folly::irange(10)) {
    sum += i;
  }
  EXPECT_EQ(sum, 45);
}

TEST(IRangeTest, TwoArg) {
  int sum = 0;
  for (const auto i : folly::irange(4, 10)) {
    sum += i;
  }
  EXPECT_EQ(sum, 39);
}

TEST(IRangeTest, BigFirst) {
  int sum = 0;
  for (const auto i : folly::irange(11, 10)) {
    sum += i;
  }
  EXPECT_EQ(sum, 0);
}

TEST(IRangeTest, FirstConvertsBig) {
  int sum = 0;
  for (const auto i : folly::irange(-1u, 10u)) {
    sum += i;
  }
  EXPECT_EQ(sum, 0);
}
