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

// ===== Automatic Possessive Promotion =====

TEST(RegexCrossEngineOptTest, PossessiveInference) {
  // [a-z]+[0-9]+ → [a-z]+ promoted to possessive (disjoint from [0-9])
  constexpr auto& ast = Regex<"[a-z]+[0-9]+">::parsed_;
  static_assert(hasPossessiveRepeat(ast));
  static_assert(ast.nfa_compatible);

  // Both repeats should be possessive
  static_assert(countNodesOfKind(ast, NodeKind::Repeat) == 2);
  // First child of root Sequence: [a-z]+ (should be possessive)
  static_assert(rootKind(ast) == NodeKind::Sequence);
  constexpr int firstRep = getNthChild(ast, ast.root, 0);
  static_assert(ast.nodes[firstRep].kind == NodeKind::Repeat);
  static_assert(ast.nodes[firstRep].repeat_mode == RepeatMode::Possessive);
  // Second child: [0-9]+ (should be possessive — nothing follows it)
  constexpr int secondRep = getNthChild(ast, ast.root, 1);
  static_assert(ast.nodes[secondRep].kind == NodeKind::Repeat);
  static_assert(ast.nodes[secondRep].repeat_mode == RepeatMode::Possessive);

  expectMatchAllEngines<"[a-z]+[0-9]+">("abc123", true);
  expectMatchAllEngines<"[a-z]+[0-9]+">("abc", false);
  expectMatchAllEngines<"[a-z]+[0-9]+">("123", false);
}

TEST(RegexCrossEngineOptTest, PossessivePromotionDisjoint) {
  // [a-z]+\d — disjoint, should be promoted
  constexpr auto& ast1 = Regex<"[a-z]+\\d">::parsed_;
  static_assert(hasPossessiveRepeat(ast1));
  static_assert(ast1.nfa_compatible);

  // [a-z]+\d — the repeat should be possessive
  static_assert(rootKind(ast1) == NodeKind::Sequence);
  constexpr int firstRep1 = getNthChild(ast1, ast1.root, 0);
  static_assert(ast1.nodes[firstRep1].kind == NodeKind::Repeat);
  static_assert(ast1.nodes[firstRep1].repeat_mode == RepeatMode::Possessive);

  expectMatchAllEngines<"[a-z]+\\d">("abc1", true);
  expectMatchAllEngines<"[a-z]+\\d">("1", false);
  expectMatchAllEngines<"[a-z]+\\d">("abc", false);
  expectSearchAllEngines<"[a-z]+\\d">("xxx abc1 yyy", true, "abc1");

  // a+b — disjoint
  constexpr auto& ast2 = Regex<"a+b">::parsed_;
  static_assert(hasPossessiveRepeat(ast2));

  expectMatchAllEngines<"a+b">("aaab", true);
  expectMatchAllEngines<"a+b">("b", false);

  // [a-z]+[a-z] — NOT disjoint, should NOT be promoted
  constexpr auto& ast3 = Regex<"[a-z]+[a-z]">::parsed_;
  static_assert(!hasPossessiveRepeat(ast3));

  expectMatchAllEngines<"[a-z]+[a-z]">("abc", true);
  expectSearchAllEngines<"[a-z]+[a-z]">("abc", true, "abc");

  // .+x — dot overlaps with everything, NOT promoted
  expectMatchAllEngines<".+x">("abcx", true);

  // Multiple promotable quantifiers in sequence
  expectMatchAllEngines<"[a-z]+\\d+[A-Z]+">("abc123XYZ", true);
}

