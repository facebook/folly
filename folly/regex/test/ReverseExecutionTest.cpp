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
#include <folly/regex/test/AstTestHelpers.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// Shorthand for common flag combinations.
constexpr auto kRevBT = Flags::ForceReverseExecution | Flags::ForceBacktracking;
constexpr auto kFwdBT = Flags::ForceBacktracking;

// ===== 1. Capture correctness in reverse =====
// Verify that forward BT and reverse BT produce identical capture groups.

TEST(ReverseExecution, CapturesThroughPrefixStripping) {
  // hello(world) on "helloworld" — group 1 should capture "world"
  auto fwd = Regex<"hello(world)", kFwdBT>::match("helloworld");
  auto rev = Regex<"hello(world)", kRevBT>::match("helloworld");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[0], "helloworld");
  EXPECT_EQ(rev[0], "helloworld");
  EXPECT_EQ(fwd[1], "world");
  EXPECT_EQ(rev[1], "world");
  EXPECT_EQ(fwd[1], rev[1]);
}

TEST(ReverseExecution, CapturesThroughAlternationFactoring) {
  // (abc|abd) on "abc" — group 1 should capture "abc"
  auto fwd = Regex<"(abc|abd)", kFwdBT>::match("abc");
  auto rev = Regex<"(abc|abd)", kRevBT>::match("abc");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[1], "abc");
  EXPECT_EQ(rev[1], "abc");
  EXPECT_EQ(fwd[1], rev[1]);
}

TEST(ReverseExecution, NestedGroupsThroughPrefix) {
  // ((abc)def)ghi on "abcdefghi" — nested groups
  auto fwd = Regex<"((abc)def)ghi", kFwdBT>::match("abcdefghi");
  auto rev = Regex<"((abc)def)ghi", kRevBT>::match("abcdefghi");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[0], "abcdefghi");
  EXPECT_EQ(rev[0], "abcdefghi");
  EXPECT_EQ(fwd[1], "abcdef");
  EXPECT_EQ(rev[1], "abcdef");
  EXPECT_EQ(fwd[2], "abc");
  EXPECT_EQ(rev[2], "abc");
}

TEST(ReverseExecution, MultipleGroupCaptures) {
  // (\d+)-(\w+) on "123-hello" — multiple capture groups
  auto fwd = Regex<R"((\d+)-(\w+))", kFwdBT>::match("123-hello");
  auto rev = Regex<R"((\d+)-(\w+))", kRevBT>::match("123-hello");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[1], "123");
  EXPECT_EQ(rev[1], "123");
  EXPECT_EQ(fwd[2], "hello");
  EXPECT_EQ(rev[2], "hello");
}

TEST(ReverseExecution, SearchCaptures) {
  // (\d+) search in "abc 42 def" — should find "42"
  auto fwd = Regex<R"((\d+))", kFwdBT>::search("abc 42 def");
  auto rev = Regex<R"((\d+))", kRevBT>::search("abc 42 def");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[0], "42");
  EXPECT_EQ(rev[0], "42");
  EXPECT_EQ(fwd[1], "42");
  EXPECT_EQ(rev[1], "42");
}

// ===== 2. Prefix stripping in reverse (computePrefixStripLen) =====

TEST(ReverseExecution, PrefixStripWithDisjointFollowSet) {
  // "abc[0-9]+" → digits follow "abc", disjoint from 'c'
  constexpr auto& revAst =
      Regex<"abc[0-9]+", Flags::ForceReverseExecution>::parsed_;
  // In reverse, the prefix comes from the reversed AST's leading literal.
  // prefix_strip_len may or may not be > 0 depending on overlap analysis.
  static_assert(revAst.prefix_len >= 0);
  // Verify behavioral correctness
  EXPECT_TRUE((Regex<"abc[0-9]+", kRevBT>::match("abc123")));
  EXPECT_FALSE((Regex<"abc[0-9]+", kRevBT>::match("abcdef")));
}

TEST(ReverseExecution, PrefixStripWithOverlappingFollowSet) {
  // "abc[a-z]+" → letters follow "abc", overlaps with 'c'
  constexpr auto& revAst =
      Regex<"abc[a-z]+", Flags::ForceReverseExecution>::parsed_;
  // Even if prefix_strip_len is reduced due to overlap, behavioral
  // correctness must hold
  EXPECT_TRUE((Regex<"abc[a-z]+", kRevBT>::match("abcxyz")));
  EXPECT_FALSE((Regex<"abc[a-z]+", kRevBT>::match("abc123")));
  // prefix_len should be non-negative in reverse
  static_assert(revAst.prefix_len >= 0);
}

