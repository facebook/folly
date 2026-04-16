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

// ===== Literal Prefix Stripping =====

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingMatch) {
  // "hello" → entire pattern is a literal prefix, root becomes Empty
  constexpr auto& ast = Regex<"hello">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "hello"));
  static_assert(rootKind(ast) == NodeKind::Empty);

  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("hell", false);
  expectMatchAllEngines<"hello">("helloo", false);
  expectMatchAllEngines<"hello">("", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingSearch) {
  constexpr auto& ast = Regex<"world">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "world"));

  expectSearchAllEngines<"world">("hello world", true, "world");
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchNotFound) {
  constexpr auto& ast = Regex<"xyz">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "xyz"));

  expectSearchAllEngines<"xyz">("hello world", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchAtStart) {
  expectSearchAllEngines<"hello">("hello world", true, "hello");
}

TEST(RegexCrossEngineOptTest, PrefixWithRepeat) {
  // Root is Repeat — no literal prefix can be extracted
  constexpr auto& ast = Regex<"a+">::parsed_;
  static_assert(ast.prefix_len == 0);
  static_assert(rootKind(ast) == NodeKind::Repeat);

  expectMatchAllEngines<"a+">("a", true);
  expectMatchAllEngines<"a+">("aaa", true);
  expectMatchAllEngines<"a+">("", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixWithSuffix) {
  // "abc.*xyz" → prefix="abc", suffix="xyz"
  constexpr auto& ast = Regex<"abc.*xyz">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));
  static_assert(ast.suffix_len == 3);
  static_assert(suffixEquals(ast, "xyz"));

  expectMatchAllEngines<"abc.*xyz">("abcxyz", true);
  expectMatchAllEngines<"abc.*xyz">("abc123xyz", true);
  expectMatchAllEngines<"abc.*xyz">("abcxy", false);
  expectMatchAllEngines<"abc.*xyz">("bc123xyz", false);
}

TEST(RegexCrossEngineOptTest, SuffixFastRejectMatch) {
  // "abc" → entire pattern is prefix, root is Empty
  constexpr auto& ast = Regex<"abc">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));
  static_assert(rootKind(ast) == NodeKind::Empty);

  expectMatchAllEngines<"abc">("abc", true);
  expectMatchAllEngines<"abc">("abd", false);
  expectMatchAllEngines<"abc">("xbc", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithCapturingGroup) {
  // "(abc)def" → prefix "abcdef" extracted through capturing Group
  constexpr auto& ast = Regex<"(abc)def">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "abcdef"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectMatchCapturesAgree<"(abc)def">("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdef");
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexCrossEngineOptTest, PrefixStrippedWithCaptures) {
  // "hello(world)" → prefix "helloworld" now extracted through capturing Group
  constexpr auto& ast = Regex<"hello(world)">::parsed_;
  static_assert(ast.prefix_len == 10);
  static_assert(prefixEquals(ast, "helloworld"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectMatchCapturesAgree<"hello(world)">("helloworld");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "world");
}

TEST(RegexCrossEngineOptTest, SearchWithPrefixAndCaptures) {
  // "foo(bar)" → prefix "foobar" now extracted through capturing Group
  constexpr auto& ast = Regex<"foo(bar)">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "foobar"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectSearchCapturesAgree<"foo(bar)">("xxxfoobaryyyy");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "foobar");
  EXPECT_EQ(m[1], "bar");
}

TEST(RegexCrossEngineOptTest, TestWithPrefix) {
  expectTestAllEngines<"hello">("say hello there", true);
  expectTestAllEngines<"hello">("no match here", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithAnchor) {
  // "world$" → "world" stripped as prefix, root is Anchor($)
  constexpr auto& ast = Regex<"world$">::parsed_;
  static_assert(ast.prefix_len == 5);
  static_assert(prefixEquals(ast, "world"));
  static_assert(rootKind(ast) == NodeKind::Anchor);

  expectSearchAllEngines<"world$">("hello world", true, "world");
  expectSearchAllEngines<"world$">("world hello", false);
}

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

// ===== Duplicate Branch Elimination =====

TEST(RegexCrossEngineOptTest, DuplicateBranchElimination) {
  // foo|foo|bar → deduplicated to foo|bar (2 branches, not 3)
  constexpr auto& ast = Regex<"foo|foo|bar">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Alternation);
  static_assert(getChildCount(ast, ast.root) == 2);

  expectMatchAllEngines<"foo|foo|bar">("foo", true);
  expectMatchAllEngines<"foo|foo|bar">("bar", true);
  expectMatchAllEngines<"foo|foo|bar">("baz", false);
}

