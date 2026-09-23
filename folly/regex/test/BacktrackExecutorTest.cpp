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

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;

// ===== CharTestTrait Specializations =====
// Tests verify the tight iterative scan loop works correctly for various
// CharClass range counts.

TEST(BacktrackExecutor, CharTestTrait_1Range) {
  // 1-range CharClass: [a-z] uses direct inline comparison
  EXPECT_TRUE((Regex<"[a-z]+x", Flags::ForceBacktracking>::match("abcx")));
  EXPECT_FALSE((Regex<"[a-z]+x", Flags::ForceBacktracking>::match("ABCX")));
}

TEST(BacktrackExecutor, CharTestTrait_2Range) {
  // 2-range CharClass: [a-zA-Z] uses two inline comparisons
  EXPECT_TRUE(
      (Regex<"[a-zA-Z]+1", Flags::ForceBacktracking>::match("abcABC1")));
  EXPECT_FALSE(
      (Regex<"[a-zA-Z]+1", Flags::ForceBacktracking>::match("1234561")));
}

TEST(BacktrackExecutor, CharTestTrait_3Range) {
  // 3-range CharClass: [a-zA-Z0-9] uses three inline comparisons
  EXPECT_TRUE(
      (Regex<"[a-zA-Z0-9]+!", Flags::ForceBacktracking>::match("aB3!")));
  EXPECT_FALSE(
      (Regex<"[a-zA-Z0-9]+!", Flags::ForceBacktracking>::match("!!!")));
}

TEST(BacktrackExecutor, CharTestTrait_4Range) {
  // 4-range CharClass: [a-fA-F0-9_] produces 4 ranges, uses four inline
  // comparisons
  EXPECT_TRUE(
      (Regex<"[a-fA-F0-9_]+.", Flags::ForceBacktracking>::match("aF9_x")));
  EXPECT_FALSE((Regex<"[a-fA-F0-9_]+.", Flags::ForceBacktracking>::match("x")));
}

TEST(BacktrackExecutor, CharTestTrait_5PlusRange) {
  // 5+ range CharClass falls back to charClassTestAt.
  // Negated class [^aeiou] produces 6 complement ranges.
  EXPECT_TRUE((Regex<"[^aeiou]+x", Flags::ForceBacktracking>::match("bcdfx")));
  EXPECT_FALSE(
      (Regex<"[^aeiou]+x", Flags::ForceBacktracking>::match("aeioux")));
}

TEST(BacktrackExecutor, CharTestTrait_SingleCharLiteral) {
  // Single-char Literal uses CharTestTrait with literal comparison
  EXPECT_TRUE((Regex<"a+b", Flags::ForceBacktracking>::match("aaab")));
  EXPECT_FALSE((Regex<"a+b", Flags::ForceBacktracking>::match("bbbb")));
}

TEST(BacktrackExecutor, CharTestTrait_AnyByte) {
  // AnyByte (\C) matches any byte including newline
  EXPECT_TRUE((Regex<R"(\C+x)", Flags::ForceBacktracking>::match("abc\nx")));
  EXPECT_FALSE((Regex<R"(\C+x)", Flags::ForceBacktracking>::match("abc\n")));
}

// ===== Sliding Window Optimization =====
// Root repeat search slides forward within already-scanned regions.

TEST(BacktrackExecutor, SlidingWindow_RootRepeatSearch) {
  auto result = Regex<"a+b", Flags::ForceBacktracking>::search("aaacaaab");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "aaab");
}

TEST(BacktrackExecutor, SlidingWindow_PossessiveRootRepeat) {
  auto result = Regex<"a++b", Flags::ForceBacktracking>::search("aaacaaab");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "aaab");
}

TEST(BacktrackExecutor, SlidingWindow_CapturesUpdated) {
  // Captures should reflect the sliding position, not the original scan start
  auto result = Regex<"(a+)b", Flags::ForceBacktracking>::search("aaacaaab");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[1], "aaa");
}

// ===== Budget Mechanism =====
// Budget limits backtracking to prevent catastrophic performance.