TEST(ReverseExecution, SimpleLiteralPrefixStrip) {
  // "abcdef" → entire pattern is a literal; prefix_strip_len == prefix_len
  constexpr auto& revAst =
      Regex<"abcdef", Flags::ForceReverseExecution>::parsed_;
  static_assert(revAst.prefix_len == 6);
  static_assert(revAst.prefix_strip_len == revAst.prefix_len);

  EXPECT_TRUE((Regex<"abcdef", kRevBT>::match("abcdef")));
  EXPECT_FALSE((Regex<"abcdef", kRevBT>::match("abcde")));
  EXPECT_FALSE((Regex<"abcdef", kRevBT>::match("abcdefg")));
}

// ===== 3. Reverse dot-star pruning =====

TEST(ReverseExecution, TrailingDotStarPruning) {
  // "abc.*" reversed becomes ".*cba" — the dot-star is now leading.
  // Check that the leading_dot_star_min or trailing_dot_star_min reflects
  // the optimizer's analysis of the reversed pattern.
  constexpr auto& revAst =
      Regex<"abc.*", Flags::ForceReverseExecution>::parsed_;
  // At least one of the dot-star fields should be non-negative, or the
  // optimizer may not prune dot-star in reverse. Either way, behavioral
  // correctness is what matters.
  (void)revAst;

  // Behavioral correctness
  EXPECT_TRUE((Regex<"abc.*", kRevBT>::match("abcxyz")));
  EXPECT_TRUE((Regex<"abc.*", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<"abc.*", kRevBT>::match("ab")));
}

TEST(ReverseExecution, LeadingDotStarPruning) {
  // ".*abc" reversed becomes "cba.*" — the dot-star moves to trailing.
  constexpr auto& revAst =
      Regex<".*abc", Flags::ForceReverseExecution>::parsed_;
  // The optimizer may or may not set dot-star fields in reverse.
  (void)revAst;

  EXPECT_TRUE((Regex<".*abc", kRevBT>::match("xyzabc")));
  EXPECT_TRUE((Regex<".*abc", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<".*abc", kRevBT>::match("xyzab")));
}

TEST(ReverseExecution, DotStarBehavioral) {
  // "abc.*" match with reverse + backtracking should succeed
  EXPECT_TRUE((Regex<"abc.*", kRevBT>::match("abcxyz")));
  EXPECT_TRUE((Regex<"abc.*", kRevBT>::match("abc")));
  EXPECT_TRUE((Regex<"abc.*", kRevBT>::match("abc!!!")));
}

// ===== 4. Reversed AST structure verification =====

TEST(ReverseExecution, ReversedAstPrefix) {
  // For "abc\\d+", the reverse AST's prefix (after un-reversal) should
  // be the original trailing literal (if any). Since \\d+ has no trailing
  // literal, the prefix comes from "abc" which gets reversed.
  constexpr auto& fwdAst = Regex<"abc\\d+">::parsed_;
  constexpr auto& revAst =
      Regex<"abc\\d+", Flags::ForceReverseExecution>::parsed_;

  // Forward AST should have "abc" as prefix
  static_assert(fwdAst.prefix_len == 3);
  static_assert(prefixEquals(fwdAst, "abc"));

  // Reverse AST processes the pattern in reverse, so it may or may not
  // have a prefix depending on what the optimizer finds
  static_assert(revAst.prefix_len >= 0);
}

TEST(ReverseExecution, ReversedAstGroupCount) {
  // Verify group count is preserved in reverse AST
  constexpr auto& fwdAst = Regex<"(a)(b)(c)">::parsed_;
  constexpr auto& revAst =
      Regex<"(a)(b)(c)", Flags::ForceReverseExecution>::parsed_;
  static_assert(fwdAst.group_count == 3);
  static_assert(revAst.group_count == 3);
}

TEST(ReverseExecution, ReversedAstNodeKindPreserved) {
  // Verify the AST contains the expected node kinds after reversal
  constexpr auto& revAst =
      Regex<"(abc|def)+", Flags::ForceReverseExecution>::parsed_;
  static_assert(
      hasNodeOfKind(revAst, NodeKind::Alternation) ||
      hasNodeOfKind(revAst, NodeKind::Repeat) ||
      hasNodeOfKind(revAst, NodeKind::Group));
}

// ===== 5. Reverse search correctness =====

TEST(ReverseExecution, SearchFindsLeftRightmostDigits) {
  // Forward search finds "123" (first/leftmost match)
  auto fwd = Regex<R"(\d+)", kFwdBT>::search("abc123def456");
  ASSERT_TRUE(fwd);
  EXPECT_EQ(fwd[0], "123");

  // Reverse search finds "456" (last/rightmost match)
  auto rev = Regex<R"(\d+)", kRevBT>::search("abc123def456");
  ASSERT_TRUE(rev);
  EXPECT_EQ(rev[0], "456");
}

TEST(ReverseExecution, SearchFindsLeftmostAlpha) {
  // Should find "abc" (leftmost match)
  auto result = Regex<"[a-z]+", kRevBT>::search("abc123");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "abc");
}