TEST(RegexCrossEngineOptTest, DuplicateBranchAllSame) {
  // abc|abc|abc → deduplicated to just "abc" (no Alternation remains)
  constexpr auto& ast = Regex<"abc|abc|abc">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
  static_assert(ast.prefix_len == 3);

  expectMatchAllEngines<"abc|abc|abc">("abc", true);
  expectMatchAllEngines<"abc|abc|abc">("ab", false);
  expectMatchAllEngines<"abc|abc|abc">("abcd", false);
}

// ===== Single-Char Alternation → CharClass =====

TEST(RegexCrossEngineOptTest, SingleCharAlternationMerge) {
  // a|b|c|d → merged into single CharClass [abcd]
  constexpr auto& ast = Regex<"a|b|c|d">::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));

  expectMatchAllEngines<"a|b|c|d">("a", true);
  expectMatchAllEngines<"a|b|c|d">("b", true);
  expectMatchAllEngines<"a|b|c|d">("c", true);
  expectMatchAllEngines<"a|b|c|d">("d", true);
  expectMatchAllEngines<"a|b|c|d">("e", false);
  expectMatchAllEngines<"a|b|c|d">("ab", false);
}

// ===== CharClass Merging in Alternation =====

TEST(RegexCrossEngineOptTest, CharClassMergeInAlternation) {
  // [a-m]|[n-z] → merged into single CharClass [a-z]
  constexpr auto& ast = Regex<"[a-m]|[n-z]">::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));

  expectMatchAllEngines<"[a-m]|[n-z]">("a", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("m", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("n", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("z", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("A", false);
  expectMatchAllEngines<"[a-m]|[n-z]">("0", false);
}

TEST(RegexCrossEngineOptTest, MixedCharMerge) {
  // a|[b-d]|e → merged into single CharClass [a-e]
  constexpr auto& ast = Regex<"a|[b-d]|e">::parsed_;
  static_assert(rootKind(ast) == NodeKind::CharClass);
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));

  expectMatchAllEngines<"a|[b-d]|e">("a", true);
  expectMatchAllEngines<"a|[b-d]|e">("b", true);
  expectMatchAllEngines<"a|[b-d]|e">("c", true);
  expectMatchAllEngines<"a|[b-d]|e">("d", true);
  expectMatchAllEngines<"a|[b-d]|e">("e", true);
  expectMatchAllEngines<"a|[b-d]|e">("f", false);
}

// ===== Anchor Hoisting =====

TEST(RegexCrossEngineOptTest, AnchorHoistingBegin) {
  // ^foo|^bar → ^(?:foo|bar): root becomes Sequence starting with Anchor(Begin)
  constexpr auto& ast = Regex<"^foo|^bar">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Sequence);
  // First child of the Sequence should be the hoisted Begin anchor
  constexpr int firstChild = ast.nodes[ast.root].child_first;
  static_assert(ast.nodes[firstChild].kind == NodeKind::Anchor);
  static_assert(ast.nodes[firstChild].anchor == AnchorKind::Begin);
  // The Alternation inside should NOT contain any Anchors
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0);
  static_assert(!hasNodeOfKind(ast, altIdx, NodeKind::Anchor));

  expectSearchAllEngines<"^foo|^bar">("foo123", true);
  expectSearchAllEngines<"^foo|^bar">("bar456", true);
  expectSearchAllEngines<"^foo|^bar">("xfoo", false);
  expectSearchAllEngines<"^foo|^bar">("xbar", false);
}

