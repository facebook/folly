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

#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <string>
#include <vector>

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// ===== Cross-Engine: Core Match Tests =====

TEST(RegexCrossEngineTest, SimpleLiteral) {
  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("world", false);
  expectMatchAllEngines<"hello">("hello world", false);
  expectMatchAllEngines<"hello">("", false);
}

TEST(RegexCrossEngineTest, EmptyPattern) {
  expectMatchAllEngines<"">("", true);
  expectMatchAllEngines<"">("a", false);
}

TEST(RegexCrossEngineTest, AnyChar) {
  expectMatchAllEngines<"a.b">("aXb", true);
  expectMatchAllEngines<"a.b">("a1b", true);
  expectMatchAllEngines<"a.b">("ab", false);
  expectMatchAllEngines<"a.b">("aXXb", false);
}

TEST(RegexCrossEngineTest, Anchors) {
  expectMatchAllEngines<"^hello$">("hello", true);
  expectMatchAllEngines<"^hello$">("hello world", false);
}

TEST(RegexCrossEngineTest, Alternation) {
  expectMatchAllEngines<"cat|dog">("cat", true);
  expectMatchAllEngines<"cat|dog">("dog", true);
  expectMatchAllEngines<"cat|dog">("bird", false);
}

TEST(RegexCrossEngineTest, StarQuantifier) {
  expectMatchAllEngines<"ab*c">("ac", true);
  expectMatchAllEngines<"ab*c">("abc", true);
  expectMatchAllEngines<"ab*c">("abbc", true);
  expectMatchAllEngines<"ab*c">("adc", false);
}

TEST(RegexCrossEngineTest, PlusQuantifier) {
  expectMatchAllEngines<"ab+c">("ac", false);
  expectMatchAllEngines<"ab+c">("abc", true);
  expectMatchAllEngines<"ab+c">("abbc", true);
}

TEST(RegexCrossEngineTest, QuestionQuantifier) {
  expectMatchAllEngines<"ab?c">("ac", true);
  expectMatchAllEngines<"ab?c">("abc", true);
  expectMatchAllEngines<"ab?c">("abbc", false);
}

TEST(RegexCrossEngineTest, CountedRepetition) {
  expectMatchAllEngines<"a{2,4}">("a", false);
  expectMatchAllEngines<"a{2,4}">("aa", true);
  expectMatchAllEngines<"a{2,4}">("aaa", true);
  expectMatchAllEngines<"a{2,4}">("aaaa", true);
  expectMatchAllEngines<"a{2,4}">("aaaaa", false);
}

TEST(RegexCrossEngineTest, ExactRepetition) {
  expectMatchAllEngines<"a{3}">("aa", false);
  expectMatchAllEngines<"a{3}">("aaa", true);
  expectMatchAllEngines<"a{3}">("aaaa", false);
}

TEST(RegexCrossEngineTest, UnescapedBraceLiteral) {
  expectMatchAllEngines<"a{b}c">("a{b}c", true);
  expectMatchAllEngines<"a{b}c">("abc", false);
}

TEST(RegexCrossEngineTest, UnescapedBraceEmptyBraces) {
  expectMatchAllEngines<"a{}b">("a{}b", true);
  expectMatchAllEngines<"a{}b">("ab", false);
}

TEST(RegexCrossEngineTest, UnescapedBraceAtEnd) {
  expectMatchAllEngines<"x = {">("x = {", true);
  expectMatchAllEngines<"x = {">("x = ", false);
}

TEST(RegexCrossEngineTest, CharClassBasic) {
  expectMatchAllEngines<"[abc]">("a", true);
  expectMatchAllEngines<"[abc]">("b", true);
  expectMatchAllEngines<"[abc]">("c", true);
  expectMatchAllEngines<"[abc]">("d", false);
}

TEST(RegexCrossEngineTest, CharClassRange) {
  expectMatchAllEngines<"[a-z]+">("hello", true);
  expectMatchAllEngines<"[a-z]+">("Hello", false);
  expectMatchAllEngines<"[a-z]+">("123", false);
}

TEST(RegexCrossEngineTest, CharClassNegated) {
  expectMatchAllEngines<"[^0-9]+">("hello", true);
  expectMatchAllEngines<"[^0-9]+">("123", false);
}

TEST(RegexCrossEngineTest, CharClassDash) {
  expectMatchAllEngines<"[-a]">("-", true);
  expectMatchAllEngines<"[-a]">("a", true);
  expectMatchAllEngines<"[-a]">("b", false);
}

TEST(RegexCrossEngineTest, ShorthandDigit) {
  expectMatchAllEngines<"\\d+">("123", true);
  expectMatchAllEngines<"\\d+">("abc", false);
}

TEST(RegexCrossEngineTest, ShorthandWord) {
  expectMatchAllEngines<"\\w+">("hello_123", true);
  expectMatchAllEngines<"\\w+">("hello world", false);
}