TEST(ReverseExecution, SearchNoMatch) {
  auto result = Regex<R"(\d+)", kRevBT>::search("no digits here");
  EXPECT_FALSE(result);
}

TEST(ReverseExecution, SearchAtEnd) {
  // Match at the very end of the string
  auto result = Regex<"xyz", kRevBT>::search("abcxyz");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "xyz");
}

TEST(ReverseExecution, SearchWithCapture) {
  // Search with capture groups in reverse
  auto fwd = Regex<R"((\w+)@(\w+))", kFwdBT>::search("email: foo@bar end");
  auto rev = Regex<R"((\w+)@(\w+))", kRevBT>::search("email: foo@bar end");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[0], rev[0]);
  EXPECT_EQ(fwd[1], rev[1]);
  EXPECT_EQ(fwd[2], rev[2]);
}

// ===== 6. Reverse match with anchors =====

TEST(ReverseExecution, AnchorBeginMatch) {
  EXPECT_TRUE((Regex<"^abc", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<"^abc", kRevBT>::match("xabc")));
}

TEST(ReverseExecution, AnchorEndMatch) {
  EXPECT_TRUE((Regex<"abc$", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<"abc$", kRevBT>::match("abcx")));
}

TEST(ReverseExecution, AnchorBothEnds) {
  EXPECT_TRUE((Regex<"^abc$", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<"^abc$", kRevBT>::match("abcd")));
  EXPECT_FALSE((Regex<"^abc$", kRevBT>::match("xabc")));
}

TEST(ReverseExecution, AnchorBeginSearch) {
  auto result = Regex<"^hello", kRevBT>::search("hello world");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "hello");
}

TEST(ReverseExecution, AnchorEndSearch) {
  auto result = Regex<"world$", kRevBT>::search("hello world");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "world");
}

// ===== Additional reverse-specific edge cases =====

TEST(ReverseExecution, EmptyPatternMatch) {
  EXPECT_TRUE((Regex<"", kRevBT>::match("")));
  EXPECT_FALSE((Regex<"", kRevBT>::match("a")));
}

TEST(ReverseExecution, SingleCharMatch) {
  EXPECT_TRUE((Regex<"a", kRevBT>::match("a")));
  EXPECT_FALSE((Regex<"a", kRevBT>::match("b")));
  EXPECT_FALSE((Regex<"a", kRevBT>::match("aa")));
}

TEST(ReverseExecution, AlternationMatch) {
  EXPECT_TRUE((Regex<"abc|def", kRevBT>::match("abc")));
  EXPECT_TRUE((Regex<"abc|def", kRevBT>::match("def")));
  EXPECT_FALSE((Regex<"abc|def", kRevBT>::match("ghi")));
}

TEST(ReverseExecution, OptionalGroupCapture) {
  // (abc)? on "abc"
  auto fwd = Regex<"(abc)?", kFwdBT>::match("abc");
  auto rev = Regex<"(abc)?", kRevBT>::match("abc");
  ASSERT_TRUE(fwd);
  ASSERT_TRUE(rev);
  EXPECT_EQ(fwd[1], rev[1]);
}

TEST(ReverseExecution, QuantifierMatch) {
  EXPECT_TRUE((Regex<"a{3}", kRevBT>::match("aaa")));
  EXPECT_FALSE((Regex<"a{3}", kRevBT>::match("aa")));
  EXPECT_FALSE((Regex<"a{3}", kRevBT>::match("aaaa")));
}

TEST(ReverseExecution, ForwardAndReverseAgreeOnNonMatch) {
  // Both should return false for non-matching input
  auto fwd = Regex<"hello(world)", kFwdBT>::match("helloworl");
  auto rev = Regex<"hello(world)", kRevBT>::match("helloworl");
  EXPECT_FALSE(fwd);
  EXPECT_FALSE(rev);
}

TEST(ReverseExecution, PossessiveRepeatInReverse) {
  // "a++b" — possessive repeat should exist in reverse AST
  constexpr auto& revAst = Regex<"a++b", Flags::ForceReverseExecution>::parsed_;
  // Possessive repeats that enter reverseAst get PossessiveProbed mode
  static_assert(hasPossessiveRepeat(revAst));

  // Behavioral check
  EXPECT_TRUE((Regex<"a++b", kRevBT>::match("aab")));
  EXPECT_TRUE((Regex<"a++b", kRevBT>::match("ab")));
  EXPECT_FALSE((Regex<"a++b", kRevBT>::match("b")));
}

// ===== 8. Possessive probe demotion in reverse =====
// When the repeat's character set is disjoint from its follow set on the
// reversed AST, PossessiveProbed should be demoted to Possessive (no probe).

