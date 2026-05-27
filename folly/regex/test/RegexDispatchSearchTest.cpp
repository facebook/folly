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

// ===== Auto Mode Tests =====

TEST(RegexAutoModeTest, AutoModeFallback) {
  // Pattern that could cause excessive backtracking
  constexpr auto re = compile<"(a*)*b">();
  // With auto mode, this should complete in bounded time due to NFA fallback
  EXPECT_FALSE(re.match("aaaaaaaaaaaaaaaa"));
  EXPECT_TRUE(re.match("aaab"));
}

TEST(RegexAutoModeTest, SearchScalesWithInput) {
  constexpr auto reNFA = compile<"[a-z]{3}\\d{3}", Flags::ForceNFA>();
  constexpr auto reBT = compile<"[a-z]{3}\\d{3}", Flags::ForceBacktracking>();
  constexpr auto reDFA = compile<"[a-z]{3}\\d{3}", Flags::ForceDFA>();

  // Simple case: match surrounded by non-matching chars
  auto nfa1 = reNFA.search("xxxabc123xxx");
  auto bt1 = reBT.search("xxxabc123xxx");
  auto dfa1 = reDFA.search("xxxabc123xxx");
  EXPECT_TRUE(bt1);
  EXPECT_TRUE(nfa1);
  EXPECT_TRUE(dfa1);
  if (bt1) {
    EXPECT_EQ(std::string(bt1[0]), "abc123");
  }
  if (nfa1 && bt1) {
    EXPECT_EQ(std::string(nfa1[0]), std::string(bt1[0]))
        << "NFA/BT disagree on match content";
  }
  if (dfa1 && bt1) {
    EXPECT_EQ(std::string(dfa1[0]), std::string(bt1[0]))
        << "DFA/BT disagree on match content";
  }

  // Match planted deep in a large input
  std::string big(1024, 'x');
  big.replace(1000, 6, "abc123");
  auto nfaBig = reNFA.search(big);
  auto btBig = reBT.search(big);
  auto dfaBig = reDFA.search(big);
  EXPECT_TRUE(btBig);
  EXPECT_TRUE(nfaBig) << "NFA missed match at pos 1000 in 1KB input";
  EXPECT_TRUE(dfaBig) << "DFA missed match at pos 1000 in 1KB input";
  if (nfaBig && btBig) {
    EXPECT_EQ(std::string(nfaBig[0]), std::string(btBig[0]))
        << "NFA/BT disagree on 1KB input";
  }
  if (dfaBig && btBig) {
    EXPECT_EQ(std::string(dfaBig[0]), std::string(btBig[0]))
        << "DFA/BT disagree on 1KB input";
  }

  // No-match input — should agree on no match
  std::string noMatch(1024, 'x');
  EXPECT_FALSE(reNFA.search(noMatch));
  EXPECT_FALSE(reBT.search(noMatch));
  EXPECT_FALSE(reDFA.search(noMatch));
}

TEST(RegexAutoModeTest, DfaSinglePassSearchAlignment) {
  constexpr auto reBT =
      compile<"[a-z][a-z][a-z]\\d", Flags::ForceBacktracking>();
  constexpr auto reDFA = compile<"[a-z][a-z][a-z]\\d", Flags::ForceDFA>();

  // Aligned: 3 skip chars before match (cycle aligns)
  auto bt1 = reBT.search("xxxabc1xxx");
  auto dfa1 = reDFA.search("xxxabc1xxx");
  EXPECT_TRUE(bt1);
  EXPECT_TRUE(dfa1);
  if (bt1 && dfa1) {
    EXPECT_EQ(std::string(dfa1[0]), std::string(bt1[0]));
  }

  // Misaligned: 4 skip chars before match (cycle off by 1)
  auto bt2 = reBT.search("xxxxabc1xxx");
  auto dfa2 = reDFA.search("xxxxabc1xxx");
  EXPECT_TRUE(bt2);
  EXPECT_TRUE(dfa2) << "DFA missed match due to alignment";
  if (bt2 && dfa2) {
    EXPECT_EQ(std::string(dfa2[0]), std::string(bt2[0]));
  }

  // Misaligned deep in large input
  std::string big(1024, 'x');
  big.replace(1000, 4, "abc1");
  auto btBig = reBT.search(big);
  auto dfaBig = reDFA.search(big);
  EXPECT_TRUE(btBig);
  EXPECT_TRUE(dfaBig) << "DFA missed match at misaligned position in 1KB input";
  if (btBig && dfaBig) {
    EXPECT_EQ(std::string(dfaBig[0]), std::string(btBig[0]));
  }
}

// ===== Cross-Engine: Alternation Dispatch Tests =====