TEST(RegexCrossEngineTest, ShorthandSpace) {
  expectMatchAllEngines<"\\s+">("  \t\n", true);
  expectMatchAllEngines<"\\s+">("abc", false);
}

TEST(RegexCrossEngineTest, NegatedShorthand) {
  expectMatchAllEngines<"\\D+">("abc", true);
  expectMatchAllEngines<"\\D+">("123", false);
}

TEST(RegexCrossEngineTest, EscapeSequences) {
  expectMatchAllEngines<"a\\tb">("a\tb", true);
  expectMatchAllEngines<"a\\tb">("ab", false);
}

TEST(RegexCrossEngineTest, EscapedSpecialChars) {
  expectMatchAllEngines<"a\\.b">("a.b", true);
  expectMatchAllEngines<"a\\.b">("axb", false);
}

TEST(RegexCrossEngineTest, NonCaptureGroup) {
  expectMatchAllEngines<"(?:ab)+">("ab", true);
  expectMatchAllEngines<"(?:ab)+">("abab", true);
  expectMatchAllEngines<"(?:ab)+">("a", false);
}

// ===== Cross-Engine: Core Search Tests =====

TEST(RegexCrossEngineTest, SearchFindInMiddle) {
  expectSearchAllEngines<"\\d+">("abc 123 def", true, "123");
}

TEST(RegexCrossEngineTest, SearchFindAtStart) {
  expectSearchAllEngines<"hello">("hello world", true, "hello");
}

TEST(RegexCrossEngineTest, SearchNoMatch) {
  expectSearchAllEngines<"\\d+">("no digits here", false);
}

TEST(RegexCrossEngineTest, SearchAnchored) {
  expectSearchAllEngines<"^hello">("hello world", true, "hello");
  expectSearchAllEngines<"^hello">("say hello", false);
}

// ===== Cross-Engine: Capture Group Tests =====

