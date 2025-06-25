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

#include <folly/container/StdBitset.h>

#include <gtest/gtest.h>

#include <bitset>

using namespace ::testing;

TEST(BitSetTest, FindFirst) {
  {
    std::string bit_string = "110010";
    std::bitset<8> objectUnderTest(bit_string); // [0,0,1,1,0,0,1,0]

    size_t actual{folly::std_bitset_find_first(objectUnderTest)};
    size_t expected{1};
    ASSERT_EQ(expected, actual);
  }

  {
    std::bitset<8> objectUnderTest; // [0,0,0,0,0,0,0,0]

    size_t actual{folly::std_bitset_find_first(objectUnderTest)};
    size_t expected{8};
    ASSERT_EQ(expected, actual);
  }
}

TEST(BitSetTest, FindNext) {
  std::string bit_string = "110010";
  std::bitset<8> objectUnderTest(bit_string); // [0,0,1,1,0,0,1,0]

  {
    size_t actual{folly::std_bitset_find_next(objectUnderTest, 0)};
    size_t expected{1};
    ASSERT_EQ(expected, actual);
  }

  {
    size_t actual{folly::std_bitset_find_next(objectUnderTest, 1)};
    size_t expected{4};
    ASSERT_EQ(expected, actual);
  }

  {
    size_t actual{folly::std_bitset_find_next(objectUnderTest, 2)};
    size_t expected{4};
    ASSERT_EQ(expected, actual);
  }

  {
    size_t actual{folly::std_bitset_find_next(objectUnderTest, 4)};
    size_t expected{5};
    ASSERT_EQ(expected, actual);
  }

  {
    size_t actual{folly::std_bitset_find_next(objectUnderTest, 5)};
    size_t expected{8};
    ASSERT_EQ(expected, actual);
  }
}
