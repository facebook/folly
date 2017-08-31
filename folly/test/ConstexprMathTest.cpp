/*
 * Copyright 2017 Facebook, Inc.
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