TEST(RegexCrossEngineOptTest, PossessivePromotionThroughOptionalRepeat) {
  // [a-z]+ followed by \d* followed by [A-Z]
  // Follow set = \d ∪ [A-Z] = [0-9A-Z], disjoint from [a-z]
  constexpr auto& ast = Regex<"[a-z]+\\d*[A-Z]">::parsed_;
  static_assert(hasPossessiveRepeat(ast));
  static_assert(ast.backtrack_safe);

  // Both repeats should be possessive: [a-z]+ and \d*
  static_assert(rootKind(ast) == NodeKind::Sequence);
  constexpr int firstRep = getNthChild(ast, ast.root, 0);
  static_assert(ast.nodes[firstRep].kind == NodeKind::Repeat);
  // [a-z]+ is promoted: follow set (\d ∪ [A-Z]) is disjoint from [a-z].
  static_assert(ast.nodes[firstRep].repeat_mode == RepeatMode::Possessive);
  constexpr int secondRep = getNthChild(ast, ast.root, 1);
  static_assert(ast.nodes[secondRep].kind == NodeKind::Repeat);
  // \d* is promoted: char set [0-9] is disjoint from the following [A-Z].
  static_assert(ast.nodes[secondRep].repeat_mode == RepeatMode::Possessive);

  expectMatchAllEngines<"[a-z]+\\d*[A-Z]">("abc123X", true);
  expectMatchAllEngines<"[a-z]+\\d*[A-Z]">("abcX", true);
  expectMatchAllEngines<"[a-z]+\\d*[A-Z]">("abc", false);
  expectSearchAllEngines<"[a-z]+\\d*[A-Z]">("xxx abc123X yyy", true, "abc123X");

  // [a-z]+ followed by \d? followed by [A-Z]
  constexpr auto& ast2 = Regex<"[a-z]+\\d?[A-Z]">::parsed_;
  static_assert(hasPossessiveRepeat(ast2));
  static_assert(ast2.backtrack_safe);

  expectMatchAllEngines<"[a-z]+\\d?[A-Z]">("abc1X", true);
  expectMatchAllEngines<"[a-z]+\\d?[A-Z]">("abcX", true);

  // [a-z]+ followed by [a-z]* — NOT disjoint, should NOT promote
  expectMatchAllEngines<"[a-z]+[a-z]*x">("abcx", true);

  // Multiple optional repeats in sequence
  expectMatchAllEngines<"[a-z]+\\d*[A-Z]*!">("abc123XYZ!", true);
  expectMatchAllEngines<"[a-z]+\\d*[A-Z]*!">("abc!", true);

  // End of sequence — follow set is empty → always disjoint → promote
  expectSearchAllEngines<"foo[a-z]+\\d*">("fooabc123", true, "fooabc123");

  // Zero-width nodes between repeat and following content
  expectMatchAllEngines<"[a-z]+$">("abc", true);
}

TEST(RegexCrossEngineOptTest, PossessiveBacktrackSafety) {
  // Explicit possessive — backtrack-safe
  static_assert(Regex<"a++b">::parsed_.backtrack_safe);
  static_assert(Regex<"[a-z]++\\d">::parsed_.backtrack_safe);
  static_assert(Regex<"(?:[a-z]++)b">::parsed_.backtrack_safe);

  // Non-possessive with simple inner — already safe
  static_assert(Regex<"a+b">::parsed_.backtrack_safe);

  // Non-possessive with complex inner — NOT safe
  static_assert(!Regex<"(?:a*b*)+c">::parsed_.backtrack_safe);
}

// ===== Minimum Input Width =====

