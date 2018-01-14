/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/ConstexprMath.h>

#include <folly/portability/GTest.h>

namespace {

class ConstexprMathTest : public testing::Test {};
} // namespace

TEST_F(ConstexprMathTest, constexpr_min) {
  constexpr auto x = uint16_t(3);
  constexpr auto y = uint16_t(7);
  constexpr auto z = uint16_t(4);
  constexpr auto a = folly::constexpr_min(x, y, z);
  EXPECT_EQ(3, a);
  EXPECT_TRUE((std::is_same<const uint16_t, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_max) {
  constexpr auto x = uint16_t(3);
  constexpr auto y = uint16_t(7);
  constexpr auto z = uint16_t(4);
  constexpr auto a = folly::constexpr_max(x, y, z);
  EXPECT_EQ(7, a);
  EXPECT_TRUE((std::is_same<const uint16_t, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_clamp) {
  constexpr auto lo = uint16_t(3);
  constexpr auto hi = uint16_t(7);
  constexpr auto x = folly::constexpr_clamp(uint16_t(2), lo, hi);
  constexpr auto y = folly::constexpr_clamp(uint16_t(5), lo, hi);
  constexpr auto z = folly::constexpr_clamp(uint16_t(8), lo, hi);
  EXPECT_EQ(3, x);
  EXPECT_EQ(5, y);
  EXPECT_EQ(7, z);
  EXPECT_TRUE((std::is_same<const uint16_t, decltype(y)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_unsigned) {
  constexpr auto v = uint32_t(17);
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17, a);
  EXPECT_TRUE((std::is_same<const uint32_t, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_signed_positive) {
  constexpr auto v = int32_t(17);
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17, a);
  EXPECT_TRUE((std::is_same<const uint32_t, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_signed_negative) {
  constexpr auto v = int32_t(-17);
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17, a);
  EXPECT_TRUE((std::is_same<const uint32_t, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_float_positive) {
  constexpr auto v = 17.5f;
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17.5, a);
  EXPECT_TRUE((std::is_same<const float, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_float_negative) {
  constexpr auto v = -17.5f;
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17.5, a);
  EXPECT_TRUE((std::is_same<const float, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_double_positive) {
  constexpr auto v = 17.5;
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17.5, a);
  EXPECT_TRUE((std::is_same<const double, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_abs_double_negative) {
  constexpr auto v = -17.5;
  constexpr auto a = folly::constexpr_abs(v);
  EXPECT_EQ(17.5, a);
  EXPECT_TRUE((std::is_same<const double, decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_1) {
  constexpr auto v = 1ull;
  constexpr auto a = folly::constexpr_log2(v);
  EXPECT_EQ(0ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_2) {
  constexpr auto v = 2ull;
  constexpr auto a = folly::constexpr_log2(v);
  EXPECT_EQ(1ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_64) {
  constexpr auto v = 64ull;
  constexpr auto a = folly::constexpr_log2(v);
  EXPECT_EQ(6ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_ceil_1) {
  constexpr auto v = 1ull;
  constexpr auto a = folly::constexpr_log2_ceil(v);
  EXPECT_EQ(0ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_ceil_2) {
  constexpr auto v = 2ull;
  constexpr auto a = folly::constexpr_log2_ceil(v);
  EXPECT_EQ(1ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_ceil_3) {
  constexpr auto v = 3ull;
  constexpr auto a = folly::constexpr_log2_ceil(v);
  EXPECT_EQ(2ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_ceil_63) {
  constexpr auto v = 63ull;
  constexpr auto a = folly::constexpr_log2_ceil(v);
  EXPECT_EQ(6ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_log2_ceil_64) {
  constexpr auto v = 64ull;
  constexpr auto a = folly::constexpr_log2_ceil(v);
  EXPECT_EQ(6ull, a);
  EXPECT_TRUE((std::is_same<decltype(v), decltype(a)>::value));
}

TEST_F(ConstexprMathTest, constexpr_ceil) {
  {
    constexpr auto roundable = 20ull;
    constexpr auto round = 6ull;
    constexpr auto rounded = folly::constexpr_ceil(roundable, round);
    EXPECT_EQ(24ull, rounded);
  }
  {
    constexpr auto roundable = -20ll;
    constexpr auto round = 6ll;
    constexpr auto rounded = folly::constexpr_ceil(roundable, round);
    EXPECT_EQ(-18ll, rounded);
  }
  {
    constexpr auto roundable = -20ll;
    constexpr auto round = 0ll;
    constexpr auto rounded = folly::constexpr_ceil(roundable, round);
    EXPECT_EQ(-20ll, rounded);
  }
}

TEST_F(ConstexprMathTest, constexpr_pow) {
  {
    constexpr auto a = folly::constexpr_pow(uint64_t(0), 15);
    EXPECT_EQ(0, a);
  }
  {
    constexpr auto a = folly::constexpr_pow(uint64_t(15), 0);
    EXPECT_EQ(1, a);
  }
  {
    constexpr auto a = folly::constexpr_pow(uint64_t(2), 6);
    EXPECT_EQ(64, a);
  }
}
