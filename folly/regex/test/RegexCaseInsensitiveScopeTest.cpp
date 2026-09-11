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
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::AnchorKind;
using detail::NodeKind;
using detail::parse;
using detail::ParseErrorMode;

// =====================================================================
// Inline flag syntax tests.
// =====================================================================

TEST(RegexCaseInsensitiveTest, InlineCaseInsensitiveDirective) {
  expectMatchAllEngines<"(?i)hello">("HELLO", true);
  expectMatchAllEngines<"(?i)hello">("hello", true);
  expectMatchAllEngines<"(?i)hello">("HeLLo", true);
  expectMatchAllEngines<"(?i)hello">("nope", false);
}

TEST(RegexCaseInsensitiveTest, InlineScopedCaseInsensitive) {
  // (?i:hello) is case-insensitive; "world" outside the group is not.
  expectMatchAllEngines<"(?i:hello)world">("helloworld", true);
  expectMatchAllEngines<"(?i:hello)world">("HELLOworld", true);
  expectMatchAllEngines<"(?i:hello)world">("HeLLoworld", true);
  expectMatchAllEngines<"(?i:hello)world">("HELLOWORLD", false);
  expectMatchAllEngines<"(?i:hello)world">("helloWORLD", false);
}

TEST(RegexCaseInsensitiveTest, InlineSequenceLevelDirective) {
  // abc(?i)def — abc is case-sensitive, def becomes case-insensitive.
  expectMatchAllEngines<"abc(?i)def">("abcdef", true);
  expectMatchAllEngines<"abc(?i)def">("abcDEF", true);
  expectMatchAllEngines<"abc(?i)def">("abcDeF", true);
  expectMatchAllEngines<"abc(?i)def">("ABCdef", false);
  expectMatchAllEngines<"abc(?i)def">("ABCDEF", false);
}

TEST(RegexCaseInsensitiveTest, CrossAlternativePropagation) {
  // a|b(?i)c|d — verified against Perl: (?i) propagates to the third
  // alternative, so "d" is also case-insensitive.
  expectSearchAllEngines<"a|b(?i)c|d">("a", true, "a");
  expectSearchAllEngines<"a|b(?i)c|d">("A", false);
  expectSearchAllEngines<"a|b(?i)c|d">("bC", true, "bC");
  expectSearchAllEngines<"a|b(?i)c|d">("Bc", false);
  expectSearchAllEngines<"a|b(?i)c|d">("d", true, "d");
  expectSearchAllEngines<"a|b(?i)c|d">("D", true, "D");
}

TEST(RegexCaseInsensitiveTest, GroupBoundaryStopsPropagation) {
  // (a|b(?i)c|d)e — flag change is contained within the group; "e" is
  // case-sensitive.
  expectMatchAllEngines<"(a|b(?i)c|d)e">("ae", true);
  expectMatchAllEngines<"(a|b(?i)c|d)e">("Ae", false);
  expectMatchAllEngines<"(a|b(?i)c|d)e">("De", true);
  expectMatchAllEngines<"(a|b(?i)c|d)e">("DE", false);
  expectMatchAllEngines<"(a|b(?i)c|d)e">("dE", false);
}

TEST(RegexCaseInsensitiveTest, FlagUnsettingDirective) {
  // (?i)abc(?-i)def — abc CI, def CS.
  expectMatchAllEngines<"(?i)abc(?-i)def">("abcdef", true);
  expectMatchAllEngines<"(?i)abc(?-i)def">("ABCdef", true);
  expectMatchAllEngines<"(?i)abc(?-i)def">("AbCdef", true);
  expectMatchAllEngines<"(?i)abc(?-i)def">("abcDEF", false);
  expectMatchAllEngines<"(?i)abc(?-i)def">("ABCDEF", false);
}

TEST(RegexCaseInsensitiveTest, CombinedFlagsInDirective) {
  // (?im) sets both case-insensitive and multiline.
  expectSearchAllEngines<"(?im)^hello$">("HELLO", true);
  expectSearchAllEngines<"(?im)^hello$">(
      std::string_view("xxx\nHELLO\nyyy", 13), true);
  // Without 'i' — no match against uppercase.
  expectSearchAllEngines<"(?m)^hello$">(
      std::string_view("xxx\nHELLO\nyyy", 13), false);
}

