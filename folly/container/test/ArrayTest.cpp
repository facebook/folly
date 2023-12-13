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

#include <folly/container/Array.h>

#include <string>

#include <folly/portability/GTest.h>

using namespace std;
using folly::make_array;

TEST(makeArray, baseCase) {
  auto arr = make_array<int>();
  static_assert(
      is_same<typename decltype(arr)::value_type, int>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 0);
}

TEST(makeArray, deduceSizePrimitive) {
  auto arr = make_array<int>(1, 2, 3, 4, 5);
  static_assert(
      is_same<typename decltype(arr)::value_type, int>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 5);
}

TEST(makeArray, deduceSizeClass) {
  auto arr = make_array<string>(string{"foo"}, string{"bar"});
  static_assert(
      is_same<typename decltype(arr)::value_type, std::string>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 2);
  EXPECT_EQ(arr[1], "bar");
}

TEST(makeArray, deduceEverything) {
  auto arr = make_array(string{"foo"}, string{"bar"});
  static_assert(
      is_same<typename decltype(arr)::value_type, std::string>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 2);
  EXPECT_EQ(arr[1], "bar");
}

TEST(makeArray, fixedCommonType) {
  auto arr = make_array<double>(1.0, 2.5f, 3, 4, 5);
  static_assert(
      is_same<typename decltype(arr)::value_type, double>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 5);
}

TEST(makeArray, deducedCommonType) {
  auto arr = make_array(1.0, 2.5f, 3, 4, 5);
  static_assert(
      is_same<typename decltype(arr)::value_type, double>::value,
      "Wrong array type");
  EXPECT_EQ(arr.size(), 5);
}

TEST(makeArrayWith, example) {
  struct make_item {
    constexpr int operator()(size_t index) const { return index + 4; }
  };

  constexpr auto actual = folly::make_array_with<3>(make_item{});
  constexpr auto expected = make_array<int>(4, 5, 6);
  EXPECT_EQ(expected, actual);
}
