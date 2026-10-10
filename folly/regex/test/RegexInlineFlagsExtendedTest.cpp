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
using detail::NodeKind;
using detail::parse;
using detail::ParseErrorMode;
using detail::RepeatMode;

// =====================================================================
// (?U) ungreedy mode. Verified PCRE2 semantics:
//  - `*`, `+`, `?`, `{n,m}` become lazy
//  - `*?`, `+?`, `??`, `{n,m}?` become greedy (the `?` modifier inverts
//    the new default)
//  - Possessive `*+`, `++`, `?+`, `{n,m}+` is unchanged
// =====================================================================

TEST(RegexInlineFlagsExtendedTest, UngreedyStarMatchesShortest) {
  // (?U)a.*b vs aXXbYYb → matches "aXXb" (lazy by default).
  expectSearchAllEngines<"(?U)a.*b">("aXXbYYb", true, "aXXb");
}

TEST(RegexInlineFlagsExtendedTest, GreedyStarStillGreedyWithoutFlag) {
  expectSearchAllEngines<"a.*b">("aXXbYYb", true, "aXXbYYb");
}

TEST(RegexInlineFlagsExtendedTest, UngreedyLazyModifierBecomesGreedy) {
  // (?U)a.*?b — `*?` flips back to greedy.
  expectSearchAllEngines<"(?U)a.*?b">("aXXbYYb", true, "aXXbYYb");
}

TEST(RegexInlineFlagsExtendedTest, UngreedyPossessiveUnchanged) {
  // (?U)a.*+b — possessive unchanged. Possessive `.` consumes
  // everything, so no match for the trailing `b`.
  expectSearchAllEngines<"(?U)a.*+b">("aXXbYYb", false);
}

TEST(RegexInlineFlagsExtendedTest, ScopedUngreedy) {
  // (?U:a.*b) scoped ungreedy — `.*` is lazy inside the group.
  // Test the scoped form separately from the global form to ensure
  // (?U:...) correctly scopes the lazy flag.
  expectSearchAllEngines<"(?U:a.+b).">("aXXbYYbc", true, "aXXbY");
  expectMatchAllEngines<"(?U:a.+b).">("aXXbYYbc", true);
}

TEST(RegexInlineFlagsExtendedTest, ScopedUngreedyWithSuffixLiteral) {
  expectSearchAllEngines<"(?U:a.*b)c">("aXXbYYbc", true, "aXXbYYbc");
  expectMatchAllEngines<"(?U:a.*b)c">("aXXbYYbc", true);
}

TEST(RegexInlineFlagsExtendedTest, ScopedUngreedyWithSuffixLiteralPlus) {
  expectSearchAllEngines<"(?U:a.+b)c">("aXXbYYbc", true, "aXXbYYbc");
  expectMatchAllEngines<"(?U:a.+b)c">("aXXbYYbc", true);
}

TEST(RegexInlineFlagsExtendedTest, ScopedUngreedyWithSuffixLiteralCounted) {
  expectSearchAllEngines<"(?U:a.{1,3}b)c">("aXXbc", true, "aXXbc");
  expectMatchAllEngines<"(?U:a.{1,3}b)c">("aXXbc", true);
}

TEST(
    RegexInlineFlagsExtendedTest,
    ScopedUngreedyWithSuffixLiteralGreedyFlipped) {
  expectSearchAllEngines<"(?U:a.*?b)c">("aXXbYYbc", true, "aXXbYYbc");
  expectMatchAllEngines<"(?U:a.*?b)c">("aXXbYYbc", true);
}

TEST(RegexInlineFlagsExtendedTest, UngreedyToggleOff) {
  // (?U)a.*b(?-U)c.*d — first half lazy, second half greedy.
  expectSearchAllEngines<"(?U)a.*b(?-U)c.*d">(
      "aXXbcYYdZZd", true, "aXXbcYYdZZd");
}

TEST(RegexInlineFlagsExtendedTest, UngreedyPlusBecomesLazy) {
  // (?U)a.+b — `+` lazy. matches at least 1 char then lazy.
  expectSearchAllEngines<"(?U)a.+b">("aXXbYYb", true, "aXXb");
}

TEST(RegexInlineFlagsExtendedTest, UngreedyCountedRepeatBecomesLazy) {
  // (?U)a.{1,3}b — counted repeat lazy. Should match shortest valid.
  expectSearchAllEngines<"(?U)a.{1,3}b">("aXXXbYYb", true, "aXXXb");
}