TEST(RegexCrossEngineOptTest, AnchorHoistingEnd) {
  // foo$|bar$ → (?:foo|bar)$: root becomes Sequence ending with Anchor(End)
  constexpr auto& ast = Regex<"foo$|bar$">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Sequence);
  // Last child of the Sequence should be the hoisted End anchor
  constexpr int lastChild = getLastChild(ast, ast.root);
  static_assert(lastChild >= 0);
  static_assert(ast.nodes[lastChild].kind == NodeKind::Anchor);
  static_assert(ast.nodes[lastChild].anchor == AnchorKind::End);
  // The Alternation inside should NOT contain any Anchors
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(altIdx >= 0);
  static_assert(!hasNodeOfKind(ast, altIdx, NodeKind::Anchor));

  expectSearchAllEngines<"foo$|bar$">("123foo", true);
  expectSearchAllEngines<"foo$|bar$">("456bar", true);
  expectSearchAllEngines<"foo$|bar$">("foox", false);
  expectSearchAllEngines<"foo$|bar$">("barx", false);
}

TEST(RegexCrossEngineOptTest, AnchorHoistingNoMix) {
  // ^foo|bar$ → different anchors, no hoisting possible
  constexpr auto& ast = Regex<"^foo|bar$">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Alternation);

  expectSearchAllEngines<"^foo|bar$">("foobar", true);
  expectSearchAllEngines<"^foo|bar$">("xbar", true);
  expectSearchAllEngines<"^foo|bar$">("foox", true);
  expectSearchAllEngines<"^foo|bar$">("xbaz", false);
}

// ===== Alternation Common Prefix Factoring =====

TEST(RegexCrossEngineOptTest, AlternationCommonPrefixMatch) {
  // dev|devvm|devrs → common prefix "dev" factored out
  constexpr auto& ast = Regex<"dev|devvm|devrs">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "dev"));

  // After factoring "dev" prefix: remaining is
  // Repeat{0,1}(Alternation("vm","rs"))
  static_assert(hasRepeatWithBounds(ast, 0, 1));
  static_assert(hasNodeOfKind(ast, NodeKind::Alternation));
  constexpr int altIdx = findNodeOfKind(ast, NodeKind::Alternation);
  static_assert(getChildCount(ast, altIdx) == 2);

  expectMatchAllEngines<"dev|devvm|devrs">("dev", true);
  expectMatchAllEngines<"dev|devvm|devrs">("devvm", true);
  expectMatchAllEngines<"dev|devvm|devrs">("devrs", true);
  expectMatchAllEngines<"dev|devvm|devrs">("de", false);
  expectMatchAllEngines<"dev|devvm|devrs">("devx", false);
}

TEST(RegexCrossEngineOptTest, AlternationPartialCommonPrefix) {
  // dev|devvm|devrs|shellserver → partial prefix factoring
  constexpr auto& ast = Regex<"dev|devvm|devrs|shellserver">::parsed_;
  // Root is Alternation (the dev-group and shellserver don't share prefix)
  static_assert(rootKind(ast) == NodeKind::Alternation);
  static_assert(getChildCount(ast, ast.root) == 2);

  // First branch should be a Sequence (the dev-group with factored prefix)
  constexpr int firstBranch = getNthChild(ast, ast.root, 0);
  static_assert(firstBranch >= 0);
  static_assert(ast.nodes[firstBranch].kind == NodeKind::Sequence);
  // Second branch should be the standalone "shellserver" literal
  constexpr int secondBranch = getNthChild(ast, ast.root, 1);
  static_assert(secondBranch >= 0);
  static_assert(ast.nodes[secondBranch].kind == NodeKind::Literal);

  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("dev", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devvm", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devrs", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("shellserver", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("shell", false);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devbig", false);
}

TEST(RegexCrossEngineOptTest, AlternationNoCommonPrefix) {
  // abc|def|ghi → no common prefix, Alternation preserved
  constexpr auto& ast = Regex<"abc|def|ghi">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Alternation);
  static_assert(getChildCount(ast, ast.root) == 3);
  static_assert(ast.prefix_len == 0);

  expectMatchAllEngines<"abc|def|ghi">("abc", true);
  expectMatchAllEngines<"abc|def|ghi">("def", true);
  expectMatchAllEngines<"abc|def|ghi">("ghi", true);
  expectMatchAllEngines<"abc|def|ghi">("abcdef", false);
}

TEST(RegexCrossEngineOptTest, AlternationSearchWithFactoring) {
  auto m =
      expectSearchCapturesAgree<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
          "host=devvm.example.com");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "devvm");
}