TEST(BacktrackExecutor, Budget_AutoModeShortInput) {
  // Auto mode on (a*)*b with input "aaaaaaaaaa": match returns false (no 'b'),
  // should not hang
  auto result = Regex<"(a*)*b">::match("aaaaaaaaaa");
  EXPECT_FALSE(result);
}

TEST(BacktrackExecutor, Budget_AutoModeLongInput) {
  // (a*)*b on 100 'a's completes in bounded time
  // Doing this without the budget will cause the test to run for at least 7
  // minutes, likely more. 7 is just where I manually killed it the first time.
  auto result = Regex<"(a*)*b">::match(std::string(100, 'a'));
  EXPECT_FALSE(result);
}

TEST(BacktrackExecutor, Budget_FallbackCorrectResult) {
  // Auto mode on non-safe pattern still produces correct result
  auto result = Regex<"(a*)*b">::match("ab");
  EXPECT_TRUE(result);
}

// ===== Greedy vs Lazy in Backtracker =====

TEST(BacktrackExecutor, GreedySearch) {
  auto result = Regex<"a+", Flags::ForceBacktracking>::search("aaa");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "aaa");
}

TEST(BacktrackExecutor, LazySearch) {
  auto result = Regex<"a+?", Flags::ForceBacktracking>::search("aaa");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "a");
}

TEST(BacktrackExecutor, LazyCounted) {
  auto result = Regex<"a{2,5}?", Flags::ForceBacktracking>::search("aaaaa");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "aa");
}

// ===== CompoundRepeatTrait =====
// Tests the compound repeat optimization for patterns like (/[a-z]+)*.

TEST(BacktrackExecutor, CompoundRepeatTrait_MultipleIterations) {
  EXPECT_TRUE((Regex<R"((?:/[a-z]+)*x)", Flags::ForceBacktracking>::match(
      "/abc/def/ghix")));
}

TEST(BacktrackExecutor, CompoundRepeatTrait_ZeroIterations) {
  EXPECT_TRUE(
      (Regex<R"((?:/[a-z]+)*x)", Flags::ForceBacktracking>::match("x")));
}

TEST(BacktrackExecutor, CompoundRepeatTrait_SingleIteration) {
  EXPECT_TRUE(
      (Regex<R"((?:/[a-z]+)*x)", Flags::ForceBacktracking>::match("/abcx")));
}

// ===== Atomic Group Handling =====

TEST(BacktrackExecutor, AtomicGroup_Match) {
  EXPECT_TRUE((Regex<"(?>a+)b", Flags::ForceBacktracking>::match("aaab")));
}

TEST(BacktrackExecutor, AtomicGroup_NoBacktrack) {
  // Atomic group consumes all a's; trailing 'a' can't match because
  // the atomic group prevents backtracking
  EXPECT_FALSE((Regex<"(?>a+)a", Flags::ForceBacktracking>::match("aaa")));
}

TEST(BacktrackExecutor, AtomicGroup_Alternation) {
  // Atomic group commits to first matching alternative "ab";
  // trailing "b" then fails with no backtrack into the group
  EXPECT_FALSE((Regex<"(?>ab|a)b", Flags::ForceBacktracking>::match("ab")));
}

// ===== Reverse-Direction Backtracking =====

TEST(BacktrackExecutor, Reverse_LiteralMatch) {
  EXPECT_TRUE((
      Regex<"abc", Flags::ForceBacktracking | Flags::ForceReverseExecution>::
          match("abc")));
}

TEST(BacktrackExecutor, Reverse_Search) {
  auto result =
      Regex<R"(\d+)", Flags::ForceBacktracking | Flags::ForceReverseExecution>::
          search("abc123def");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "123");
}

TEST(BacktrackExecutor, Reverse_Possessive) {
  EXPECT_TRUE((
      Regex<"a++b", Flags::ForceBacktracking | Flags::ForceReverseExecution>::
          match("aaab")));
}

TEST(BacktrackExecutor, Reverse_PossessiveNoBacktrack) {
  // Possessive a++ consumes all a's; trailing 'a' can't match
  EXPECT_FALSE((
      Regex<"a++a", Flags::ForceBacktracking | Flags::ForceReverseExecution>::
          match("aaa")));
}