// AST verification for (?U). Use the unoptimized parser output so the
// optimizer does not rewrite trivial lazy/greedy quantifiers (which it
// otherwise will, e.g., demoting `a*` at end-of-pattern to possessive).
TEST(RegexInlineFlagsExtendedTest, UngreedyAstHasLazyRepeat) {
  // Under (?U), the `*` quantifier should produce a Lazy Repeat in the
  // raw parse output.
  static_assert([] {
    auto r = parse<ParseErrorMode::RuntimeReport>("(?U)a*b");
    if (!r.valid) {
      return false;
    }
    int idx = findNodeOfKind(r, NodeKind::Repeat);
    return idx >= 0 && r.nodes[idx].repeat_mode == RepeatMode::Lazy;
  }());
}

TEST(RegexInlineFlagsExtendedTest, UngreedyLazyAstHasGreedyRepeat) {
  // Under (?U), `*?` flips to Greedy.
  static_assert([] {
    auto r = parse<ParseErrorMode::RuntimeReport>("(?U)a*?b");
    if (!r.valid) {
      return false;
    }
    int idx = findNodeOfKind(r, NodeKind::Repeat);
    return idx >= 0 && r.nodes[idx].repeat_mode == RepeatMode::Greedy;
  }());
}

TEST(RegexInlineFlagsExtendedTest, UngreedyPossessiveAstUnchanged) {
  // Under (?U), `*+` stays Possessive.
  static_assert([] {
    auto r = parse<ParseErrorMode::RuntimeReport>("(?U)a*+b");
    if (!r.valid) {
      return false;
    }
    int idx = findNodeOfKind(r, NodeKind::Repeat);
    return idx >= 0 && r.nodes[idx].repeat_mode == RepeatMode::Possessive;
  }());
}