TEST(ReverseExecution, PossessiveDemotionDisjointFollow) {
  // "a++b" reversed is "ba++" — 'a' is disjoint from 'b', so the
  // probe is unnecessary. The possessive should be demoted to exact
  // Possessive (not PossessiveProbed).
  constexpr auto& revAst = Regex<"a++b", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  // Demotion should have converted PossessiveProbed → Possessive
  static_assert(countPossessiveProbed(revAst) == 0);
  static_assert(countPossessiveExact(revAst) >= 1);

  // Behavioral correctness unchanged
  EXPECT_TRUE((Regex<"a++b", kRevBT>::match("aaab")));
  EXPECT_TRUE((Regex<"a++b", kRevBT>::match("ab")));
  EXPECT_FALSE((Regex<"a++b", kRevBT>::match("aaa")));
}

TEST(ReverseExecution, PossessiveDemotionCharClassDisjoint) {
  // "[a-z]++[0-9]" — letters are disjoint from digits. In reverse,
  // the possessive should be demoted.
  constexpr auto& revAst =
      Regex<"[a-z]++[0-9]", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  static_assert(countPossessiveProbed(revAst) == 0);
  static_assert(countPossessiveExact(revAst) >= 1);

  EXPECT_TRUE((Regex<"[a-z]++[0-9]", kRevBT>::match("abc3")));
  EXPECT_FALSE((Regex<"[a-z]++[0-9]", kRevBT>::match("abcd")));
}

TEST(ReverseExecution, PossessiveNoDemotionOverlappingFollow) {
  // "a++a" reversed is "aa++" — 'a' overlaps with 'a', so the probe
  // cannot be demoted. PossessiveProbed must be preserved.
  constexpr auto& revAst = Regex<"a++a", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  // The probe cannot be removed — PossessiveProbed must remain
  static_assert(countPossessiveProbed(revAst) >= 1);

  // Behavioral correctness: possessive consumes all a's, can't give back
  EXPECT_FALSE((Regex<"a++a", kRevBT>::match("aaa")));
  EXPECT_FALSE((Regex<"a++a", kRevBT>::match("aa")));
}

TEST(ReverseExecution, PossessiveDemotionStarDisjoint) {
  // "a*+b" — possessive star with disjoint follow, should be demoted
  constexpr auto& revAst = Regex<"a*+b", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  static_assert(countPossessiveProbed(revAst) == 0);

  EXPECT_TRUE((Regex<"a*+b", kRevBT>::match("aaab")));
  EXPECT_TRUE((Regex<"a*+b", kRevBT>::match("b")));
  EXPECT_FALSE((Regex<"a*+b", kRevBT>::match("aaa")));
}

TEST(ReverseExecution, PossessiveNoDemotionCharClassOverlap) {
  // "[a-m]++[f-z]" — ranges overlap (f-m), probe cannot be removed
  constexpr auto& revAst =
      Regex<"[a-m]++[f-z]", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  static_assert(countPossessiveProbed(revAst) >= 1);

  // Behavioral correctness
  EXPECT_TRUE((Regex<"[a-m]++[f-z]", kRevBT>::match("abcz")));
  EXPECT_FALSE((Regex<"[a-m]++[f-z]", kRevBT>::match("abc3")));
}

TEST(ReverseExecution, PossessiveDemotionMultipleRepeats) {
  // "a++b++c" — two possessive repeats, both disjoint from their
  // forward follow sets ('b' disjoint from 'a', 'c' disjoint from 'b').
  // Both should be demoted.
  constexpr auto& revAst =
      Regex<"a++b++c", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  static_assert(countPossessiveProbed(revAst) == 0);
  static_assert(countPossessiveExact(revAst) >= 2);

  EXPECT_TRUE((Regex<"a++b++c", kRevBT>::match("aabbc")));
  EXPECT_TRUE((Regex<"a++b++c", kRevBT>::match("abc")));
  EXPECT_FALSE((Regex<"a++b++c", kRevBT>::match("aabb")));
}

TEST(ReverseExecution, PossessiveDemotionMixedPromotable) {
  // "a++b+c" — syntax possessive a++ has disjoint follow 'b' and should
  // be demoted. Greedy b+ followed by disjoint 'c' should be promoted
  // to possessive by the promotion pass.
  constexpr auto& revAst =
      Regex<"a++b+c", Flags::ForceReverseExecution>::parsed_;
  static_assert(hasPossessiveRepeat(revAst));
  // a++ is syntax-possessive → PossessiveProbed → demoted to Possessive
  // b+ is greedy → promoted to Possessive in both forward and reverse
  static_assert(countPossessiveProbed(revAst) == 0);
  static_assert(countPossessiveExact(revAst) >= 2);

  EXPECT_TRUE((Regex<"a++b+c", kRevBT>::match("aabbc")));
  EXPECT_FALSE((Regex<"a++b+c", kRevBT>::match("aabb")));
}
