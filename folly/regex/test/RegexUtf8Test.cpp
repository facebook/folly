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

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Parser.h>
#include <folly/regex/test/AstTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::NodeKind;
using detail::parse;
using detail::ParseErrorMode;

// =====================================================================
// \x{N} codepoint emission. folly is a byte engine: \x{N} for N > 0x7F
// emits the UTF-8 byte sequence for the codepoint as a Literal. Inside
// character classes and under CaseInsensitive, multi-byte values error.
// =====================================================================

TEST(RegexUtf8Test, AsciiCodepointEmitsSingleByte) {
  // \x{20} = ASCII space.
  EXPECT_TRUE(bool(Regex<R"(\x{20})">::match(" ")));
  EXPECT_FALSE(bool(Regex<R"(\x{20})">::match("a")));
  // \x{7F} = DEL.
  EXPECT_TRUE(bool(Regex<R"(\x{7F})">::match("\x7F")));
}

TEST(RegexUtf8Test, NullByteCodepoint) {
  // \x{0} = single null byte.
  EXPECT_TRUE(bool(Regex<R"(\x{0})">::match(std::string_view("\0", 1))));
}

TEST(RegexUtf8Test, TwoByteCodepointEmitsUtf8) {
  // U+00E9 (é) = 0xC3 0xA9 in UTF-8.
  EXPECT_TRUE(bool(Regex<R"(\x{E9})">::match("\xC3\xA9")));
  EXPECT_FALSE(bool(Regex<R"(\x{E9})">::match("\xE9"))); // raw byte ≠ UTF-8
}

TEST(RegexUtf8Test, BoundaryAtU0080) {
  // U+0080 = 0xC2 0x80 in UTF-8 (first 2-byte codepoint).
  EXPECT_TRUE(bool(Regex<R"(\x{80})">::match("\xC2\x80")));
}

TEST(RegexUtf8Test, ThreeByteCodepointEmitsUtf8) {
  // U+4F60 (你) = 0xE4 0xBD 0xA0 in UTF-8.
  EXPECT_TRUE(bool(Regex<R"(\x{4F60})">::match("\xE4\xBD\xA0")));
}

TEST(RegexUtf8Test, FourByteCodepointEmitsUtf8) {
  // U+1F600 (😀) = 0xF0 0x9F 0x98 0x80 in UTF-8.
  EXPECT_TRUE(bool(Regex<R"(\x{1F600})">::match("\xF0\x9F\x98\x80")));
}

TEST(RegexUtf8Test, MultiByteWithSurroundingLiterals) {
  EXPECT_TRUE(
      bool(Regex<R"(hello\x{1F600}world)">::match(
          "hello\xF0\x9F\x98\x80world")));
}

TEST(RegexUtf8Test, SearchFindsMultiByteInLongerString) {
  auto m = Regex<R"(\x{1F600})">::search("text \xF0\x9F\x98\x80 more");
  ASSERT_TRUE(bool(m));
  EXPECT_EQ(m[0], "\xF0\x9F\x98\x80");
}

TEST(RegexUtf8Test, MultiByteParticipatesInPrefixExtraction) {
  // The 4 UTF-8 bytes for U+1F600 should end up in the Literal node's
  // bytes, observable via prefix extraction or the literal_buf.
  constexpr auto& ast = Regex<R"(\x{1F600}abc)">::parsed_;
  // The four UTF-8 bytes should be at the start of the matched content;
  // the prefix should include the UTF-8 bytes plus "abc".
  static_assert(ast.prefix_len >= 4);
  static_assert(ast.literal_buf[0] == static_cast<char>(0xF0));
  static_assert(ast.literal_buf[1] == static_cast<char>(0x9F));
  static_assert(ast.literal_buf[2] == static_cast<char>(0x98));
  static_assert(ast.literal_buf[3] == static_cast<char>(0x80));
}

// =====================================================================
// Error cases.
// =====================================================================

TEST(RegexUtf8Test, CodepointTooLargeErrors) {
  auto result = parse<8, ParseErrorMode::RuntimeReport>(
      std::string_view(R"(\x{110000})", 10));
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, MultiByteWithCaseInsensitiveErrors) {
  auto result = parse<8, ParseErrorMode::RuntimeReport>(
      std::string_view(R"(\x{1F600})", 9), Flags::CaseInsensitive);
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, NonAsciiAsciiBoundaryWithCaseInsensitiveErrors) {
  // Anything > 0x7F errors under CaseInsensitive.
  auto result = parse<8, ParseErrorMode::RuntimeReport>(
      std::string_view(R"(\x{80})", 6), Flags::CaseInsensitive);
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, AsciiCodepointWorksUnderCaseInsensitive) {
  // \x{41} = 'A'. Under CaseInsensitive this should match both 'A' and 'a'
  // via case-folding.
  EXPECT_TRUE(bool(Regex<R"(\x{41})", Flags::CaseInsensitive>::match("A")));
  EXPECT_TRUE(bool(Regex<R"(\x{41})", Flags::CaseInsensitive>::match("a")));
}

TEST(RegexUtf8Test, MultiByteInCharClassErrors) {
  auto result = parse<10, ParseErrorMode::RuntimeReport>(
      std::string_view(R"([\x{1F600}])", 11));
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, SingleByteInCharClassWorks) {
  // Within a char class, \x{NN} for N <= 0xFF is fine — it goes into
  // the class as a single byte.
  EXPECT_TRUE(bool(Regex<R"([\x{41}])">::match("A")));
  EXPECT_FALSE(bool(Regex<R"([\x{41}])">::match("B")));
}

TEST(RegexUtf8Test, EmptyBracedHexErrors) {
  auto result =
      parse<4, ParseErrorMode::RuntimeReport>(std::string_view(R"(\x{})", 4));
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, UnclosedHexErrors) {
  auto result =
      parse<4, ParseErrorMode::RuntimeReport>(std::string_view(R"(\x{20)", 5));
  EXPECT_FALSE(result.valid);
}

TEST(RegexUtf8Test, InvalidHexDigitErrors) {
  auto result =
      parse<6, ParseErrorMode::RuntimeReport>(std::string_view(R"(\x{2g})", 6));
  EXPECT_FALSE(result.valid);
}

// =====================================================================
// AST structure verification.
// =====================================================================

TEST(RegexUtf8Test, AsciiSingleByteProducesLiteralNode) {
  constexpr auto& ast = Regex<R"(\x{20})">::parsed_;
  // Single-byte literals can be extracted as a prefix.
  static_assert(ast.prefix_len == 1);
  static_assert(ast.literal_buf[0] == ' ');
}

TEST(RegexUtf8Test, MultiByteEmitsLiteralWithUtf8Bytes) {
  // \x{4F60} (你) = 0xE4 0xBD 0xA0
  constexpr auto& ast = Regex<R"(\x{4F60})">::parsed_;
  // The 3 UTF-8 bytes should be in the prefix.
  static_assert(ast.prefix_len == 3);
  static_assert(ast.literal_buf[0] == static_cast<char>(0xE4));
  static_assert(ast.literal_buf[1] == static_cast<char>(0xBD));
  static_assert(ast.literal_buf[2] == static_cast<char>(0xA0));
}