TEST(RegexCrossEngineOptTest, MinWidth) {
  // Pure literal — min_width equals pattern length
  static_assert(Regex<"abc">::parsed_.min_width == 3);

  // Quantifiers
  static_assert(Regex<"a+">::parsed_.min_width == 1);
  static_assert(Regex<"a*">::parsed_.min_width == 0);
  static_assert(Regex<"a?">::parsed_.min_width == 0);
  static_assert(Regex<"a{3,7}">::parsed_.min_width == 3);
  static_assert(Regex<"a{3}">::parsed_.min_width == 3);

  // Sequences
  static_assert(Regex<"abc.+xyz">::parsed_.min_width == 7); // 3 + 1 + 3
  static_assert(Regex<"a.*b">::parsed_.min_width == 2); // 1 + 0 + 1

  // Alternation — takes minimum branch
  static_assert(Regex<"abc|de">::parsed_.min_width == 2);
  static_assert(Regex<"a|bb|ccc">::parsed_.min_width == 1);

  // Groups are transparent
  static_assert(Regex<"(abc)">::parsed_.min_width == 3);
  static_assert(Regex<"(?:a+)">::parsed_.min_width == 1);

  // Zero-width assertions don't add width
  static_assert(Regex<"^abc$">::parsed_.min_width == 3);
  static_assert(Regex<"\\babc\\b">::parsed_.min_width == 3);

  // Complex patterns
  static_assert(Regex<"[a-z]+\\d{3}[A-Z]+">::parsed_.min_width == 5); // 1+3+1
  static_assert(Regex<"(foo|bar)baz">::parsed_.min_width == 6);
}

TEST(RegexCrossEngineOptTest, MinWidthEarlyRejection) {
  // Match — input too short
  expectMatchAllEngines<"abc">("ab", false);
  expectMatchAllEngines<"a{3}">("aa", false);

  // Search — input too short for any match
  expectSearchAllEngines<"abc">("ab", false);
  expectSearchAllEngines<"[a-z]{5}">("abcd", false);

  // Test — input too short
  expectTestAllEngines<"abc">("ab", false);
  expectTestAllEngines<"[a-z]{5}">("abcd", false);

  // Input exactly min_width — should attempt match
  expectMatchAllEngines<"a+">("a", true);
  expectSearchAllEngines<"abc">("abc", true);
  expectTestAllEngines<"abc">("xabcx", true);
}

// ===== Doubly-Linked AST Consistency =====

TEST(RegexCrossEngineOptTest, DoublyLinkedChildPrevConsistency) {
  // Verify prev_sibling links are consistent after all optimization passes
  // for a variety of patterns that exercise different optimizer transforms.

  // Simple patterns
  static_assert(
      verifyChildPrevLinks(Regex<"abc">::parsed_, Regex<"abc">::parsed_.root));
  static_assert(verifyChildPrevLinks(
      Regex<"a|b|c">::parsed_, Regex<"a|b|c">::parsed_.root));

  // Prefix factoring: foobar|foobaz → foo(bar|baz)
  static_assert(verifyChildPrevLinks(
      Regex<"foobar|foobaz|fooqux">::parsed_,
      Regex<"foobar|foobaz|fooqux">::parsed_.root));

  // Alternation to optional: (a|ab) → ab?
  static_assert(verifyChildPrevLinks(
      Regex<"(a|ab)+c">::parsed_, Regex<"(a|ab)+c">::parsed_.root));

  // Nested quantifiers: (?:a*)* → a*
  static_assert(verifyChildPrevLinks(
      Regex<"(?:a*)*b">::parsed_, Regex<"(?:a*)*b">::parsed_.root));

  // \d+abc — disjoint trailing literal
  static_assert(verifyChildPrevLinks(
      Regex<"\\d+abc">::parsed_, Regex<"\\d+abc">::parsed_.root));

  // Complex pattern with captures, quantifiers, char classes
  static_assert(verifyChildPrevLinks(
      Regex<"(\\d+)-(\\w+)">::parsed_, Regex<"(\\d+)-(\\w+)">::parsed_.root));

  // Possessive quantifiers
  static_assert(verifyChildPrevLinks(
      Regex<"a++b">::parsed_, Regex<"a++b">::parsed_.root));

  // Counted repetition
  static_assert(verifyChildPrevLinks(
      Regex<"a{2,4}b{3}">::parsed_, Regex<"a{2,4}b{3}">::parsed_.root));

  // Lazy quantifiers
  static_assert(verifyChildPrevLinks(
      Regex<"a+?b">::parsed_, Regex<"a+?b">::parsed_.root));

  // Anchors
  static_assert(verifyChildPrevLinks(
      Regex<"^hello$">::parsed_, Regex<"^hello$">::parsed_.root));
}
