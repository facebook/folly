/*
 * Copyright 2016 Facebook, Inc.
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

// @author Tudor Bosman (tudorb@fb.com)

#include <folly/Bits.h>

#include <folly/portability/GTest.h>

using namespace folly;

// Test constexpr-ness.
#if !defined(__clang__) && !defined(_MSC_VER)
static_assert(findFirstSet(2u) == 2, "findFirstSet");
static_assert(findLastSet(2u) == 2, "findLastSet");
static_assert(nextPowTwo(2u) == 2, "nextPowTwo");
#endif

#ifndef __clang__
static_assert(isPowTwo(2u), "isPowTwo");
#endif

namespace {

template <class INT>
void testFFS() {
  EXPECT_EQ(0, findFirstSet(static_cast<INT>(0)));
  size_t bits = std::numeric_limits<
    typename std::make_unsigned<INT>::type>::digits;
  for (size_t i = 0; i < bits; i++) {
    INT v = (static_cast<INT>(1) << (bits - 1)) |
            (static_cast<INT>(1) << i);
    EXPECT_EQ(i+1, findFirstSet(v));
  }
}

template <class INT>
void testFLS() {
  typedef typename std::make_unsigned<INT>::type UINT;
  EXPECT_EQ(0, findLastSet(static_cast<INT>(0)));
  size_t bits = std::numeric_limits<UINT>::digits;
  for (size_t i = 0; i < bits; i++) {
    INT v1 = static_cast<UINT>(1) << i;
    EXPECT_EQ(i + 1, findLastSet(v1));

    INT v2 = (static_cast<UINT>(1) << i) - 1;
    EXPECT_EQ(i, findLastSet(v2));
  }
}

}  // namespace

TEST(Bits, FindFirstSet) {
  testFFS<char>();
  testFFS<signed char>();
  testFFS<unsigned char>();
  testFFS<short>();
  testFFS<unsigned short>();
  testFFS<int>();
  testFFS<unsigned int>();
  testFFS<long>();
  testFFS<unsigned long>();
  testFFS<long long>();
  testFFS<unsigned long long>();
}

TEST(Bits, FindLastSet) {
  testFLS<char>();
  testFLS<signed char>();
  testFLS<unsigned char>();
  testFLS<short>();
  testFLS<unsigned short>();
  testFLS<int>();
  testFLS<unsigned int>();
  testFLS<long>();
  testFLS<unsigned long>();
  testFLS<long long>();
  testFLS<unsigned long long>();
}

TEST(Bits, nextPowTwoClz) {
  EXPECT_EQ(1, nextPowTwo(0u));
  EXPECT_EQ(1, nextPowTwo(1u));
  EXPECT_EQ(2, nextPowTwo(2u));
  EXPECT_EQ(4, nextPowTwo(3u));
  EXPECT_EQ(4, nextPowTwo(4u));
  EXPECT_EQ(8, nextPowTwo(5u));
  EXPECT_EQ(8, nextPowTwo(6u));
  EXPECT_EQ(8, nextPowTwo(7u));
  EXPECT_EQ(8, nextPowTwo(8u));
  EXPECT_EQ(16, nextPowTwo(9u));
  EXPECT_EQ(16, nextPowTwo(13u));
  EXPECT_EQ(16, nextPowTwo(16u));
  EXPECT_EQ(512, nextPowTwo(510u));
  EXPECT_EQ(512, nextPowTwo(511u));
  EXPECT_EQ(512, nextPowTwo(512u));
  EXPECT_EQ(1024, nextPowTwo(513u));
  EXPECT_EQ(1024, nextPowTwo(777u));
  EXPECT_EQ(1ul << 31, nextPowTwo((1ul << 31) - 1));
  EXPECT_EQ(1ull << 32, nextPowTwo((1ull << 32) - 1));
  EXPECT_EQ(1ull << 63, nextPowTwo((1ull << 62) + 1));
}

TEST(Bits, isPowTwo) {
  EXPECT_FALSE(isPowTwo(0u));
  EXPECT_TRUE(isPowTwo(1ul));
  EXPECT_TRUE(isPowTwo(2ull));
  EXPECT_FALSE(isPowTwo(3ul));
  EXPECT_TRUE(isPowTwo(4ul));
  EXPECT_FALSE(isPowTwo(5ul));
  EXPECT_TRUE(isPowTwo(8ul));
  EXPECT_FALSE(isPowTwo(15u));
  EXPECT_TRUE(isPowTwo(16u));
  EXPECT_FALSE(isPowTwo(17u));
  EXPECT_FALSE(isPowTwo(511ul));
  EXPECT_TRUE(isPowTwo(512ul));
  EXPECT_FALSE(isPowTwo(513ul));
  EXPECT_FALSE(isPowTwo((1ul<<31) - 1));
  EXPECT_TRUE(isPowTwo(1ul<<31));
  EXPECT_FALSE(isPowTwo((1ul<<31) + 1));
  EXPECT_FALSE(isPowTwo((1ull<<63) - 1));
  EXPECT_TRUE(isPowTwo(1ull<<63));
  EXPECT_FALSE(isPowTwo((1ull<<63) + 1));
}

TEST(Bits, popcount) {
  EXPECT_EQ(0, popcount(0U));
  EXPECT_EQ(1, popcount(1U));
  EXPECT_EQ(32, popcount(uint32_t(-1)));
  EXPECT_EQ(64, popcount(uint64_t(-1)));
}