TEST(RegexCaseInsensitiveTest, CombinedSetClearInDirective) {
  // (?i-m) — set i, clear m.
  expectMatchAllEngines<"(?i-m)hello">("HELLO", true);
}

TEST(RegexCaseInsensitiveTest, MultilineInlineDirective) {
  expectSearchAllEngines<"(?m)^hello">(
      std::string_view("first\nhello\nlast", 16), true);
}

TEST(RegexCaseInsensitiveTest, DotAllInlineDirective) {
  expectMatchAllEngines<"(?s)a.b">(std::string_view("a\nb", 3), true);
}

TEST(RegexCaseInsensitiveTest, ScopedCaseInsensitiveDoesNotLeak) {
  // (?i:abc)(?i:def)ghi — both groups are CI, ghi is CS.
  expectMatchAllEngines<"(?i:abc)(?i:def)ghi">("ABCDEFghi", true);
  expectMatchAllEngines<"(?i:abc)(?i:def)ghi">("ABCDEFGHI", false);
}

TEST(RegexCaseInsensitiveTest, AtomicGroupScopesFlagChange) {
  // Verified against Perl: (?>(?i)abc) with a (?i) directive inside an
  // atomic group does NOT leak the flag change to following characters,
  // matching the behavior of regular groups.
  expectMatchAllEngines<"(?>(?i)abc)def">("abcdef", true);
  expectMatchAllEngines<"(?>(?i)abc)def">("ABCdef", true);
  expectMatchAllEngines<"(?>(?i)abc)def">("abcDEF", false);
  expectMatchAllEngines<"(?>(?i)abc)def">("ABCDEF", false);
}

TEST(RegexCaseInsensitiveTest, LookaheadScopesFlagChange) {
  // Lookarounds are also their own flag scope. (?=(?i)abc) sets CI only
  // inside the lookahead.
  expectSearchAllEngines<"(?=(?i)abc)\\w+">("ABCdef", true);
  expectSearchAllEngines<"(?=(?i)abc)\\w+">("XYZ", false);
}

// =====================================================================
// Backreference tests: case-insensitivity follows the flag at the
// backref site (per Perl/PCRE2/Python). Backrefs are BT-only.
// =====================================================================

TEST(RegexCaseInsensitiveTest, BackrefRespectsCaseInsensitiveFlag) {
  using R = Regex<"(\\w+) \\1", Flags::CaseInsensitive>;
  EXPECT_TRUE(R::match("hello hello"));
  EXPECT_TRUE(R::match("hello HELLO"));
  EXPECT_TRUE(R::match("HELLO hello"));
  EXPECT_TRUE(R::match("HeLLo hElLo"));
}

TEST(RegexCaseInsensitiveTest, BackrefWithoutFlagIsCaseSensitive) {
  using R = Regex<"(\\w+) \\1">;
  EXPECT_TRUE(R::match("hello hello"));
  EXPECT_FALSE(R::match("hello HELLO"));
  EXPECT_FALSE(R::match("HeLLo hElLo"));
}

TEST(RegexCaseInsensitiveTest, BackrefScopedByGroup) {
  // ((?i)\w+) \1 — \1 is OUTSIDE the (?i) scope, so backref is
  // case-sensitive even though the captured text was matched
  // case-insensitively. This matches Perl/PCRE2 behavior.
  using R = Regex<"((?i)\\w+) \\1">;
  EXPECT_TRUE(R::match("hello hello"));
  EXPECT_FALSE(R::match("HELLO hello"));
  EXPECT_FALSE(R::match("Hello hello"));
}

TEST(RegexCaseInsensitiveTest, BackrefInsideTopLevelInlineFlag) {
  // (?i)(\w+) \1 — both capture and backref in (?i) scope.
  using R = Regex<"(?i)(\\w+) \\1">;
  EXPECT_TRUE(R::match("hello HELLO"));
  EXPECT_TRUE(R::match("HELLO hello"));
  EXPECT_TRUE(R::match("Hello hello"));
}

// =====================================================================
// AST structure tests: verify that the parser produces the expected
// tree shape for case-insensitive patterns.
// =====================================================================

TEST(RegexCaseInsensitiveTest, SingleAlphaBecomesCharClass) {
  constexpr auto& ast = Regex<"a", Flags::CaseInsensitive>::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  static_assert(countNodesOfKind(ast, NodeKind::Literal) == 0);
}