TEST(RegexCrossEngineOptTest, AlternationSearchMultipleBranches) {
  expectSearchAllEngines<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
      "shellserver.example.com", true);
  expectSearchAllEngines<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
      "webserver.example.com", false);
}

TEST(RegexCrossEngineOptTest, AlternationWithCaptures) {
  // (abc|abd) → common prefix "ab" factored, prefix "ab" now extracted
  constexpr auto& ast = Regex<"(abc|abd)">::parsed_;
  static_assert(ast.prefix_len == 2);
  static_assert(prefixEquals(ast, "ab"));
  static_assert(ast.prefix_group_adjustment_count == 1);
  static_assert(hasNodeOfKind(ast, NodeKind::Group));
  constexpr int grpIdx = findNodeOfKind(ast, NodeKind::Group);
  static_assert(ast.nodes[grpIdx].capturing);

  auto m1 = expectMatchCapturesAgree<"(abc|abd)">("abc");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[1], "abc");
  auto m2 = expectMatchCapturesAgree<"(abc|abd)">("abd");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "abd");
}

TEST(RegexCrossEngineOptTest, AlternationNestedFactoring) {
  // foobar|foobaz|fooqux → nested prefix factoring:
  // 1. "foo" factored and stripped as prefix
  // 2. Remaining: bar|baz|qux
  // 3. "bar" and "baz" share prefix "ba" → factored to ba[rz]
  // 4. Result: Alternation(Sequence("ba", CharClass[rz]), Literal("qux"))
  constexpr auto& ast = Regex<"foobar|foobaz|fooqux">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "foo"));

  // Root should be an Alternation with 2 branches
  static_assert(rootKind(ast) == NodeKind::Alternation);
  static_assert(getChildCount(ast, ast.root) == 2);

  // First branch: Sequence("ba", CharClass[rz])
  constexpr int branch0 = getNthChild(ast, ast.root, 0);
  static_assert(ast.nodes[branch0].kind == NodeKind::Sequence);
  // First child of that Sequence is Literal "ba"
  constexpr int baLit = getNthChild(ast, branch0, 0);
  static_assert(ast.nodes[baLit].kind == NodeKind::Literal);
  // Second child is the merged CharClass [rz]
  constexpr int rzCC = getNthChild(ast, branch0, 1);
  static_assert(ast.nodes[rzCC].kind == NodeKind::CharClass);

  // Second branch: Literal "qux"
  constexpr int branch1 = getNthChild(ast, ast.root, 1);
  static_assert(ast.nodes[branch1].kind == NodeKind::Literal);

  expectMatchAllEngines<"foobar|foobaz|fooqux">("foobar", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("foobaz", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("fooqux", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("foo", false);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("fooby", false);
}

// ===== Alternation to Optional =====

TEST(RegexCrossEngineOptTest, AlternationToOptional) {
  // (a|ab)+c → prefix factoring converts a|ab to ab?, yielding (ab?)+c
  constexpr auto& ast = Regex<"(a|ab)+c">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
  static_assert(hasRepeatWithBounds(ast, 0, 1)); // the optional b?

  constexpr auto& nfa1 = Regex<"(a|ab)+c">::nfaProg_;
  constexpr auto& nfa2 = Regex<"(ab?)+c">::nfaProg_;
  static_assert(ast.suffix_len == Regex<"(ab?)+c">::parsed_.suffix_len);
  static_assert(
      ast.suffix_strip_len == Regex<"(ab?)+c">::parsed_.suffix_strip_len);
  // After possessive promotion, the factored lazy b?? should be promoted to
  // greedy b? since [b] is disjoint from the follow set [ac]. Verify the
  // NFA Split ordering matches the direct (ab?)+c version.
  static_assert(
      nfa1.states[2].next == nfa2.states[2].next &&
          nfa1.states[2].alt == nfa2.states[2].alt,
      "factored (a|ab)+c optional Split should match direct (ab?)+c after "
      "lazy-to-greedy promotion");

  expectMatchAllEngines<"(a|ab)+c">("ac", true);
  expectMatchAllEngines<"(a|ab)+c">("abc", true);
  expectMatchAllEngines<"(a|ab)+c">("aac", true);
  expectMatchAllEngines<"(a|ab)+c">("ababc", true);
  expectMatchAllEngines<"(a|ab)+c">("abac", true);
  expectMatchAllEngines<"(a|ab)+c">("aababc", true);
  expectMatchAllEngines<"(a|ab)+c">("c", false);
  expectMatchAllEngines<"(a|ab)+c">("ab", false);
  expectMatchAllEngines<"(a|ab)+c">("", false);
  expectMatchAllEngines<"(a|ab)+c">("bc", false);

  expectSearchAllEngines<"(a|ab)+c">("xababcx", true, "ababc");

  expectMatchAllEngines<"(ab?)+c">("ac", true);
  expectMatchAllEngines<"(ab?)+c">("abc", true);
  expectMatchAllEngines<"(ab?)+c">("ababc", true);
  expectMatchAllEngines<"(ab?)+c">("c", false);
  expectMatchAllEngines<"(ab?)+c">("ab", false);
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

  // Suffix "b" detected but NOT stripped — preceding a* is possibly
  // zero-width, so stripping is unsafe for search. The suffix is still
  // recognized for fast-reject in anchored match().
  static_assert(ast1.suffix_len == 1);
  static_assert(ast1.suffix_strip_len == 0);

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

  // Suffix "b" detected but NOT stripped — preceding a* is possibly
  // zero-width, so stripping is unsafe for search. The suffix is still
  // recognized for fast-reject in anchored match().
  static_assert(ast2.suffix_len == 1);
  static_assert(ast2.suffix_strip_len == 0);

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
  // Suffix "b" stripped — root becomes the Group
  static_assert(rootKind(ast) == NodeKind::Group);
  static_assert(ast.suffix_strip_len == 1);

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

  // Suffix "b" stripped — root becomes the Repeat
  static_assert(rootKind(ast) == NodeKind::Repeat);
  static_assert(ast.root == repIdx);
  static_assert(ast.suffix_strip_len == 1);

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

// ===== Branch Subsumption =====

TEST(RegexCrossEngineOptTest, BranchSubsumption) {
  // (\w+|\d+)+z → \d+ subsumed by \w+ → equivalent to (\w+)+z
  constexpr auto& ast = Regex<"(?:\\w+|\\d+)+z">::parsed_;
  static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));

  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123", false);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123", false);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionBasic) {
  // \w+ subsumes \d+ → no Alternation remains
  constexpr auto& ast1 = Regex<"(?:\\w+|\\d+)">::parsed_;
  static_assert(!hasNodeOfKind(ast1, NodeKind::Alternation));

  // After subsumption, becomes just \w+ — a Repeat over a CharClass
  static_assert(!hasNodeOfKind(ast1, NodeKind::Group));
  static_assert(rootKind(ast1) == NodeKind::Repeat);
  constexpr int repIdx1 = findNodeOfKind(ast1, NodeKind::Repeat);
  constexpr int innerIdx1 = ast1.nodes[repIdx1].child_first;
  static_assert(ast1.nodes[innerIdx1].kind == NodeKind::CharClass);

  expectMatchAllEngines<"(?:\\w+|\\d+)">("abc", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)">("123", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)">("!", false);

  // [a-z] subsumes [a-f]
  expectMatchAllEngines<"(?:[a-z]|[a-f])">("c", true);
  expectMatchAllEngines<"(?:[a-z]|[a-f])">("z", true);

  // AnyChar subsumes \w
  expectMatchAllEngines<"(?:.|\\w)">("a", true);
  expectMatchAllEngines<"(?:.|\\w)">("!", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionQuantified) {
  // \w+ subsumes \d+ (same quantifier, char subset)
  expectMatchAllEngines<"(?:\\w+|\\d+)z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)z">("123z", true);

  // \w+ subsumes \d (single char subsumed by + quantifier)
  expectMatchAllEngines<"(?:\\w+|\\d)z">("abcz", true);
  expectMatchAllEngines<"(?:\\w+|\\d)z">("1z", true);

  // \w* subsumes \d+ ([1,inf) subsumed by [0,inf))
  expectMatchAllEngines<"(?:\\w*|\\d+)z">("z", true);
  expectMatchAllEngines<"(?:\\w*|\\d+)z">("123z", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionNoElimination) {
  // \d doesn't subsume \w — Alternation preserved
  expectMatchAllEngines<"(?:\\d|\\w)">("a", true);
  expectMatchAllEngines<"(?:\\d|\\w)">("1", true);

  // Different repeat bounds, not subsumed
  expectMatchAllEngines<"(?:\\d{3}|\\d+)">("12", true);
  expectMatchAllEngines<"(?:\\d{3}|\\d+)">("123", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionEquivalence) {
  // (\w+|\d+)+z should behave identically to (\w+)+z
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123", false);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123", false);
}

// ===== Generalized Prefix/Suffix Factoring =====

TEST(RegexCrossEngineOptTest, GeneralizedPrefixFactoring) {
  // \w+x|\w+y → \w+ factored as common prefix, suffixes merged to [xy]
  {
    constexpr auto& ast = Regex<"(?:\\w+x|\\w+y)">::parsed_;
    // After factoring: Sequence(\w+, [xy]) with no Alternation.
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
    static_assert(rootKind(ast) == NodeKind::Sequence);
  }
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcx", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcy", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcz", false);

  // CharClass common prefix → [a-z](?:1|2) → [a-z][12]
  {
    constexpr auto& ast = Regex<"(?:[a-z]1|[a-z]2)">::parsed_;
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
    static_assert(rootKind(ast) == NodeKind::Sequence);
  }
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("a1", true);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("z2", true);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("a3", false);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("A1", false);

  // Multi-node common prefix → \w+\d+[xy]
  {
    constexpr auto& ast = Regex<"(?:\\w+\\d+x|\\w+\\d+y)">::parsed_;
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
    static_assert(rootKind(ast) == NodeKind::Sequence);
  }
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123x", true);
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123y", true);
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123z", false);

  // Partial group — only some branches share prefix
  {
    constexpr auto& ast = Regex<"(?:\\w+x|\\w+y|\\d+z)">::parsed_;
    // 2 branches share \w+ prefix, 1 doesn't — Alternation preserved with 2
    // branches
    static_assert(rootKind(ast) == NodeKind::Alternation);
    static_assert(getChildCount(ast, ast.root) == 2);
  }
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("abcx", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("123z", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("123x", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("abcz", false);

  // Combined with literal prefix: foo\w+x|foo\w+y → foo\w+[xy]
  {
    constexpr auto& ast = Regex<"(?:foo\\w+x|foo\\w+y)">::parsed_;
    static_assert(ast.prefix_len == 3);
    static_assert(prefixEquals(ast, "foo"));
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
  }
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcx", true);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcy", true);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcz", false);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("barax", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedSuffixFactoring) {
  // x\d+|y\d+ → [xy]\d+ : common \d+ suffix factored out
  {
    constexpr auto& ast = Regex<"(?:x\\d+|y\\d+)">::parsed_;
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
    static_assert(rootKind(ast) == NodeKind::Sequence);
  }
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("x123", true);
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("y456", true);
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("z789", false);

  // a\w+|b\w+ → [ab]\w+ : common \w+ suffix factored out
  {
    constexpr auto& ast = Regex<"(?:a\\w+|b\\w+)">::parsed_;
    static_assert(!hasNodeOfKind(ast, NodeKind::Alternation));
    static_assert(rootKind(ast) == NodeKind::Sequence);
  }
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("abc", true);
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("bxy", true);
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("cxy", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedFactoringSearchMode) {
  expectSearchAllEngines<"(?:\\w+x|\\w+y)">("...abcx...", true);
  expectSearchAllEngines<"(?:\\w+x|\\w+y)">("...abcy...", true);
  expectSearchAllEngines<"(?:x\\d+|y\\d+)">("...x42...", true);
}

// ===== Prefix Through Groups =====

TEST(RegexCrossEngineOptTest, PrefixThroughNonCapturingGroup) {
  constexpr auto& ast = Regex<"(?:abc)def">::parsed_;
  static_assert(ast.prefix_len == 6);
  static_assert(prefixEquals(ast, "abcdef"));
  static_assert(rootKind(ast) == NodeKind::Empty);
  static_assert(!hasNodeOfKind(ast, NodeKind::Group));

  expectMatchAllEngines<"(?:abc)def">("abcdef", true);
  expectMatchAllEngines<"(?:abc)def">("abcde", false);
  expectSearchAllEngines<"(?:abc)def">("xxxabcdefyyy", true, "abcdef");
}

TEST(RegexCrossEngineOptTest, PrefixThroughNonCapturingGroupPartial) {
  constexpr auto& ast = Regex<"(?:abc\\d+)def">::parsed_;
  static_assert(ast.prefix_len == 3);
  static_assert(prefixEquals(ast, "abc"));

  expectMatchAllEngines<"(?:abc\\d+)def">("abc123def", true);
  expectMatchAllEngines<"(?:abc\\d+)def">("abcdef", false);
}

TEST(RegexCrossEngineOptTest, PrefixFullyStrippedCapturingGroup) {
  constexpr auto& ast1 = Regex<"(abc)def">::parsed_;
  static_assert(ast1.prefix_len == 6);
  static_assert(prefixEquals(ast1, "abcdef"));
  static_assert(ast1.prefix_group_adjustment_count == 1);

  auto m1 = expectMatchCapturesAgree<"(abc)def">("abcdef");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[0], "abcdef");
  EXPECT_EQ(m1[1], "abc");

  constexpr auto& ast2 = Regex<"(abc)\\d+">::parsed_;
  static_assert(ast2.prefix_len == 3);
  static_assert(prefixEquals(ast2, "abc"));
  static_assert(ast2.prefix_group_adjustment_count == 1);

  auto m2 = expectMatchCapturesAgree<"(abc)\\d+">("abc123");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[0], "abc123");
  EXPECT_EQ(m2[1], "abc");

  constexpr auto& ast3 = Regex<"foo(bar)baz">::parsed_;
  static_assert(ast3.prefix_len == 9);
  static_assert(prefixEquals(ast3, "foobarbaz"));
  static_assert(ast3.prefix_group_adjustment_count == 1);

  auto m3 = expectMatchCapturesAgree<"foo(bar)baz">("foobarbaz");
  EXPECT_TRUE(m3);
  EXPECT_EQ(m3[0], "foobarbaz");
  EXPECT_EQ(m3[1], "bar");
}

TEST(RegexCrossEngineOptTest, PrefixPartiallyStrippedCapturingGroup) {
  constexpr auto& ast1 = Regex<"(abc\\d+)xyz">::parsed_;
  static_assert(ast1.prefix_len == 3);
  static_assert(prefixEquals(ast1, "abc"));
  static_assert(ast1.prefix_group_adjustment_count == 1);
  static_assert(hasNodeOfKind(ast1, NodeKind::Group));
  constexpr int grpIdx1 = findNodeOfKind(ast1, NodeKind::Group);
  static_assert(ast1.nodes[grpIdx1].capturing);

  auto m1 = expectMatchCapturesAgree<"(abc\\d+)xyz">("abc123xyz");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[0], "abc123xyz");
  EXPECT_EQ(m1[1], "abc123");

  constexpr auto& ast2 = Regex<"(abcdef\\w+)!">::parsed_;
  static_assert(ast2.prefix_len == 6);
  static_assert(prefixEquals(ast2, "abcdef"));
  static_assert(ast2.prefix_group_adjustment_count == 1);
  static_assert(hasNodeOfKind(ast2, NodeKind::Group));

  auto m2 = expectMatchCapturesAgree<"(abcdef\\w+)!">("abcdefghij!");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[0], "abcdefghij!");
  EXPECT_EQ(m2[1], "abcdefghij");
}

TEST(RegexCrossEngineOptTest, PrefixThroughNestedGroups) {
  constexpr auto& ast = Regex<"((abc)def)ghi">::parsed_;
  static_assert(ast.prefix_len == 9);
  static_assert(prefixEquals(ast, "abcdefghi"));
  static_assert(ast.prefix_group_adjustment_count == 2);

  auto m = expectMatchCapturesAgree<"((abc)def)ghi">("abcdefghi");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdefghi");
  EXPECT_EQ(m[1], "abcdef");
  EXPECT_EQ(m[2], "abc");
}

TEST(RegexCrossEngineOptTest, SearchPrefixThroughCapturingGroup) {
  constexpr auto& ast = Regex<"(hello)world">::parsed_;
  static_assert(ast.prefix_len == 10);
  static_assert(prefixEquals(ast, "helloworld"));
  static_assert(ast.prefix_group_adjustment_count == 1);

  auto m = expectSearchCapturesAgree<"(hello)world">("say helloworld now");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "hello");
}

