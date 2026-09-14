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

#include <folly/regex/test/AstTestHelpers.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::AnchorKind;
using detail::NodeKind;
using detail::RepeatMode;

// ===== Non-Capturing Group Flattening =====

TEST(RegexCrossEngineOptTest, GroupFlatteningSingle) {
  // (?:(?:a)) → flattened to just Literal "a" → prefix stripped
  constexpr auto& ast = Regex<"(?:(?:a))">::parsed_;
  static_assert(ast.prefix_len == 1);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));

  expectMatchAllEngines<"(?:(?:a))">("a", true);
  expectMatchAllEngines<"(?:(?:a))">("b", false);
  expectMatchAllEngines<"(?:(?:a))">("aa", false);
}

TEST(RegexCrossEngineOptTest, GroupFlatteningNested) {
  // (?:(?:(?:abc))) → flattened to Literal "abc" → prefix stripped
  constexpr auto& ast = Regex<"(?:(?:(?:abc)))">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));

  expectMatchAllEngines<"(?:(?:(?:abc)))">("abc", true);
  expectMatchAllEngines<"(?:(?:(?:abc)))">("ab", false);
  expectMatchAllEngines<"(?:(?:(?:abc)))">("abcd", false);
}

TEST(RegexCrossEngineOptTest, GroupFlatteningPreservesCapturing) {
  // (?:(a)) → outer non-capturing group flattened, inner capturing preserved
  constexpr auto& ast = Regex<"(?:(a))">::parsed_;
  static_assert(hasNodeOfKind(ast, NodeKind::Group));
  constexpr int grpIdx = findNodeOfKind(ast, NodeKind::Group);
  static_assert(ast.nodes[grpIdx].capturing);

  auto m = expectMatchCapturesAgree<"(?:(a))">("a");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "a");
}

// ===== Trivial Repeat Simplification =====

TEST(RegexCrossEngineOptTest, RepeatExactlyOne) {
  // a{1} → simplified to just Literal "a", no Repeat node remains
  constexpr auto& ast = Regex<"a{1}">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::Repeat));
  static_assert(ast.prefix_len == 1);

  expectMatchAllEngines<"a{1}">("a", true);
  expectMatchAllEngines<"a{1}">("aa", false);
  expectMatchAllEngines<"a{1}">("", false);
}

TEST(RegexCrossEngineOptTest, RepeatZeroTimes) {
  // a{0,0} → simplified to Empty, no Repeat node remains
  constexpr auto& ast = Regex<"a{0,0}">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Empty);
  static_assert(!hasNodeOfKind(ast, NodeKind::Repeat));

  expectMatchAllEngines<"a{0,0}">("", true);
  expectMatchAllEngines<"a{0,0}">("a", false);
}

TEST(RegexCrossEngineOptTest, RepeatOneInSequence) {
  // ab{1}c → b{1} simplified to b → "abc" is all prefix
  constexpr auto& ast = Regex<"ab{1}c">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::Repeat));
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));

  expectMatchAllEngines<"ab{1}c">("abc", true);
  expectMatchAllEngines<"ab{1}c">("abbc", false);
  expectMatchAllEngines<"ab{1}c">("ac", false);
}

// ===== Nested Quantifier Flattening =====

TEST(RegexCrossEngineOptTest, GeneralizedNestedQuant) {
  // (?:a*)* → a* : nested groups/repeats flattened
  constexpr auto& ast1 = Regex<"(?:a*)*b">::parsed_;
  static_assert(!hasNodeOfKind(ast1, NodeKind::Group));
  static_assert(countNodesOfKind(ast1, NodeKind::Repeat) == 1);
  constexpr int repIdx1 = findNodeOfKind(ast1, NodeKind::Repeat);
  static_assert(ast1.nodes[repIdx1].min_repeat == 0);
  static_assert(ast1.nodes[repIdx1].max_repeat == -1);

  expectMatchAllEngines<"(?:a*)*b">("b", true);
  expectMatchAllEngines<"(?:a*)*b">("ab", true);
  expectMatchAllEngines<"(?:a*)*b">("aaab", true);
  expectMatchAllEngines<"(?:a*)*b">("aaa", false);
  expectMatchAllEngines<"(?:a*)*b">("", false);

  // (?:a+)* → a* : flattened to a*
  constexpr auto& ast2 = Regex<"(?:a+)*b">::parsed_;
  static_assert(!hasNodeOfKind(ast2, NodeKind::Group));
  static_assert(countNodesOfKind(ast2, NodeKind::Repeat) == 1);
  constexpr int repIdx2 = findNodeOfKind(ast2, NodeKind::Repeat);
  static_assert(ast2.nodes[repIdx2].min_repeat == 0);
  static_assert(ast2.nodes[repIdx2].max_repeat == -1);

  expectMatchAllEngines<"(?:a+)*b">("b", true);
  expectMatchAllEngines<"(?:a+)*b">("ab", true);
  expectMatchAllEngines<"(?:a+)*b">("aaab", true);
  expectMatchAllEngines<"(?:a+)*b">("aaa", false);
}