TEST(RegexCrossEngineTest, SingleGroupCapture) {
  auto m = expectSearchCapturesAgree<"(\\d+)">("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
}

TEST(RegexCrossEngineTest, MultipleGroupCapture) {
  auto m = expectMatchCapturesAgree<"(\\d+)-(\\w+)">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123-hello");
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexCrossEngineTest, NestedGroupCapture) {
  auto m = expectMatchCapturesAgree<"((\\d+)-(\\w+))">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123-hello");
  EXPECT_EQ(m[2], "123");
  EXPECT_EQ(m[3], "hello");
}

// ===== Cross-Engine: Test Function =====

TEST(RegexCrossEngineTest, TestFunction) {
  expectTestAllEngines<"\\d+">("abc 123 def", true);
  expectTestAllEngines<"\\d+">("no digits", false);
}

// ===== Cross-Engine: Quantifier Edge Cases =====

TEST(RegexCrossEngineTest, LazyStarVsGreedy) {
  expectMatchAllEngines<"a.*b">("aXXbYYb", true);
  expectMatchAllEngines<"a.*?b">("aXXbYYb", true);
}

TEST(RegexCrossEngineTest, OpenEndedRepetition) {
  expectMatchAllEngines<"a{2,}">("a", false);
  expectMatchAllEngines<"a{2,}">("aa", true);
  expectMatchAllEngines<"a{2,}">("aaa", true);
  expectMatchAllEngines<"a{2,}">("aaaaaaa", true);
}

// ===== Cross-Engine: Edge Cases =====

TEST(RegexCrossEngineTest, EmptyInput) {
  expectMatchAllEngines<"a*">("", true);
}

TEST(RegexCrossEngineTest, SingleCharPattern) {
  expectMatchAllEngines<"a">("a", true);
  expectMatchAllEngines<"a">("b", false);
  expectMatchAllEngines<"a">("", false);
}

TEST(RegexCrossEngineTest, AlternationWithEmpty) {
  expectMatchAllEngines<"a|">("a", true);
  expectMatchAllEngines<"a|">("", true);
}

// ===== Cross-Engine: Hex Escapes =====

TEST(RegexCrossEngineTest, HexEscapeTwoDigit) {
  expectMatchAllEngines<R"(\x41)">("A", true);
  expectMatchAllEngines<R"(\x41)">("B", false);
}

TEST(RegexCrossEngineTest, HexEscapeInCharClass) {
  expectMatchAllEngines<R"([\x41-\x5A]+)">("ABC", true);
  expectMatchAllEngines<R"([\x41-\x5A]+)">("abc", false);
}

// ===== Cross-Engine: Octal/Bell Escapes =====

TEST(RegexCrossEngineTest, OctalEscape) {
  expectMatchAllEngines<R"(\012)">("\n", true);
  expectMatchAllEngines<R"(\012)">("a", false);
}

TEST(RegexCrossEngineTest, BellEscape) {
  expectMatchAllEngines<R"(\a)">("\a", true);
  expectMatchAllEngines<R"(\a)">("a", false);
}

// ===== Cross-Engine: Shorthand in Char Classes =====

TEST(RegexCrossEngineTest, ShorthandDigitInCharClass) {
  expectMatchAllEngines<R"([\d]+)">("123", true);
  expectMatchAllEngines<R"([\d]+)">("abc", false);
}

TEST(RegexCrossEngineTest, ShorthandWordInCharClass) {
  expectMatchAllEngines<R"([\w]+)">("abc123_", true);
  expectMatchAllEngines<R"([\w]+)">("!@#", false);
}

TEST(RegexCrossEngineTest, ShorthandSpaceInCharClass) {
  expectMatchAllEngines<R"([\s]+)">(" \t\n", true);
  expectMatchAllEngines<R"([\s]+)">("abc", false);
}

TEST(RegexCrossEngineTest, MixedShorthandInCharClass) {
  expectMatchAllEngines<R"([\d\s]+)">("1 2 3", true);
  expectMatchAllEngines<R"([\d\s]+)">("abc", false);
}

TEST(RegexCrossEngineTest, NegatedShorthandInCharClass) {
  expectMatchAllEngines<R"([\D]+)">("abc", true);
  expectMatchAllEngines<R"([\D]+)">("123", false);
}

TEST(RegexCrossEngineTest, NegatedCharClassComplement) {
  expectMatchAllEngines<"[^abc]">("d", true);
  expectMatchAllEngines<"[^abc]">("a", false);
  expectMatchAllEngines<"[^abc]">("b", false);
  expectMatchAllEngines<"[^a-z]">("1", true);
  expectMatchAllEngines<"[^a-z]">("a", false);
  expectSearchAllEngines<"[^a-z]+">("abc123def", true, "123");
}

TEST(RegexCrossEngineTest, NegatedShorthandComplement) {
  expectMatchAllEngines<"\\D">("a", true);
  expectMatchAllEngines<"\\D">("1", false);
  expectMatchAllEngines<"\\W">("!", true);
  expectMatchAllEngines<"\\W">("a", false);
  expectMatchAllEngines<"\\S">("a", true);
  expectMatchAllEngines<"\\S">(" ", false);
}

TEST(RegexCrossEngineTest, NegatedCharClassInAlternation) {
  expectSearchAllEngines<"[^a]|[^b]">("a", true);
  expectSearchAllEngines<"[^a]|[^b]">("c", true);
}

TEST(RegexCrossEngineTest, FlatSequenceCaptures) {
  // Captures in non-backtracking (flat) children
  constexpr auto re = compile<"(abc)(def)">();
  auto m = re.match("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
  EXPECT_EQ(m[2], "def");

  // Captures with backtracking child in the middle
  constexpr auto re2 = compile<"(abc)([a-z]+)(xyz)">();
  auto m2 = re2.match("abcdefxyz");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "abc");
  EXPECT_EQ(m2[2], "def");
  EXPECT_EQ(m2[3], "xyz");
}

TEST(RegexCrossEngineTest, FlatSequenceStateRestore) {
  // (abc)(def)ghi — if trailing fails, captures must be rolled back
  constexpr auto re = compile<"(abc)(def)ghi">();
  auto m = re.match("abcdefghi");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
  EXPECT_EQ(m[2], "def");

  // Failed match — captures should not leak
  auto m2 = re.match("abcdefxxx");
  EXPECT_FALSE(m2);
}

TEST(RegexCrossEngineTest, FlatSequenceFixedWidthAlternation) {
  // Multi-char alternation branches with equal fixed width are
  // non-backtracking: each branch consumes exactly 3 chars.
  // Single-char alternations get optimized to CharClass, so use
  // multi-char branches with mostly disjoint prefixes.
  expectMatchAllEngines<"(abc|def|ghi)xyz">("abcxyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("defxyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("ghixyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("abcdef", false);

  // Mixed-width branches are NOT non-backtracking
  // (ab|cde) has widths 2 and 3 — must stay in continuation-nesting
  expectMatchAllEngines<"(ab|cde)f">("abf", true);
  expectMatchAllEngines<"(ab|cde)f">("cdef", true);

  // Fixed-width alternation with captures
  constexpr auto re = compile<"(abc|def)(ghi|jkl)!">();
  auto m3 = re.match("abcjkl!");
  EXPECT_TRUE(m3);
  EXPECT_EQ(m3[1], "abc");
  EXPECT_EQ(m3[2], "jkl");

  auto m4 = re.match("defghi!");
  EXPECT_TRUE(m4);
  EXPECT_EQ(m4[1], "def");
  EXPECT_EQ(m4[2], "ghi");
}

TEST(RegexCrossEngineTest, UnescapedBraceWithGroup) {
  expectSearchAllEngines<"{error_code: (\\d+)}">("{error_code: 42}", true);
  expectSearchAllEngines<"{error_code: (\\d+)}">("error_code: 42", false);
}

TEST(RegexCrossEngineTest, HexEscapeBraced) {
  expectMatchAllEngines<R"(\x{0A})">("\n", true);
  expectMatchAllEngines<R"(\x{0A})">("a", false);
}
