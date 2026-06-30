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

// Tests for the alternation discriminator dispatch internals.
//
// The discriminator system works in three passes during AST optimization:
//   1. computeDiscriminatorsBottomUp — finds valid discriminator offsets
//   2. propagateDiscriminatorsTopDown — selects and propagates offsets
//   3. materializeDiscriminatorCharClasses — creates union char classes
//
// Tests verify discriminator_offset and char_class_index on Alternation
// nodes, prefix factoring interactions, and behavioral correctness.

#include <folly/regex/Regex.h>
#include <folly/regex/test/AstTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// ===== 1. Discriminator offset selection =====

// 3-branch alternation with disjoint chars at position 0.
// 'a' vs 'd' vs 'g' are all disjoint → discriminator_offset == 0.
TEST(DiscriminatorTest, ThreeBranchDisjointAtPos0) {
  constexpr auto& ast = Regex<"abc|def|ghi">::parsed_;
  static_assert(ast.nodes[ast.root].kind == detail::NodeKind::Alternation);
  static_assert(ast.nodes[ast.root].discriminator_offset == 0);
}

// 3-branch alternation with overlap at position 0 but disjoint at position 1.
// [ab] ∩ [bc] ≠ ∅ at pos 0; 'x' vs 'y' vs 'z' are disjoint at pos 1
// → discriminator_offset == 1.
// Note: branches must have structurally different first CharClass nodes to
// avoid being merged by generalized prefix factoring.
TEST(DiscriminatorTest, ThreeBranchOverlapPos0DisjointPos1) {
  constexpr auto& ast = Regex<"[ab]x|[bc]y|[cd]z">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0, "Alternation node must exist");
  static_assert(ast.nodes[altIdx].discriminator_offset == 1);
}

// 2-branch alternation: with disjoint first chars, discriminator_offset
// should be auto-selected at offset 0.
TEST(DiscriminatorTest, TwoBranchWithDiscriminator) {
  constexpr auto& ast = Regex<"ab|cd">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  if constexpr (altIdx >= 0) {
    static_assert(ast.nodes[altIdx].discriminator_offset == 0);
  }
}

// ===== 2. No discriminator when chars overlap at all positions =====

// "abc|abd|abe" — after prefix factoring, becomes ab[cde] with no
// alternation remaining. Verify no Alternation node exists.
TEST(DiscriminatorTest, PrefixFactoredNoAlternation) {
  constexpr auto& ast = Regex<"abc|abd|abe">::parsed_;
  static_assert(
      !hasNodeOfKind(ast, NodeKind::Alternation),
      "Prefix factoring should eliminate the alternation");
}

// ===== 3. Discriminator with CharClass branches =====

// Overlap at pos 0 (overlapping range char classes), disjoint at pos 1
// ('x' vs 'y' vs 'z') → discriminator_offset == 1.
// All three ranges must differ to avoid generalized prefix factoring.
TEST(DiscriminatorTest, CharClassBranchesDiscriminatorAtPos1) {
  constexpr auto& ast = Regex<"[a-f]x|[d-k]y|[h-z]z">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0, "Alternation node must exist");
  static_assert(ast.nodes[altIdx].discriminator_offset == 1);
}

// ===== 4. Discriminator char_class_index set =====

// For patterns with discriminator_offset >= 0, verify char_class_index >= 0.
// The char class is the union of all branch characters at the discriminator
// offset.
TEST(DiscriminatorTest, CharClassIndexSetWhenDiscriminatorChosen) {
  constexpr auto& ast = Regex<"abc|def|ghi">::parsed_;
  static_assert(ast.nodes[ast.root].discriminator_offset >= 0);
  static_assert(
      ast.nodes[ast.root].char_class_index >= 0,
      "char_class_index must be set when discriminator is active");
}

TEST(DiscriminatorTest, CharClassIndexSetForCharClassBranches) {
  constexpr auto& ast = Regex<"[a-f]x|[d-k]y|[h-z]z">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0);
  if constexpr (ast.nodes[altIdx].discriminator_offset >= 0) {
    static_assert(ast.nodes[altIdx].char_class_index >= 0);
  }
}

// ===== 5. Alternation inside a capturing group =====

// A capturing group should not prevent discriminator computation.
// "(abc|def|ghi)" → the alternation inside the group gets discriminator
// at position 0.
TEST(DiscriminatorTest, AlternationInCapturingGroup) {
  constexpr auto& ast = Regex<"(abc|def|ghi)">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0, "Alternation node must exist");
  static_assert(ast.nodes[altIdx].discriminator_offset == 0);
}

// ===== 6. Discriminator through sequence =====