TEST(RegexCrossEngineTest, AlternationDispatch) {
  // 7 branches, no common prefix/suffix, no single position discriminates
  // all 7 (t appears at pos 0 for tue/thu, s for sat/sun, etc.)
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">(
      "today is fri", true, "fri");
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">(
      "it is monday", true, "mon");
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">("no match", false);

  // Branches of different known lengths, discriminator within shortest
  expectSearchAllEngines<"(abc|defg|hijkl)">("xxxdefgyyy", true, "defg");
  expectSearchAllEngines<"(abc|defg|hijkl)">("xxxabcyyy", true, "abc");
  expectSearchAllEngines<"(abc|defg|hijkl)">("xxxhijklyyy", true, "hijkl");

  // 3 branches, position 0 discriminates
  expectSearchAllEngines<"(abc|def|ghi)">("xxxdefyyy", true, "def");
  expectSearchAllEngines<"(abc|def|ghi)">("xxxghiyyy", true, "ghi");
}

TEST(RegexCrossEngineTest, AlternationDispatchNonZeroOffset) {
  // CharClass overlap at position 0 forces discriminator to position 1.
  // Branches 0 and 1 share [a-m] at pos 0, so pos 0 is not pairwise
  // disjoint. Position 1 has x/y/z — all distinct.
  expectSearchAllEngines<"([a-m]x|[a-m]y|[n-z]z)">("bx", true, "bx");
  expectSearchAllEngines<"([a-m]x|[a-m]y|[n-z]z)">("cy", true, "cy");
  expectSearchAllEngines<"([a-m]x|[a-m]y|[n-z]z)">("nz", true, "nz");
  expectSearchAllEngines<"([a-m]x|[a-m]y|[n-z]z)">("na", false);

  // 4 branches: first 3 share [0-9] at pos 0. Position 1 (a/b/c/d) is
  // the earliest fully disjoint position.
  expectSearchAllEngines<"([0-9]a|[0-9]b|[0-9]c|[a-z]d)">("5a", true, "5a");
  expectSearchAllEngines<"([0-9]a|[0-9]b|[0-9]c|[a-z]d)">("3c", true, "3c");
  expectSearchAllEngines<"([0-9]a|[0-9]b|[0-9]c|[a-z]d)">("xd", true, "xd");
  expectSearchAllEngines<"([0-9]a|[0-9]b|[0-9]c|[a-z]d)">("xe", false);
}

TEST(RegexCrossEngineTest, AlternationDispatchNestedPropagation) {
  // Each inner alternation has 2 branches (below the 3-branch threshold
  // for independent search). The outer alternation has 3 branches.
  // Outer discriminates at position 0: [a-c] vs [d-f] vs [g-i] — disjoint.
  // Propagation forces the inner 2-branch alternations to use position 0,
  // which they wouldn't select independently.
  expectSearchAllEngines<"(([a-c]x|[d-f]y)|([g-i]x|[j-l]y)|([m-o]x|[p-r]y))">(
      "ax", true, "ax");
  expectSearchAllEngines<"(([a-c]x|[d-f]y)|([g-i]x|[j-l]y)|([m-o]x|[p-r]y))">(
      "ey", true, "ey");
  expectSearchAllEngines<"(([a-c]x|[d-f]y)|([g-i]x|[j-l]y)|([m-o]x|[p-r]y))">(
      "hx", true, "hx");
  expectSearchAllEngines<"(([a-c]x|[d-f]y)|([g-i]x|[j-l]y)|([m-o]x|[p-r]y))">(
      "py", true, "py");
  expectSearchAllEngines<"(([a-c]x|[d-f]y)|([g-i]x|[j-l]y)|([m-o]x|[p-r]y))">(
      "sz", false);
}

TEST(RegexCrossEngineTest, AlternationDispatchEdgeCases) {
  // 3 branches with different widths
  expectSearchAllEngines<"(foo|ba|quux)">("ba", true, "ba");
  expectSearchAllEngines<"(foo|ba|quux)">("quux", true, "quux");

  // 2 branches — below threshold, sequential trial
  expectSearchAllEngines<"(ab|cd)">("cd", true, "cd");

  // After factoring, this becomes aa[bcd] — no alternation dispatch
  expectSearchAllEngines<"(aab|aac|aad)">("aac", true, "aac");

  // No match in any branch
  expectSearchAllEngines<"(abc|def|ghi)">("xyz", false);
}