TEST(RegexCaseInsensitiveTest, AllAlphaProducesNoLiteralNodes) {
  // "hello" CI: every char becomes a CharClass node (or gets stripped
  // as prefix). Verify no Literal nodes contain alpha content remain.
  constexpr auto& ast = Regex<"hello", Flags::CaseInsensitive>::parsed_;
  // Literal prefix cannot be extracted for CI — comparison would need
  // case folding. So no prefix.
  static_assert(ast.prefix_len == 0);
  static_assert(countNodesOfKind(ast, NodeKind::Literal) == 0);
  // Five CharClass nodes for h, e, l, l, o.
  static_assert(countNodesOfKind(ast, NodeKind::CharClass) == 5);
}

TEST(RegexCaseInsensitiveTest, NonAlphaLiteralStaysLiteral) {
  // "123" CI: no alpha chars, all stays in a literal (and gets
  // extracted as a prefix).
  constexpr auto& ast = Regex<"123", Flags::CaseInsensitive>::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "123"));
  static_assert(rootKind(ast) == NodeKind::Empty);
}

TEST(RegexCaseInsensitiveTest, MixedAlphaAndDigitsSplits) {
  // "a1b" CI: Sequence(CharClass[aA], Literal("1"), CharClass[bB]).
  constexpr auto& ast = Regex<"a1b", Flags::CaseInsensitive>::parsed_;
  // Two CharClass nodes (a, b) + one Literal ("1"). Other nodes may
  // exist depending on whether the optimizer extracted a prefix.
  static_assert(countNodesOfKind(ast, NodeKind::CharClass) >= 2);
}

TEST(RegexCaseInsensitiveTest, CharClassExpandsBothCases) {
  // [a-z] CI should produce a single CharClass that covers both [a-z]
  // and [A-Z]. We verify by matching: both 'a' and 'A' should match.
  constexpr auto& ast = Regex<"[a-z]", Flags::CaseInsensitive>::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  // Behavioral check that the expansion happened.
  EXPECT_TRUE(bool(Regex<"[a-z]", Flags::CaseInsensitive>::match("A")));
  EXPECT_TRUE(bool(Regex<"[a-z]", Flags::CaseInsensitive>::match("z")));
}

TEST(RegexCaseInsensitiveTest, NegatedCharClassExpandsBeforeNegation) {
  // [^a-z] CI: stored ranges before negation are [a-zA-Z], so after
  // negation neither lowercase nor uppercase letters match.
  constexpr auto& ast = Regex<"[^a-z]", Flags::CaseInsensitive>::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  EXPECT_FALSE(bool(Regex<"[^a-z]", Flags::CaseInsensitive>::match("a")));
  EXPECT_FALSE(bool(Regex<"[^a-z]", Flags::CaseInsensitive>::match("Z")));
  EXPECT_TRUE(bool(Regex<"[^a-z]", Flags::CaseInsensitive>::match("3")));
}

TEST(RegexCaseInsensitiveTest, InlineFlagProducesSameAstAsGlobalFlag) {
  // "(?i)abc" should be structurally equivalent to "abc" with
  // Flags::CaseInsensitive. Both should have zero Literal nodes
  // containing alpha content.
  constexpr auto& astInline = Regex<"(?i)abc">::parsed_;
  constexpr auto& astFlag = Regex<"abc", Flags::CaseInsensitive>::parsed_;
  static_assert(
      countNodesOfKind(astInline, NodeKind::Literal) ==
      countNodesOfKind(astFlag, NodeKind::Literal));
  static_assert(
      countNodesOfKind(astInline, NodeKind::CharClass) ==
      countNodesOfKind(astFlag, NodeKind::CharClass));
}

TEST(RegexCaseInsensitiveTest, ScopedFlagPreservesOuterCaseSensitivity) {
  // (?i:abc)def — abc nodes are CI CharClasses, def stays as a literal.
  constexpr auto& ast = Regex<"(?i:abc)def">::parsed_;
  // "def" should still appear as a literal somewhere.
  static_assert(countNodesOfKind(ast, NodeKind::CharClass) >= 3);
}

