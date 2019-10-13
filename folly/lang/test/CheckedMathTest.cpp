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

#include <folly/lang/CheckedMath.h>

#include <limits>

#include <folly/portability/GTest.h>

TEST(CheckedMath, checked_add_no_overflow) {
  unsigned int a;

  EXPECT_TRUE(folly::checked_add(&a, 5u, 4u));
  EXPECT_EQ(a, 9);
}

TEST(CheckedMath, checked_add_overflow) {
  unsigned int a;

  EXPECT_FALSE(
      folly::checked_add(&a, std::numeric_limits<unsigned int>::max(), 4u));
  EXPECT_EQ(a, {});
}

TEST(CheckedMath, checked_add_size_t_overflow) {
  size_t a;

  EXPECT_FALSE(folly::checked_add<size_t>(
      &a, std::numeric_limits<size_t>::max() - 7, 9));
  EXPECT_EQ(a, {});
}
