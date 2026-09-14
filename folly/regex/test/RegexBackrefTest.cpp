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
// Multi-digit backreferences \N for N >= 10. folly diverges from Perl:
// folly always parses \NN as a backref and errors if the group does not
// exist. To match a literal byte by octal value, use \0NN.
// =====================================================================

TEST(RegexBackrefTest, BackrefToGroup10) {
  using R = Regex<R"((.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\10)">;
  EXPECT_TRUE(bool(R::match("abcdefghijj")));
  EXPECT_FALSE(bool(R::match("abcdefghijx")));
}

TEST(RegexBackrefTest, BackrefToGroup11) {
  using R = Regex<R"((.)(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\11)">;
  EXPECT_TRUE(bool(R::match("abcdefghijkk")));
  EXPECT_FALSE(bool(R::match("abcdefghijkx")));
}

TEST(RegexBackrefTest, MultiDigitBackrefNonexistentGroupErrors) {
  // (a)\12 in folly is a parse error (no octal fallback). To get \012
  // (newline), use \012 explicitly.
  auto result =
      parse<5, ParseErrorMode::RuntimeReport>(std::string_view(R"((a)\12)", 6));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, ExplicitOctalEscapeStillWorks) {
  // (a)\012 — \012 is octal newline; group 1 is 'a'.
  EXPECT_TRUE(bool(Regex<R"((a)\012)">::match("a\n")));
}

TEST(RegexBackrefTest, SingleDigitBackrefStillWorks) {
  EXPECT_TRUE(bool(Regex<R"((a)\1)">::match("aa")));
  EXPECT_FALSE(bool(Regex<R"((a)\1)">::match("ab")));
}

// =====================================================================
// \g forms: \gN, \g{N}, \g{-N} (relative).
// =====================================================================

TEST(RegexBackrefTest, GBareDigitForm) {
  EXPECT_TRUE(bool(Regex<R"((a)\g1)">::match("aa")));
}

TEST(RegexBackrefTest, GBracedDigitForm) {
  EXPECT_TRUE(bool(Regex<R"((a)\g{1})">::match("aa")));
}

TEST(RegexBackrefTest, GRelativeMostRecent) {
  // \g{-1} = most recent group.
  EXPECT_TRUE(bool(Regex<R"((a)\g{-1})">::match("aa")));
}

TEST(RegexBackrefTest, GRelativeNthBack) {
  // \g{-2} with 2 groups = group 1.
  EXPECT_TRUE(bool(Regex<R"((a)(b)\g{-2})">::match("aba")));
  EXPECT_FALSE(bool(Regex<R"((a)(b)\g{-2})">::match("abb")));
}

TEST(RegexBackrefTest, GRelativeMostRecentTwoGroups) {
  // \g{-1} with 2 groups = group 2.
  EXPECT_TRUE(bool(Regex<R"((a)(b)\g{-1})">::match("abb")));
  EXPECT_FALSE(bool(Regex<R"((a)(b)\g{-1})">::match("aba")));
}

TEST(RegexBackrefTest, GMultiDigitGroup) {
  using R = Regex<R"((.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\g{10})">;
  EXPECT_TRUE(bool(R::match("abcdefghijj")));
}

TEST(RegexBackrefTest, GBracedNonexistentGroupErrors) {
  auto result = parse<8, ParseErrorMode::RuntimeReport>(
      std::string_view(R"((a)\g{2})", 8));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GRelativeExceedsCountErrors) {
  auto result = parse<11, ParseErrorMode::RuntimeReport>(
      std::string_view(R"((a)(b)\g{-3})", 12));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GBracedZeroErrors) {
  auto result =
      parse<6, ParseErrorMode::RuntimeReport>(std::string_view(R"(\g{0})", 5));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GBareZeroErrors) {
  auto result =
      parse<3, ParseErrorMode::RuntimeReport>(std::string_view(R"(\g0)", 3));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GBracedEmptyErrors) {
  auto result =
      parse<4, ParseErrorMode::RuntimeReport>(std::string_view(R"(\g{})", 4));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GRelativeNegZeroErrors) {
  auto result =
      parse<6, ParseErrorMode::RuntimeReport>(std::string_view(R"(\g{-0})", 6));
  EXPECT_FALSE(result.valid);
}