TEST(RegexCrossEngineOptTest, NestedQuantifierWithCaptures) {
  // (a+)+b → capturing Group blocks full flattening, but still matches
  constexpr auto& ast = Regex<"(a+)+b">::parsed_;
  static_assert(hasNodeOfKind(ast, NodeKind::Group));
  constexpr int grpIdx = findNodeOfKind(ast, NodeKind::Group);
  static_assert(ast.nodes[grpIdx].capturing);
  static_assert(ast.backtrack_safe);

  // The outer + is absorbed — result is equivalent to (a+)b
  // There should be exactly one Repeat (inside the capturing group)
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  // Root is a Sequence: the capturing Group followed by the literal "b".
  static_assert(rootKind(ast) == NodeKind::Sequence);

  expectMatchAllEngines<"(a+)+b">("aaab", true);
  expectMatchAllEngines<"(a+)+b">("aaaaaaaaaaaaaaaa", false);

  auto m = expectMatchCapturesAgree<"(a+)+b">("aaab");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "aaab");
  EXPECT_EQ(m[1], "aaa");

  auto s = expectSearchCapturesAgree<"(a+)+b">("xaaab");
  EXPECT_TRUE(s);
  EXPECT_EQ(s[0], "aaab");
  EXPECT_EQ(s[1], "aaa");

  auto m2 = expectMatchCapturesAgree<"(a+)+b">("ab");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "a");
}

// ===== Adjacent Repeat Merging =====

TEST(RegexCrossEngineOptTest, AdjacentRepeatMerge) {
  // a+a+b → merged to a{2,}b : single Repeat with min=2
  constexpr auto& ast = Regex<"a+a+b">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 2);
  static_assert(ast.nodes[repIdx].max_repeat == -1);

  // Root is a Sequence: the merged Repeat followed by the literal "b".
  static_assert(rootKind(ast) == NodeKind::Sequence);

  expectMatchAllEngines<"a+a+b">("ab", false);
  expectMatchAllEngines<"a+a+b">("aab", true);
  expectMatchAllEngines<"a+a+b">("aaab", true);
  expectMatchAllEngines<"a+a+b">("b", false);
  expectMatchAllEngines<"a+a+b">("", false);
}

// ===== Fixed-Length Sequence Repeat =====

TEST(RegexCrossEngineOptTest, FixedLengthSequence) {
  // (?:ab)+ → Group flattened, root is Repeat(Literal("ab"))
  constexpr auto& ast = Regex<"(?:ab)+">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Repeat);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));

  // The Repeat's child should be a Literal (the flattened "ab")
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(repIdx >= 0);
  static_assert(ast.nodes[repIdx].min_repeat == 1);
  static_assert(ast.nodes[repIdx].max_repeat == -1);
  constexpr int innerIdx = ast.nodes[repIdx].child_first;
  static_assert(innerIdx >= 0);
  static_assert(ast.nodes[innerIdx].kind == NodeKind::Literal);

  expectMatchAllEngines<"(?:ab)+">("ab", true);
  expectMatchAllEngines<"(?:ab)+">("abab", true);
  expectMatchAllEngines<"(?:ab)+">("ababab", true);
  expectMatchAllEngines<"(?:ab)+">("a", false);
  expectMatchAllEngines<"(?:ab)+">("aba", false);
  expectMatchAllEngines<"(?:ab)+">("", false);

  expectSearchAllEngines<"(?:ab)+">("xababx", true, "abab");
}

// ===== Single-Char CharClass to Literal =====

TEST(RegexCrossEngineOptTest, SingleCharClassToLiteral) {
  // [a]+b → [a] converted to Literal "a", no CharClass remains
  constexpr auto& ast = Regex<"[a]+b">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::CharClass));
  static_assert(hasNodeOfKind(ast, NodeKind::Repeat));

  expectMatchAllEngines<"[a]+b">("ab", true);
  expectMatchAllEngines<"[a]+b">("aaab", true);
  expectMatchAllEngines<"[a]+b">("b", false);
  expectMatchAllEngines<"[a]+b">("", false);
}

// ===== Nested Counted Repeat Flattening =====