TEST(RegexCrossEngineOptTest, PrefixBlockedByRepeat) {
  constexpr auto& ast = Regex<"(abc)+def">::parsed_;
  static_assert(ast.prefix_len == 0);

  expectMatchAllEngines<"(abc)+def">("abcdef", true);
  expectMatchAllEngines<"(abc)+def">("abcabcdef", true);
  expectMatchAllEngines<"(abc)+def">("def", false);
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

// ===== Suffix Stripping =====

TEST(RegexCrossEngineOptTest, SuffixStripping) {
  // \d+abc → suffix "abc" disjoint from \d+, fully stripped
  constexpr auto& ast1 = Regex<"\\d+abc">::parsed_;
  static_assert(ast1.suffix_len == 3);
  static_assert(suffixEquals(ast1, "abc"));
  static_assert(ast1.suffix_strip_len == 3);

  expectMatchAllEngines<"\\d+abc">("123abc", true);
  expectMatchAllEngines<"\\d+abc">("123ab", false);
  expectSearchAllEngines<"\\d+abc">("xxx123abcyyy", true, "123abc");
}

TEST(RegexCrossEngineOptTest, SuffixNotStripped) {
  // [a-z]+abc → suffix char 'a' overlaps [a-z]+, not stripped
  constexpr auto& ast2 = Regex<"[a-z]+abc">::parsed_;
  static_assert(ast2.suffix_len == 3);
  static_assert(ast2.suffix_strip_len == 0);

  expectMatchAllEngines<"[a-z]+abc">("xyzabc", true);
  expectSearchAllEngines<"[a-z]+abc">("123xyzabc456", true, "xyzabc");
}

TEST(RegexCrossEngineOptTest, SuffixNotStrippedSingleChar) {
  // [a-z]+a → suffix char 'a' overlaps [a-z]+, not stripped
  constexpr auto& ast3 = Regex<"[a-z]+a">::parsed_;
  static_assert(ast3.suffix_len == 1);
  static_assert(ast3.suffix_strip_len == 0);

  expectMatchAllEngines<"[a-z]+a">("xyza", true);
  expectSearchAllEngines<"[a-z]+a">("123xyza456", true, "xyza");
}

TEST(RegexCrossEngineOptTest, SuffixPartialStrip) {
  // [a-z]+xyz123 → suffix "xyz123", barrier at '1' (disjoint from [a-z]+)
  // Strip "123" (3 chars from barrier), keep "xyz" in AST
  constexpr auto& ast4 = Regex<"[a-z]+xyz123">::parsed_;
  static_assert(ast4.suffix_len == 6);
  static_assert(suffixEquals(ast4, "xyz123"));
  static_assert(ast4.suffix_strip_len == 3);

  expectMatchAllEngines<"[a-z]+xyz123">("abcxyz123", true);
  expectSearchAllEngines<"[a-z]+xyz123">("000abcxyz123000", true, "abcxyz123");
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

  // Suffix stripping: \\d+abc
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

TEST(RegexCrossEngineOptTest, SuffixStripWithCaptures) {
  // (\d+)abc → suffix "abc" stripped, captures preserved
  auto m = expectMatchCapturesAgree<"(\\d+)abc">("123abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123abc");
  EXPECT_EQ(m[1], "123");
}

TEST(RegexCrossEngineOptTest, SuffixAlwaysStrippedForMatch) {
  // [a-z]+abc → suffix overlaps, not stripped; verify match correctness
  expectMatchAllEngines<"[a-z]+abc">("xyzabc", true);
  expectMatchAllEngines<"[a-z]+abc">("abc", false);
  expectMatchAllEngines<"[a-z]+abc">("xyz", false);
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