// =====================================================================
// \g{name} (named form): resolves through the same path as \k<name>.
// =====================================================================

TEST(RegexBackrefTest, GNamedPcreSyntax) {
  EXPECT_TRUE(bool(Regex<R"((?<word>a)\g{word})">::match("aa")));
  EXPECT_FALSE(bool(Regex<R"((?<word>a)\g{word})">::match("ab")));
}

TEST(RegexBackrefTest, GNamedPythonSyntax) {
  EXPECT_TRUE(bool(Regex<R"((?P<word>a)\g{word})">::match("aa")));
}

TEST(RegexBackrefTest, GNamedTwoGroups) {
  EXPECT_TRUE(bool(Regex<R"((?<a>x)(?<b>y)\g{a}\g{b})">::match("xyxy")));
}

TEST(RegexBackrefTest, GNamedNonexistentErrors) {
  auto result = parse<16, ParseErrorMode::RuntimeReport>(
      std::string_view(R"((a)\g{nonexistent})", 18));
  EXPECT_FALSE(result.valid);
}

TEST(RegexBackrefTest, GBracedDigitsThenLettersErrors) {
  // `\g{1bad}` — digit start means numeric form, but `}` doesn't follow
  // the digits, so the close-brace check fails.
  auto result = parse<8, ParseErrorMode::RuntimeReport>(
      std::string_view(R"(\g{1bad})", 8));
  EXPECT_FALSE(result.valid);
}

// =====================================================================
// AST verification.
// =====================================================================

TEST(RegexBackrefTest, MultiDigitBackrefProducesBackrefNode) {
  // `(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\10` should produce one Backref
  // node with group_id == 10.
  constexpr auto& ast = Regex<R"((.)(.)(.)(.)(.)(.)(.)(.)(.)(.)\10)">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  constexpr int idx = findNodeOfKind(ast, NodeKind::Backref);
  static_assert(idx >= 0);
  static_assert(ast.nodes[idx].group_id == 10);
}

TEST(RegexBackrefTest, OctalEscapeProducesLiteralNotBackref) {
  // `(a)\012` should produce a Literal containing \012, NOT a backref.
  constexpr auto& ast = Regex<R"((a)\012)">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 0);
}

TEST(RegexBackrefTest, GBracedNumericProducesBackref) {
  constexpr auto& ast = Regex<R"((a)\g{1})">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  constexpr int idx = findNodeOfKind(ast, NodeKind::Backref);
  static_assert(ast.nodes[idx].group_id == 1);
}

TEST(RegexBackrefTest, GRelativeResolvesToCorrectGroupId) {
  // `(a)(b)\g{-2}` → group_id 1 (group `a`).
  constexpr auto& ast = Regex<R"((a)(b)\g{-2})">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  constexpr int idx = findNodeOfKind(ast, NodeKind::Backref);
  static_assert(ast.nodes[idx].group_id == 1);
}

TEST(RegexBackrefTest, GBracedNamedProducesBackref) {
  constexpr auto& ast = Regex<R"((?<word>a)\g{word})">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  constexpr int idx = findNodeOfKind(ast, NodeKind::Backref);
  static_assert(ast.nodes[idx].group_id == 1);
}

// =====================================================================
// Combined with case-insensitive flag.
// =====================================================================

TEST(RegexBackrefTest, MultiDigitBackrefCaseInsensitive) {
  using R = Regex<
      R"((\w)(\w)(\w)(\w)(\w)(\w)(\w)(\w)(\w)(\w)\10)",
      Flags::CaseInsensitive>;
  EXPECT_TRUE(bool(R::match("abcdefghijJ")));
}

TEST(RegexBackrefTest, GRelativeCaseInsensitive) {
  using R = Regex<R"((\w+)\g{-1})", Flags::CaseInsensitive>;
  EXPECT_TRUE(bool(R::match("HelloHELLO")));
}

TEST(RegexBackrefTest, GBackrefUnderCaseInsensitiveProducesCIBackref) {
  constexpr auto& ast = Regex<R"((a)\g{1})", Flags::CaseInsensitive>::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 0);
}