TEST(RegexCrossEngineOptTest, NestedCountedRepeatExactTimesExact) {
  // (?:a{4}){5} → a{20} : single Repeat, no Group
  constexpr auto& ast = Regex<"(?:a{4}){5}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 20);
  static_assert(ast.nodes[repIdx].max_repeat == 20);

  expectMatchAllEngines<"(?:a{4}){5}">(std::string(19, 'a'), false);
  expectMatchAllEngines<"(?:a{4}){5}">(std::string(20, 'a'), true);
  expectMatchAllEngines<"(?:a{4}){5}">(std::string(21, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatRangeTimesExact) {
  // (?:a{2,3}){4} → a{8,12}
  constexpr auto& ast = Regex<"(?:a{2,3}){4}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 8);
  static_assert(ast.nodes[repIdx].max_repeat == 12);

  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(8, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(12, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(13, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatExactTimesRange) {
  // (?:a{3}){2,5} — cannot flatten to a{6,15} because only multiples
  // of 3 in [6,15] are valid (6, 9, 12, 15). Nested structure preserved.
  constexpr auto& ast = Regex<"(?:a{3}){2,5}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);

  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(9, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(10, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(12, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(15, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(16, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatRangeTimesRange) {
  // (?:a{2,3}){4,5} → a{8,15}
  constexpr auto& ast = Regex<"(?:a{2,3}){4,5}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 8);
  static_assert(ast.nodes[repIdx].max_repeat == 15);

  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(8, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(11, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(15, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(16, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatCharClass) {
  // (?:[a-z]{3}){2} → [a-z]{6}
  constexpr auto& ast = Regex<"(?:[a-z]{3}){2}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 6);
  static_assert(ast.nodes[repIdx].max_repeat == 6);

  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcde", false);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcdef", true);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcdefg", false);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcDE1", false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatUnboundedInner) {
  // (?:a{2,}){5} → a{10,}
  constexpr auto& ast = Regex<"(?:a{2,}){5}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);
  constexpr int repIdx = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[repIdx].min_repeat == 10);
  static_assert(ast.nodes[repIdx].max_repeat == -1);

  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(9, 'a'), false);
  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(50, 'a'), true);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatUnboundedOuter) {
  // (?:a{3}){2,} — cannot flatten to a{6,} because only multiples
  // of 3 that are ≥6 are valid. Nested structure preserved.
  constexpr auto& ast = Regex<"(?:a{3}){2,}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);

  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(9, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(48, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(50, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatCapturingBlocks) {
  // Capturing group blocks flattening — nested structure preserved
  constexpr auto& ast = Regex<"(a{2}){3}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);
  static_assert(hasNodeOfKind(ast, NodeKind::Group));
  constexpr int grpIdx = findNodeOfKind(ast, NodeKind::Group);
  static_assert(ast.nodes[grpIdx].capturing);

  std::string capInput(6, 'a');
  auto m = expectMatchCapturesAgree<"(a{2}){3}">(capInput);
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "aaaaaa");
  EXPECT_EQ(m[1], "aa");
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatGreedinessBlocks) {
  // Greedy/lazy mismatch blocks flattening — 2 Repeats preserved
  constexpr auto& ast = Regex<"(?:a{2,4}?){3}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);

  // Structure: outer Repeat{3,3}(inner Repeat{2,4,lazy}(Literal "a"))
  // Greediness mismatch blocks flattening — nested Repeats preserved.
  // The non-capturing group should be flattened away since its child is a
  // Repeat.
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));
  // Outer Repeat should be {3,3} greedy
  constexpr int outerRep = findNodeOfKind(ast, NodeKind::Repeat);
  static_assert(ast.nodes[outerRep].min_repeat == 3);
  static_assert(ast.nodes[outerRep].max_repeat == 3);
  static_assert(ast.nodes[outerRep].repeat_mode == RepeatMode::Greedy);
  // Inner Repeat should be {2,4} lazy
  constexpr int innerRepIdx = ast.nodes[outerRep].child_first;
  static_assert(ast.nodes[innerRepIdx].kind == NodeKind::Repeat);
  static_assert(ast.nodes[innerRepIdx].min_repeat == 2);
  static_assert(ast.nodes[innerRepIdx].max_repeat == 4);
  static_assert(ast.nodes[innerRepIdx].repeat_mode == RepeatMode::Lazy);

  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(7, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(8, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(9, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(11, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(12, 'a'), true);
  expectMatchAllEngines<"(?:a{2,4}?){3}">(std::string(13, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatPossessiveBlocks) {
  // Possessive inner blocks flattening — 2 Repeats preserved
  constexpr auto& ast = Regex<"(?:a{2}+){3}">::parsed_;
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);

  constexpr auto re = compile<"(?:a{2}+){3}">();
  EXPECT_FALSE(re.match(std::string(5, 'a')));
  EXPECT_TRUE(re.match(std::string(6, 'a')));
  EXPECT_FALSE(re.match(std::string(7, 'a')));
}

// ===== Atomic Group Optimizations =====

TEST(RegexCrossEngineOptTest, AtomicToPossessive) {
  // (?>a+) should be converted to a++ by the optimizer
  constexpr auto& ast = Regex<"(?>a+)b">::parsed_;
  static_assert(hasPossessiveRepeat(ast));
  // The {1,1}+ wrapper should be gone — inner repeat promoted to possessive
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 1);

  expectMatchAllEngines<"(?>a+)b">("aaab", true);
  expectMatchAllEngines<"(?>a+)b">("b", false);
}

TEST(RegexCrossEngineOptTest, AtomicNonBacktrackingUnwrap) {
  // (?>abc) — inner is non-backtracking, possessive wrapper unwrapped
  constexpr auto& ast = Regex<"(?>abc)def">::parsed_;
  // No Repeat node should remain (the {1,1}+ was unwrapped)
  static_assert(!hasRepeatWithBounds(ast, 1, 1));

  expectMatchAllEngines<"(?>abc)def">("abcdef", true);
  expectMatchAllEngines<"(?>abc)def">("abcde", false);
}