// Pattern with a prefix before the alternation.
// After prefix stripping, the alternation's discriminator offset should be 0
// (relative to the alternation's position).
TEST(DiscriminatorTest, DiscriminatorThroughSequence) {
  constexpr auto& ast = Regex<"prefix(abc|def|ghi)">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0, "Alternation node must exist");
  static_assert(ast.nodes[altIdx].discriminator_offset == 0);
}

// ===== 7. Behavioral correctness =====

// Verify that discriminator dispatch produces correct match results.
TEST(DiscriminatorTest, ThreeBranchDispatchCorrectness) {
  EXPECT_TRUE(bool(Regex<"abc|def|ghi">::match("abc")));
  EXPECT_TRUE(bool(Regex<"abc|def|ghi">::match("def")));
  EXPECT_TRUE(bool(Regex<"abc|def|ghi">::match("ghi")));
  EXPECT_FALSE(bool(Regex<"abc|def|ghi">::match("xyz")));
}

TEST(DiscriminatorTest, ThreeBranchPartialOverlapCorrectness) {
  EXPECT_TRUE(bool(Regex<"ax|ay|bz">::match("ax")));
  EXPECT_TRUE(bool(Regex<"ax|ay|bz">::match("ay")));
  EXPECT_TRUE(bool(Regex<"ax|ay|bz">::match("bz")));
  EXPECT_FALSE(bool(Regex<"ax|ay|bz">::match("az")));
  EXPECT_FALSE(bool(Regex<"ax|ay|bz">::match("bx")));
}

TEST(DiscriminatorTest, CharClassBranchCorrectness) {
  EXPECT_TRUE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("ax")));
  EXPECT_TRUE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("my")));
  EXPECT_TRUE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("nz")));
  EXPECT_TRUE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("zz")));
  EXPECT_FALSE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("az")));
  EXPECT_FALSE(bool(Regex<"[a-m]x|[a-m]y|[n-z]z">::match("nx")));
}

TEST(DiscriminatorTest, PrefixSequenceCorrectness) {
  EXPECT_TRUE(bool(Regex<"prefix(abc|def|ghi)">::match("prefixabc")));
  EXPECT_TRUE(bool(Regex<"prefix(abc|def|ghi)">::match("prefixdef")));
  EXPECT_TRUE(bool(Regex<"prefix(abc|def|ghi)">::match("prefixghi")));
  EXPECT_FALSE(bool(Regex<"prefix(abc|def|ghi)">::match("prefixzzz")));
  EXPECT_FALSE(bool(Regex<"prefix(abc|def|ghi)">::match("abc")));
}

// ===== 8. Large branch counts =====

// 7-branch alternation: verify discriminator is chosen.
TEST(DiscriminatorTest, SevenBranchAlternation) {
  constexpr auto& ast7 = Regex<"mon|tue|wed|thu|fri|sat|sun">::parsed_;
  // Root may be Alternation (if not fully factored).
  // If Alternation exists, check discriminator_offset >= 0.
  constexpr int altIdx = findNodeOfKind(ast7, NodeKind::Alternation);
  if constexpr (altIdx >= 0) {
    static_assert(ast7.nodes[altIdx].discriminator_offset >= 0);
  }
}

TEST(DiscriminatorTest, SevenBranchDispatchCorrectness) {
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("mon")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("tue")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("wed")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("thu")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("fri")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("sat")));
  EXPECT_TRUE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("sun")));
  EXPECT_FALSE(bool(Regex<"mon|tue|wed|thu|fri|sat|sun">::match("jan")));
}

// ===== 9. Discriminator with different branch widths =====

// "abc|defg|hijkl" — discriminator chosen within minimum prefix length.
TEST(DiscriminatorTest, DifferentBranchWidths) {
  constexpr auto& ast = Regex<"abc|defg|hijkl">::parsed_;
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0, "Alternation node must exist");
  // The minimum branch prefix length is 3 (from "abc"), so the discriminator
  // must be chosen at an offset < 3. With disjoint first chars, offset 0 is
  // expected.
  static_assert(ast.nodes[altIdx].discriminator_offset >= 0);
  static_assert(ast.nodes[altIdx].discriminator_offset < 3);
}

TEST(DiscriminatorTest, DifferentBranchWidthsCorrectness) {
  EXPECT_TRUE(bool(Regex<"abc|defg|hijkl">::match("abc")));
  EXPECT_TRUE(bool(Regex<"abc|defg|hijkl">::match("defg")));
  EXPECT_TRUE(bool(Regex<"abc|defg|hijkl">::match("hijkl")));
  EXPECT_FALSE(bool(Regex<"abc|defg|hijkl">::match("abcd")));
  EXPECT_FALSE(bool(Regex<"abc|defg|hijkl">::match("def")));
  EXPECT_FALSE(bool(Regex<"abc|defg|hijkl">::match("xyz")));
}