TEST(RegexCaseInsensitiveTest, BackrefNodeKindReflectsFlag) {
  constexpr auto& astCI = Regex<"(\\w+)\\1", Flags::CaseInsensitive>::parsed_;
  constexpr auto& astCS = Regex<"(\\w+)\\1">::parsed_;
  static_assert(countNodesOfKind(astCI, NodeKind::CaseInsensitiveBackref) == 1);
  static_assert(countNodesOfKind(astCI, NodeKind::Backref) == 0);
  static_assert(countNodesOfKind(astCS, NodeKind::Backref) == 1);
  static_assert(countNodesOfKind(astCS, NodeKind::CaseInsensitiveBackref) == 0);
}

TEST(RegexCaseInsensitiveTest, ScopedBackrefIsCaseSensitive) {
  // ((?i)\w+) \1 — \1 is outside (?i) scope, must produce a regular
  // Backref node.
  constexpr auto& ast = Regex<"((?i)\\w+) \\1">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 0);
}

TEST(RegexCaseInsensitiveTest, TopLevelInlineBackrefIsCaseInsensitive) {
  // (?i)(\w+) \1 — both inside (?i) scope.
  constexpr auto& ast = Regex<"(?i)(\\w+) \\1">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::CaseInsensitiveBackref) == 1);
  static_assert(countNodesOfKind(ast, NodeKind::Backref) == 0);
}

// =====================================================================
// Error tests: non-ASCII bytes with CaseInsensitive flag should error.
// =====================================================================

TEST(RegexCaseInsensitiveTest, NonAsciiByteInLiteralErrors) {
  auto result = parse<2, ParseErrorMode::RuntimeReport>(
      std::string_view("\x80", 1), Flags::CaseInsensitive);
  EXPECT_FALSE(result.valid);
}

TEST(RegexCaseInsensitiveTest, NonAsciiByteInCharClassErrors) {
  auto result = parse<6, ParseErrorMode::RuntimeReport>(
      std::string_view("[\\x80]", 6), Flags::CaseInsensitive);
  EXPECT_FALSE(result.valid);
}

TEST(RegexCaseInsensitiveTest, NonAsciiRangeEndpointInCharClassErrors) {
  auto result = parse<11, ParseErrorMode::RuntimeReport>(
      std::string_view("[\\x80-\\xff]", 11), Flags::CaseInsensitive);
  EXPECT_FALSE(result.valid);
}

TEST(RegexCaseInsensitiveTest, NegatedShorthandClassNotErrored) {
  // \D includes bytes 128-255 after negation, but the user didn't
  // explicitly write them, so this should NOT error.
  auto result = parse<2, ParseErrorMode::RuntimeReport>(
      std::string_view("\\D", 2), Flags::CaseInsensitive);
  EXPECT_TRUE(result.valid);
}

TEST(RegexCaseInsensitiveTest, NegatedCharClassNotErrored) {
  // [^a-z] post-negation includes bytes 128-255, but user input is
  // ASCII-only, so this should NOT error.
  auto result = parse<6, ParseErrorMode::RuntimeReport>(
      std::string_view("[^a-z]", 6), Flags::CaseInsensitive);
  EXPECT_TRUE(result.valid);
}

TEST(RegexCaseInsensitiveTest, PosixClassNotErrored) {
  auto result = parse<10, ParseErrorMode::RuntimeReport>(
      std::string_view("[[:alpha:]]", 11), Flags::CaseInsensitive);
  EXPECT_TRUE(result.valid);
}

TEST(RegexCaseInsensitiveTest, NonAsciiAllowedWithoutFlag) {
  // Without CaseInsensitive, \x80 is accepted as a regular byte.
  auto result = parse<4, ParseErrorMode::RuntimeReport>(
      std::string_view("\\x80", 4), Flags::None);
  EXPECT_TRUE(result.valid);
}

TEST(RegexCaseInsensitiveTest, ErrorOnlyWithinScopedCaseInsensitiveGroup) {
  // (?i:\x80) errors because \x80 is inside the CI scope.
  auto result = parse<10, ParseErrorMode::RuntimeReport>(
      std::string_view("(?i:\\x80)", 9), Flags::None);
  EXPECT_FALSE(result.valid);
}

TEST(RegexCaseInsensitiveTest, ScopedCaseInsensitiveDoesNotAffectOutside) {
  // (?i:abc)\x80 — \x80 is OUTSIDE the (?i:) scope, so no error.
  auto result = parse<12, ParseErrorMode::RuntimeReport>(
      std::string_view("(?i:abc)\\x80", 12), Flags::None);
  EXPECT_TRUE(result.valid);
}
