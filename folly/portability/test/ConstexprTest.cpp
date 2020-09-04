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

#include <folly/portability/Constexpr.h>

#include <folly/portability/GTest.h>

using folly::constexpr_strcmp;
using folly::constexpr_strlen;
using folly::detail::constexpr_strcmp_fallback;
using folly::detail::constexpr_strlen_fallback;

TEST(ConstexprTest, constexpr_strlen_cstr) {
  constexpr auto v = "hello";
  {
    constexpr auto a = constexpr_strlen(v);
    EXPECT_EQ(5, a);
    EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
  }
  {
    constexpr auto a = constexpr_strlen_fallback(v);
    EXPECT_EQ(5, a);
    EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
  }
}

TEST(ConstexprTest, constexpr_strlen_ints) {
  constexpr int v[] = {5, 3, 4, 0, 7};
  {
    constexpr auto a = constexpr_strlen(v);
    EXPECT_EQ(3, a);
    EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
  }
  {
    constexpr auto a = constexpr_strlen_fallback(v);
    EXPECT_EQ(3, a);
    EXPECT_TRUE((std::is_same<const size_t, decltype(a)>::value));
  }
}

TEST(ConstexprTest, constexpr_strcmp_ints) {
  constexpr int v[] = {5, 3, 4, 0, 7};
  constexpr int v1[] = {6, 4};
  static_assert(constexpr_strcmp(v1, v) > 0, "constexpr_strcmp is broken");
  static_assert(constexpr_strcmp(v, v) == 0, "constexpr_strcmp is broken");
  static_assert(
      constexpr_strcmp_fallback(v1, v) > 0, "constexpr_strcmp is broken");
  static_assert(
      constexpr_strcmp_fallback(v, v) == 0, "constexpr_strcmp is broken");
}

static_assert(
    constexpr_strcmp("abc", "abc") == 0,
    "constexpr_strcmp is broken");
static_assert(constexpr_strcmp("", "") == 0, "constexpr_strcmp is broken");
static_assert(constexpr_strcmp("abc", "def") < 0, "constexpr_strcmp is broken");
static_assert(constexpr_strcmp("xyz", "abc") > 0, "constexpr_strcmp is broken");
static_assert(constexpr_strcmp("a", "abc") < 0, "constexpr_strcmp is broken");
static_assert(constexpr_strcmp("abc", "a") > 0, "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("abc", "abc") == 0,
    "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("", "") == 0,
    "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("abc", "def") < 0,
    "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("xyz", "abc") > 0,
    "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("a", "abc") < 0,
    "constexpr_strcmp is broken");
static_assert(
    constexpr_strcmp_fallback("abc", "a") > 0,
    "constexpr_strcmp is broken");
