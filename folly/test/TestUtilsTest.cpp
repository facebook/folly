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

#include <folly/test/TestUtils.h>

#include <folly/portability/GTest.h>

using namespace folly;
using namespace std::string_literals;
using ::testing::PrintToString;

TEST(TestUtilsFbStringTest, Ascii) {
  const auto kHelloFbString = fbstring("hello");
  const auto kHelloString = "hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFbString));
}

TEST(TestUtilsFbStringTest, Wide) {
  const auto kHelloFbString = basic_fbstring<wchar_t>(L"hello");
  const auto kHelloString = L"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFbString));
}

TEST(TestUtilsFbStringTest, Utf16) {
  const auto kHelloFbString = basic_fbstring<char16_t>(u"hello");
  const auto kHelloString = u"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFbString));
}

TEST(TestUtilsFbStringTest, Utf32) {
  const auto kHelloFbString = basic_fbstring<char32_t>(U"hello");
  const auto kHelloString = U"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFbString));
}

TEST(TestUtilsFixedStringTest, Ascii) {
  constexpr auto kHelloFixedString = makeFixedString("hello");
  const auto kHelloString = "hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFixedString));
}

TEST(TestUtilsFixedStringTest, Wide) {
  constexpr auto kHelloFixedString = makeFixedString(L"hello");
  const auto kHelloString = L"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFixedString));
}

TEST(TestUtilsFixedStringTest, Utf16) {
  constexpr auto kHelloFixedString = makeFixedString(u"hello");
  const auto kHelloString = u"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFixedString));
}

TEST(TestUtilsFixedStringTest, Utf32) {
  constexpr auto kHelloFixedString = makeFixedString(U"hello");
  const auto kHelloString = U"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloFixedString));
}

TEST(TestUtilsRangeTest, Ascii) {
  constexpr auto kHelloStringPiece = "hello"_sp;
  const auto kHelloString = "hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}

TEST(TestUtilsRangeTest, Wide) {
  constexpr auto kHelloStringPiece = L"hello"_sp;
  const auto kHelloString = L"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}

TEST(TestUtilsRangeTest, Utf16) {
  constexpr auto kHelloStringPiece = u"hello"_sp;
  const auto kHelloString = u"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}

TEST(TestUtilsRangeTest, Utf32) {
  constexpr auto kHelloStringPiece = U"hello"_sp;
  const auto kHelloString = U"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}
