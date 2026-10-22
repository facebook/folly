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

  constexpr auto& nfa1 = Regex<"(a|ab)+c">::nfaProg_;
  constexpr auto& nfa2 = Regex<"(ab?)+c">::nfaProg_;
  // After possessive promotion, the factored lazy b?? should be promoted to
  // greedy b? since [b] is disjoint from the follow set [ac]. Verify the
  // NFA Split ordering matches the direct (ab?)+c version.
  static_assert(
      nfa1.states[2].next == nfa2.states[2].next &&
          nfa1.states[2].alt == nfa2.states[2].alt,
      "factored (a|ab)+c optional Split should match direct (ab?)+c after "
      "lazy-to-greedy promotion");

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

// ===== Literal Suffix Factoring in Alternations =====

TEST(RegexCrossEngineOptTest, AlternationCommonLiteralSuffix) {
  // alpha|delta|india → factored to Sequence(Alt(alph,delt,indi), 'a')
  {
    constexpr auto& ast = Regex<"alpha|delta|india">::parsed_;
    static_assert(rootKind(ast) == NodeKind::Sequence);
    static_assert(getChildCount(ast, ast.root) == 2);

    // First child: the Alternation of stripped branches (no non-capturing
    // Group wrapper — the inner re-parse with skipUnnecessaryGroups elides it).
    constexpr int altIdx = getNthChild(ast, ast.root, 0);
    static_assert(ast.nodes[altIdx].kind == NodeKind::Alternation);
    static_assert(getChildCount(ast, altIdx) == 3);

    // Second child: the factored-out trailing literal "a".
    constexpr int litIdx = getLastChild(ast, ast.root);
    static_assert(ast.nodes[litIdx].kind == NodeKind::Literal);
    static_assert(ast.nodes[litIdx].literal == "a");
  }
  expectMatchAllEngines<"alpha|delta|india">("alpha", true);
  expectMatchAllEngines<"alpha|delta|india">("delta", true);
  expectMatchAllEngines<"alpha|delta|india">("india", true);
  expectMatchAllEngines<"alpha|delta|india">("bravo", false);
}

TEST(RegexCrossEngineOptTest, AlternationCommonLiteralSuffixMultiChar) {
  // fooing|barring → factored to Sequence(Alt(foo,barr), Literal("ing"))
  {
    constexpr auto& ast = Regex<"fooing|barring">::parsed_;
    static_assert(rootKind(ast) == NodeKind::Sequence);
    static_assert(getChildCount(ast, ast.root) == 2);

    // First child: the Alternation of stripped branches (no non-capturing
    // Group wrapper — the inner re-parse with skipUnnecessaryGroups elides it).
    constexpr int altIdx = getNthChild(ast, ast.root, 0);
    static_assert(ast.nodes[altIdx].kind == NodeKind::Alternation);
    static_assert(getChildCount(ast, altIdx) == 2);
    constexpr int branch0 = getNthChild(ast, altIdx, 0);
    constexpr int branch1 = getNthChild(ast, altIdx, 1);
    static_assert(ast.nodes[branch0].kind == NodeKind::Literal);
    static_assert(ast.nodes[branch0].literal == "foo");
    static_assert(ast.nodes[branch1].kind == NodeKind::Literal);
    static_assert(ast.nodes[branch1].literal == "barr");

    // Second child: the factored-out trailing literal "ing".
    constexpr int litIdx = getLastChild(ast, ast.root);
    static_assert(ast.nodes[litIdx].kind == NodeKind::Literal);
    static_assert(ast.nodes[litIdx].literal == "ing");
  }
  expectMatchAllEngines<"fooing|barring">("fooing", true);
  expectMatchAllEngines<"fooing|barring">("barring", true);
  expectMatchAllEngines<"fooing|barring">("foong", false);
}

TEST(RegexCrossEngineOptTest, AlternationNoCommonLiteralSuffix) {
  // abc|def|ghi → all different suffixes, no factoring
  constexpr auto& ast = Regex<"abc|def|ghi">::parsed_;
  static_assert(rootKind(ast) == NodeKind::Alternation);
  static_assert(getChildCount(ast, ast.root) == 3);
}

TEST(RegexCrossEngineOptTest, AlternationMixedSuffixGroups) {
  // alpha|delta|bravo|echo → 'a' group (alpha,delta) and 'o' group (bravo,echo)
  expectMatchAllEngines<"alpha|delta|bravo|echo">("alpha", true);
  expectMatchAllEngines<"alpha|delta|bravo|echo">("delta", true);
  expectMatchAllEngines<"alpha|delta|bravo|echo">("bravo", true);
  expectMatchAllEngines<"alpha|delta|bravo|echo">("echo", true);
  expectMatchAllEngines<"alpha|delta|bravo|echo">("charlie", false);
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