// =====================================================================
// (?n) no-auto-capture: plain `(...)` becomes non-capturing. Named
// groups still capture.
// =====================================================================

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureBasic) {
  // (?n)(abc) — group is non-capturing.
  EXPECT_TRUE(bool(Regex<"(?n)(abc)">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureGroupCountIsZero) {
  constexpr auto& ast = Regex<"(?n)(abc)">::parsed_;
  static_assert(ast.group_count == 0);
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureMultipleGroupsAllNonCapturing) {
  constexpr auto& ast = Regex<"(?n)(abc)(def)">::parsed_;
  static_assert(ast.group_count == 0);
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureScopedAffectsOnlyInside) {
  // (?n:(abc))(def) — only the second group captures. group_count == 1.
  constexpr auto& ast = Regex<"(?n:(abc))(def)">::parsed_;
  static_assert(ast.group_count == 1);
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureNamedGroupStillCaptures) {
  // (?n)(?<name>abc) — named group captures.
  constexpr auto& ast = Regex<R"((?n)(?<name>abc))">::parsed_;
  static_assert(ast.group_count == 1);
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureToggleOff) {
  // (?n)(abc)(?-n)(def) — only the second captures.
  constexpr auto& ast = Regex<"(?n)(abc)(?-n)(def)">::parsed_;
  static_assert(ast.group_count == 1);
}

TEST(RegexInlineFlagsExtendedTest, NoAutoCaptureCombinedWithCaseInsensitive) {
  // (?ni)(abc) — non-capturing AND case-insensitive.
  EXPECT_TRUE(bool(Regex<"(?ni)(abc)">::match("ABC")));
  constexpr auto& ast = Regex<"(?ni)(abc)">::parsed_;
  static_assert(ast.group_count == 0);
}

// =====================================================================
// (?#...) comments: opaque body terminated by `)`.
// =====================================================================

TEST(RegexInlineFlagsExtendedTest, CommentBetweenLiterals) {
  EXPECT_TRUE(bool(Regex<"a(?#this is a comment)b">::match("ab")));
}

TEST(RegexInlineFlagsExtendedTest, EmptyComment) {
  EXPECT_TRUE(bool(Regex<"a(?#)b">::match("ab")));
}

TEST(RegexInlineFlagsExtendedTest, CommentInAlternation) {
  EXPECT_TRUE(bool(Regex<"a|(?#z)b">::match("b")));
  EXPECT_TRUE(bool(Regex<"a|(?#z)b">::match("a")));
}

TEST(RegexInlineFlagsExtendedTest, CommentAtStart) {
  EXPECT_TRUE(bool(Regex<"(?#prefix)abc">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, MultipleComments) {
  EXPECT_TRUE(bool(Regex<"(?#one)a(?#two)b(?#three)c">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, UnterminatedCommentErrors) {
  auto result = parse<16, ParseErrorMode::RuntimeReport>(
      std::string_view("a(?#unterminated", 16));
  EXPECT_FALSE(result.valid);
}

// =====================================================================
// (?x) extended mode: whitespace and `# ... \n` comments are ignored.
// =====================================================================

TEST(RegexInlineFlagsExtendedTest, ExtendedSkipsSpaces) {
  EXPECT_TRUE(bool(Regex<"(?x) a b c">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedDoesNotMatchLiteralSpaces) {
  EXPECT_FALSE(bool(Regex<"(?x) a b c">::match("a b c")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedHashStartsLineComment) {
  EXPECT_TRUE(bool(Regex<"(?x) a # comment\n b">::match("ab")));
}

TEST(
    RegexInlineFlagsExtendedTest,
    ExtendedWhitespaceInsideCharClassSignificant) {
  // (?x)[ a b ] should still match literal space, since whitespace
  // inside [...] is preserved per Perl semantics.
  EXPECT_TRUE(bool(Regex<"(?x)[ a b ]">::match(" ")));
  EXPECT_TRUE(bool(Regex<"(?x)[ a b ]">::match("a")));
  EXPECT_TRUE(bool(Regex<"(?x)[ a b ]">::match("b")));
  EXPECT_FALSE(bool(Regex<"(?x)[ a b ]">::match("c")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedEscapedSpaceIsLiteral) {
  // `\ ` is a literal escaped space.
  EXPECT_TRUE(bool(Regex<R"((?x) a \  b)">::match("a b")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedOctalEscapePreserved) {
  // `\012` is octal newline; the digits must not be split by
  // whitespace handling.
  EXPECT_TRUE(bool(Regex<R"((?x) \012)">::match("\n")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedTabsAndNewlinesIgnored) {
  EXPECT_TRUE(bool(Regex<"(?x)\ta\tb\tc">::match("abc")));
  EXPECT_TRUE(bool(Regex<"(?x)\na\nb\nc">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedCarriageReturnFormFeedVtIgnored) {
  EXPECT_TRUE(bool(Regex<"(?x)\ra\fb\vc">::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedPropagatesAcrossAlternation) {
  // `a b|(?x)c d` — (?x) propagates so the second branch matches "cd".
  EXPECT_TRUE(bool(Regex<"a b|(?x)c d">::match("cd")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedDoesNotLeakOutOfGroup) {
  // ((?x)a b)cd — `(?x)` is scoped to the group; cd is outside.
  EXPECT_TRUE(
      bool(Regex<"((?x)a b)cd", Flags::ForceBacktracking>::match("abcd")));
  // Verify (?x) does NOT leak outside the group: outside space is literal.
  EXPECT_FALSE(
      bool(Regex<"((?x)a b).", Flags::ForceBacktracking>::match("a bc")));
  EXPECT_TRUE(
      bool(Regex<"((?x)a b).", Flags::ForceBacktracking>::match("abc")));
}

TEST(RegexInlineFlagsExtendedTest, ScopedExtendedForm) {
  EXPECT_TRUE(bool(Regex<"(?x: a b )c">::match("abc")));
  EXPECT_FALSE(bool(Regex<"(?x: a b )c">::match("a b c")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedCombinedWithCaseInsensitive) {
  EXPECT_TRUE(bool(Regex<"(?xi) a b c">::match("ABC")));
}

TEST(RegexInlineFlagsExtendedTest, ExtendedToggleOff) {
  // (?x)abc(?-x)d e — `d e` requires literal space.
  EXPECT_TRUE(bool(Regex<"(?x)abc(?-x)d e">::match("abcd e")));
  EXPECT_FALSE(bool(Regex<"(?x)abc(?-x)d e">::match("abcde")));
}

TEST(
    RegexInlineFlagsExtendedTest,
    ExtendedSkipsWhitespaceInsideQuantifiedCount) {
  // Whitespace inside {n,m} should be ignored.
  EXPECT_TRUE(bool(Regex<"(?x)a{ 2 , 4 }">::match("aaa")));
}
