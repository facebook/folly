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

#include <folly/test/TestUtils.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace std::string_literals;
using ::testing::MatchesRegex;
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

// gtest-printer.h has added an overload to PrintTo for u16string, which is
// present in platform010 but not present in platform009:
//   void PrintTo(const ::std::u16string& s, ::std::ostream* os)
// Before this overload existed, u16string was printed as an array of unicode
// characters, "{ U+0068, U+0065, U+006C, U+006C, U+006F }", but this has been
// prettified to "u\"hello\"". As such, it no longer matches the string piece
// printer.
// This is not worth fixing: adding a PrintTo overload for Range<const char16_t>
// and friends would be liable to break when gtest further changes their
// printer. Disable the test; no other folly tests are broken on account of this
// PrintTo discrepancy.
TEST(TestUtilsRangeTest, DISABLEDUtf16) {
  constexpr auto kHelloStringPiece = u"hello"_sp;
  const auto kHelloString = u"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}

// Also broken on platform010, as with TestUtilsRangeTest.Utf16
TEST(TestUtilsRangeTest, DISABLEDUtf32) {
  constexpr auto kHelloStringPiece = U"hello"_sp;
  const auto kHelloString = U"hello"s;
  EXPECT_EQ(PrintToString(kHelloString), PrintToString(kHelloStringPiece));
}

TEST(ExpectThrowRegex, SuccessCases) {
  EXPECT_THROW_RE(throw std::runtime_error("test"), std::runtime_error, "test");
  EXPECT_THROW_RE(
      throw std::invalid_argument("test 123"), std::invalid_argument, ".*123");
  EXPECT_THROW_RE(
      throw std::invalid_argument("test 123"),
      std::invalid_argument,
      std::string("t.*123"));
}

TEST(ExpectThrowRegex, FailureCases) {
  std::string failureMsg;
  auto recordFailure = [&](const char* msg) { failureMsg = msg; };

  TEST_THROW_RE_(
      throw std::runtime_error("test"),
      std::invalid_argument,
      "test",
      recordFailure);
  EXPECT_THAT(
      failureMsg,
      MatchesRegex(".*Actual: it throws a different exception type.*"));

  failureMsg = "";
  TEST_THROW_RE_(
      throw std::runtime_error("test"),
      std::runtime_error,
      "xest",
      recordFailure);
  EXPECT_THAT(
      failureMsg,
      MatchesRegex("Expected:.* throws a std::runtime_error with message "
                   "matching \"xest\".*Actual: message is: test"));

  failureMsg = "";
  TEST_THROW_RE_(
      throw std::runtime_error("abc"),
      std::runtime_error,
      std::string("xyz"),
      recordFailure);
  EXPECT_THAT(
      failureMsg,
      MatchesRegex("Expected:.* throws a std::runtime_error with message "
                   "matching \"xyz\".*Actual: message is: abc"));
}