TEST(RegexCrossEngineTest, AlternationDispatchWithCaptures) {
  auto m1 = expectMatchCapturesAgree<"(abc|def|ghi)">("def");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[1], "def");

  auto m2 = expectMatchCapturesAgree<"(abc|defg|hijkl)">("hijkl");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "hijkl");

  // Captures with discriminator at position 1 (CharClass overlap at pos 0)
  auto m3 = expectSearchCapturesAgree<"([a-m]x|[a-m]y|[n-z]z)">("__cy__");
  EXPECT_TRUE(m3);
  EXPECT_EQ(m3[1], "cy");

  auto m4 = expectSearchCapturesAgree<"([a-m]x|[a-m]y|[n-z]z)">("__nz__");
  EXPECT_TRUE(m4);
  EXPECT_EQ(m4[1], "nz");
}

TEST(RegexCrossEngineTest, NfaDiscriminatorPruning) {
  // 7-branch alternation — NFA should prune to 1 branch
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">(
      "today is fri", true, "fri");
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">(
      "it is monday", true, "mon");
  expectSearchAllEngines<"(mon|tue|wed|thu|fri|sat|sun)">("no match", false);

  // Discriminator at offset 0
  expectSearchAllEngines<"(abc|def|ghi)">("xxxdefyyy", true, "def");

  // Discriminator at offset > 0
  expectSearchAllEngines<"(cars|cats|bags)">("my cats", true, "cats");

  // All branches with no match
  expectSearchAllEngines<"(abc|def|ghi)">("zzz", false);

  // Match each branch of a 3-way alternation
  expectSearchAllEngines<"(foo|bar|baz)">("xxfoo", true, "foo");
  expectSearchAllEngines<"(foo|bar|baz)">("xxbar", true, "bar");
  expectSearchAllEngines<"(foo|bar|baz)">("xxbaz", true, "baz");
}

TEST(RegexCrossEngineTest, NfaDiscriminatorPruningWithCaptures) {
  // Verify captures are correct when discriminator pruning is active
  auto m1 = expectSearchCapturesAgree<"(abc|def|ghi)">("xxxghiyyy");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[1], "ghi");

  auto m2 = expectSearchCapturesAgree<"(cars|cats|bags)">("my bags");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "bags");
}

// ===== Cross-Engine: Search Sliding Window Tests =====

TEST(RegexCrossEngineTest, SearchSlidingWindow) {
  // a{3}b{3}: many a's before the actual match
  expectSearchAllEngines<"a{3}b{3}">("aaaaacaaaaaaaaabbbaaa", true, "aaabbb");

  // Leading repeat with excess chars
  expectSearchAllEngines<"[a-z]{3}\\d">("abcdefg1xy", true, "efg1");

  // Leading repeat, match at end
  expectSearchAllEngines<"x{5}y">("xxxxxxxxy", true, "xxxxxy");

  // No leading repeat — sliding window not applicable, but still correct
  expectSearchAllEngines<"ab{3}">("aabbbaaa", true, "abbb");

  // Leading repeat with min > 1 and bounded max
  expectSearchAllEngines<"a{2,4}b">("aaaaab", true, "aaaab");

  // Leading repeat at exact minimum, no excess to slide
  expectSearchAllEngines<"a{3}b">("aaab", true, "aaab");

  // Leading repeat with long non-matching prefix
  expectSearchAllEngines<"z{2}\\d">("zzzzzzzzzzzzzzzzzzz3", true, "zz3");

  // Bool-only search (test) path — no capture tracking needed
  EXPECT_TRUE((Regex<"a{3}b{3}">::test("aaaaacaaaaaaaaabbbaaa")));
  EXPECT_TRUE((Regex<"[a-z]{3}\\d">::test("abcdefg1xy")));

  // Possessive leading repeat — slides without backtracking
  expectSearchAllEngines<"a{3,}+b">("aaaaab", true, "aaaaab");
  expectSearchAllEngines<"[a-z]{2,}+\\d">("abcdef1", true, "abcdef1");
  expectSearchAllEngines<"x{3}+y">("xxxxxxy", true, "xxxy");
}

// ===== Cross-Engine: 3+ Branch Empty Alternation =====

TEST(RegexCrossEngineTest, EmptyAlternation3Plus) {
  // Empty first — lazy optional: (?:|foo|bar) → (?:foo|bar)??
  expectMatchAllEngines<"(?:|foo|bar)">("foo", true);
  expectMatchAllEngines<"(?:|foo|bar)">("bar", true);
  expectMatchAllEngines<"(?:|foo|bar)">("", true);
  expectMatchAllEngines<"(?:|foo|bar)">("baz", false);

  // Empty last — greedy optional: (?:foo|bar|) → (?:foo|bar)?
  expectSearchAllEngines<"(?:foo|bar|)">("foobar", true, "foo");
  expectMatchAllEngines<"(?:foo|bar|)">("", true);

  // Empty middle: (?:foo||bar) → (?:foo|bar)?
  expectMatchAllEngines<"(?:foo||bar)">("foo", true);
  expectMatchAllEngines<"(?:foo||bar)">("bar", true);
  expectMatchAllEngines<"(?:foo||bar)">("", true);
}

