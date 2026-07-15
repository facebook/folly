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

// ===== Greedy vs Lazy Semantics Tests =====
//
// These tests verify that greedy and lazy quantifiers produce different
// match content in search operations across all engines (backtracker,
// NFA, and DFA). The DFA tracks per-quantifier lazy/greedy preference
// via accept_early tagged states, matching the NFA and backtracker.

TEST(RegexGreedyLazyTest, GreedySearchAllEnginesAgree) {
  expectSearchAllEngines<"[a-z]+">("abc123", true, "abc");
  expectSearchAllEngines<"a.*b">("aXXbYYb", true, "aXXbYYb");
}

TEST(RegexGreedyLazyTest, LazySearchAllEnginesAgree) {
  expectSearchAllEngines<"[a-z]+?">("abc123", true, "a");
  expectSearchAllEngines<"a.*?b">("aXXbYYb", true, "aXXb");
  expectSearchAllEngines<"a??">("abc", true, "");
  expectSearchAllEngines<"a?">("abc", true, "a");
}

TEST(RegexGreedyLazyTest, LazyStarNoShorterOption) {
  // a*?b must consume all a's before the only b — no shorter option
  expectSearchAllEngines<"a*?b">("aaab", true, "aaab");
}

TEST(RegexGreedyLazyTest, GreedyVsLazyDifferentMatchLength) {
  // a+a greedy vs a+?a lazy on "aaa" produce different match lengths.
  expectSearchAllEngines<"a+a">("aaa", true, "aaa");
  expectSearchAllEngines<"a+?a">("aaa", true, "aa");
}

TEST(RegexGreedyLazyTest, LazyVsGreedySameFollowingContent) {
  // When following content constrains to a unique match position,
  // lazy and greedy produce the same result. All engines agree.
  expectSearchAllEngines<"[a-z]+?\\d">("abc1", true, "abc1");
  expectSearchAllEngines<"[a-z]+\\d">("abc1", true, "abc1");
}

TEST(RegexGreedyLazyTest, FullMatchGreedyLazyEquivalent) {
  expectMatchAllEngines<"a{2,5}">("aaa", true);
  expectMatchAllEngines<"a{2,5}?">("aaa", true);
  expectMatchAllEngines<"a{2,5}">("a", false);
  expectMatchAllEngines<"a{2,5}?">("a", false);
}

TEST(RegexGreedyLazyTest, DfaLazyMatchContent) {
  // The DFA now correctly returns shortest match for lazy patterns.
  auto greedy = Regex<"[a-z]+", Flags::ForceDFA>::search("abc");
  auto lazy = Regex<"[a-z]+?", Flags::ForceDFA>::search("abc");
  EXPECT_EQ(std::string(greedy[0]), "abc");
  EXPECT_EQ(std::string(lazy[0]), "a");
}

TEST(RegexGreedyLazyTest, GreedyPatternsUnchanged) {
  // All-greedy patterns produce identical results to before
  expectSearchAllEngines<"[a-z]+\\d">("abc1", true, "abc1");
  expectSearchAllEngines<"a.*b">("aXXb", true, "aXXb");
  expectSearchAllEngines<"a+">("aaa", true, "aaa");
}

TEST(RegexGreedyLazyTest, MixedGreedyLazy) {
  // When following required content forces extension, lazy and greedy
  // produce the same full match from the leftmost start position.
  expectSearchAllEngines<"[a-z]+?\\d+">("abc123", true, "abc123");
  expectSearchAllEngines<"[a-z]+\\d+">("abc123", true, "abc123");

  // Lazy vs greedy dot-plus produces different match boundaries
  // when multiple valid match endpoints exist from the same start.
  expectSearchAllEngines<"a.+?a">("abaca", true, "aba");
  expectSearchAllEngines<"a.+a">("abaca", true, "abaca");
}

TEST(RegexGreedyLazyTest, MixedGreedinessCapturesBTvsNFA) {
  // (a+?)(a+) on "aaaa": lazy group 1 matches minimum (1),
  // greedy group 2 matches the remaining (3).
  auto bt = Regex<"(a+?)(a+)", Flags::ForceBacktracking>::search("aaaa");
  EXPECT_TRUE(bt);
  EXPECT_EQ(std::string(bt[1]), "a");
  EXPECT_EQ(std::string(bt[2]), "aaa");

  if constexpr (Regex<"(a+?)(a+)">::parsed_.nfa_compatible) {
    auto nfa = Regex<"(a+?)(a+)", Flags::ForceNFA>::search("aaaa");
    EXPECT_TRUE(nfa);
    EXPECT_EQ(std::string(nfa[1]), std::string(bt[1]));
    EXPECT_EQ(std::string(nfa[2]), std::string(bt[2]));
  }
}