// ===== Cross-Engine: Implicit Empty via min_repeat == 0 =====

TEST(RegexCrossEngineTest, ImplicitEmptyAlternation) {
  // a* is a bare Repeat with min_repeat=0 → promote to a+, wrap in ?
  expectSearchAllEngines<"(?:a*|b)">("b", true, "b");
  expectSearchAllEngines<"(?:a*|b)">("aaa", true, "aaa");
  expectMatchAllEngines<"(?:a*|b)">("", true);

  // a? is a bare Repeat with min_repeat=0 → promote to a, wrap in ?
  expectMatchAllEngines<"(?:a?|b)">("a", true);
  expectMatchAllEngines<"(?:a?|b)">("b", true);
  expectMatchAllEngines<"(?:a?|b)">("", true);

  // Multiple bare Repeats with min_repeat == 0
  expectSearchAllEngines<"(?:a*|b*|c)">("c", true, "c");
  expectSearchAllEngines<"(?:a*|b*|c)">("aaa", true, "aaa");

  // NOT eligible: foo* parses as fo + o*, the branch is a Sequence
  // with mandatory 'f' and 'o', so it's not possibly-zero-width
  expectSearchAllEngines<"(?:foo*|bar)">("fo", true, "fo");
}

// ===== Cross-Engine: Zero-Width Repeat Simplification =====

TEST(RegexCrossEngineTest, ZeroWidthRepeatSimplification) {
  // \b+ → \b (min_repeat >= 1 with zero-width inner)
  expectSearchAllEngines<"\\b+foo">("hello foo", true, "foo");

  // \b* → \b? (min_repeat == 0 with zero-width inner)
  expectSearchAllEngines<"\\b*foo">("foo", true, "foo");

  // \b{3} → \b (repeating a zero-width match doesn't change it)
  expectSearchAllEngines<"\\b{3}foo">("hello foo", true, "foo");

  // ^+ → ^ (anchors are zero-width)
  expectMatchAllEngines<"^+abc">("abc", true);

  // $+ → $ (anchors are zero-width)
  expectMatchAllEngines<"abc$+">("abc", true);
}

// ===== Cross-Engine: Uniform Greediness =====

TEST(RegexCrossEngineTest, UniformGreediness) {
  // Both branches greedy, promoted: (?:a*|b*) → (?:a+|b+)?
  expectSearchAllEngines<"(?:a*|b*)">("aaa", true, "aaa");
  expectMatchAllEngines<"(?:a*|b*)">("", true);
}

// ===== Cross-Engine: NFA Leftmost-Longest Search Fix =====

TEST(RegexCrossEngineTest, NfaSearchLeftmostLongest) {
  // Verify leftmost match wins even when a shorter match at a later position
  // has higher thread priority
  expectSearchAllEngines<"[a-z]+\\d+">("abc123def456", true, "abc123");

  // Greedy quantifiers should produce the longest match from the leftmost
  // start position
  expectSearchAllEngines<"a+">("aaa", true, "aaa");
  expectSearchAllEngines<"a.*b">("aXXb", true, "aXXb");
}

TEST(RegexCrossEngineTest, NfaBacktrackerAgreement) {
  // Anchored match tests — these exercise NfaRunner::matchAnchored which
  // correctly handles the leftmost-longest semantics
  expectMatchAllEngines<"(a|ab)+c">("ac", true);
  expectMatchAllEngines<"(a|ab)+c">("abc", true);
  expectMatchAllEngines<"(a|ab)+c">("ababc", true);
  expectMatchAllEngines<"(a|ab)+c">("aababc", true);
  expectMatchAllEngines<"(a|ab)+c">("ababababc", true);

  // Equivalent optimized form
  expectMatchAllEngines<"(ab?)+c">("ababc", true);
  expectMatchAllEngines<"(ab?)+c">("ac", true);

  // Search for patterns with no suffix stripping (suffix char overlaps)
  // (ab?)+ has no suffix to strip — tests findFirst + matchAnchored directly
  expectSearchAllEngines<"(ab?)+">("xababx", true, "abab");

  // Test (ab?)+c directly
  expectSearchAllEngines<"(ab?)+c">("xababcx", true, "ababc");
}

// ===== Cross-Engine: NFA Linked-List Earliest-Start Tracking =====

TEST(RegexCrossEngineTest, NfaEarliestStartTracking) {
  // Verify the leftmost-match early return works correctly
  // with the linked-list thread management approach.
  expectSearchAllEngines<"(a|ab)+c">("xababcx", true, "ababc");
  expectSearchAllEngines<"[a-z]+\\d+">("abc123def456", true, "abc123");
}
