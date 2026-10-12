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

#include <folly/portability/GTest.h>

using namespace folly::regex;

// ---------- 1. DFA anchored match ----------

TEST(DfaExecutorTest, AnchoredMatchExact) {
  auto result = Regex<"abc", Flags::ForceDFA>::match("abc");
  EXPECT_TRUE(result);
}

TEST(DfaExecutorTest, AnchoredMatchRejectsExtra) {
  auto result = Regex<"abc", Flags::ForceDFA>::match("abcd");
  EXPECT_FALSE(result);
}

TEST(DfaExecutorTest, AnchoredMatchCharClass) {
  auto result = Regex<"[a-z]+", Flags::ForceDFA>::match("hello");
  EXPECT_TRUE(result);
}

// ---------- 2. DFA search (tryMatchFrom) ----------

TEST(DfaExecutorTest, SearchFindsDigits) {
  auto result = Regex<R"(\d+)", Flags::ForceDFA>::search("abc 123 def");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "123");
}

TEST(DfaExecutorTest, SearchFindsLiteral) {
  auto result = Regex<"hello", Flags::ForceDFA>::search("say hello there");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "hello");
}

TEST(DfaExecutorTest, SearchReturnsNoMatch) {
  auto result = Regex<R"(\d+)", Flags::ForceDFA>::search("no digits");
  EXPECT_FALSE(result);
}

// ---------- 3. DFA capture groups (tag ops) ----------

TEST(DfaExecutorTest, CaptureGroupsTwoGroups) {
  auto result = Regex<R"((\d+)-(\w+))", Flags::ForceDFA>::match("123-hello");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[1], "123");
  EXPECT_EQ(result[2], "hello");
}

TEST(DfaExecutorTest, CaptureGroupSearchDigit) {
  auto result = Regex<R"((\d+))", Flags::ForceDFA>::search("abc 42 def");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[1], "42");
}

TEST(DfaExecutorTest, CaptureGroupThreeGroups) {
  auto result =
      Regex<R"((\d+)\.(\d+)\.(\d+))", Flags::ForceDFA>::match("1.23.456");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[1], "1");
  EXPECT_EQ(result[2], "23");
  EXPECT_EQ(result[3], "456");
}

// ---------- 4. DFA greedy vs lazy ----------

TEST(DfaExecutorTest, GreedyRepeat) {
  auto result = Regex<"a+", Flags::ForceDFA>::search("aaa");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "aaa");
}

TEST(DfaExecutorTest, LazyRepeat) {
  auto result = Regex<"a+?", Flags::ForceDFA>::search("aaa");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "a");
}

TEST(DfaExecutorTest, GreedyDotStar) {
  auto result = Regex<"a.*b", Flags::ForceDFA>::search("aXXbYYb");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "aXXbYYb");
}

TEST(DfaExecutorTest, LazyDotStar) {
  auto result = Regex<"a.*?b", Flags::ForceDFA>::search("aXXbYYb");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "aXXb");
}

// ---------- 5. DFA with anchors ----------

TEST(DfaExecutorTest, AnchorBeginSearch) {
  auto result = Regex<"^hello", Flags::ForceDFA>::search("hello world");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "hello");
}

TEST(DfaExecutorTest, AnchorBeginSearchRejectsMiddle) {
  auto result = Regex<"^hello", Flags::ForceDFA>::search("say hello");
  EXPECT_FALSE(result);
}

TEST(DfaExecutorTest, AnchorEndSearch) {
  auto result = Regex<"world$", Flags::ForceDFA>::search("hello world");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "world");
}

// ---------- 6. DFA test function (testUnanchored) ----------

TEST(DfaExecutorTest, TestFindsMatch) {
  EXPECT_TRUE((Regex<R"(\d+)", Flags::ForceDFA>::test("abc 123 def")));
}

TEST(DfaExecutorTest, TestRejectsNoMatch) {
  EXPECT_FALSE((Regex<R"(\d+)", Flags::ForceDFA>::test("no digits")));
}

// ---------- 7. DFA with multiline anchors ----------

TEST(DfaExecutorTest, MultilineAnchorSearch) {
  auto result = Regex<"^line", Flags::ForceDFA | Flags::Multiline>::search(
      "first\nline two");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "line");
}

// ---------- 8. DFA with lookaround probes ----------

TEST(DfaExecutorTest, LookaheadProbeAccepts) {
  auto result = Regex<R"((\d+(?=px)))", Flags::ForceDFA>::search("100px");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "100");
}

TEST(DfaExecutorTest, LookaheadProbeRejects) {
  auto result = Regex<R"((\d+(?=px)))", Flags::ForceDFA>::search("100em");
  EXPECT_FALSE(result);
}

// ---------- 9. DFA reverse mode ----------

TEST(DfaExecutorTest, ReverseMatchExact) {
  auto result =
      Regex<"abc", Flags::ForceDFA | Flags::ForceReverseExecution>::match(
          "abc");
  EXPECT_TRUE(result);
}

TEST(DfaExecutorTest, ReverseSearchDigits) {
  auto result =
      Regex<R"(\d+)", Flags::ForceDFA | Flags::ForceReverseExecution>::search(
          "abc123def");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "123");
}

// ---------- 10. DFA possessive quantifiers ----------

TEST(DfaExecutorTest, PossessiveMatchSuccess) {
  auto result = Regex<"a++b", Flags::ForceDFA>::match("aaab");
  EXPECT_TRUE(result);
}

TEST(DfaExecutorTest, PossessiveMatchFailsNoBacktrack) {
  auto result = Regex<"a++a", Flags::ForceDFA>::match("aaa");
  EXPECT_FALSE(result);
}

// ---------- 11. DFA with counted repeats ----------

TEST(DfaExecutorTest, CountedRepeatExact) {
  auto result = Regex<"a{3}", Flags::ForceDFA>::match("aaa");
  EXPECT_TRUE(result);
}

TEST(DfaExecutorTest, CountedRepeatTooFew) {
  auto result = Regex<"a{3}", Flags::ForceDFA>::match("aa");
  EXPECT_FALSE(result);
}

TEST(DfaExecutorTest, CountedRepeatRange) {
  auto result = Regex<"a{2,4}", Flags::ForceDFA>::match("aaa");
  EXPECT_TRUE(result);
}

// ---------- 12. DFA with trailing dot-star ----------

TEST(DfaExecutorTest, TrailingDotStarMatch) {
  auto result = Regex<"abc.*", Flags::ForceDFA>::match("abcxyz");
  EXPECT_TRUE(result);
}

// Site 1: start state is immediately accepting (epsilon DFA after full prefix
// strip + dot-star pruning). The AllLazy early return must not bypass the
// trailing dot-star extension.
TEST(DfaExecutorTest, TrailingDotStarSearch) {
  auto result = Regex<"abc.*", Flags::ForceDFA>::search("xxxabcyyy");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "abcyyy");
}

// Site 2: DFA body requires at least one transition before accepting (char
// class after prefix strip). The AllLazy early return at the in-loop accept
// site must not bypass the trailing dot-star extension.
TEST(DfaExecutorTest, TrailingDotStarSearchWithDfaBody) {
  auto result = Regex<"ab[0-9].*", Flags::ForceDFA>::search("xxab5yyy");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "ab5yyy");

  // Also verify multiple digits before the trailing content.
  auto r2 = Regex<"ab[0-9][0-9].*", Flags::ForceDFA>::search("xxab59yyy");
  EXPECT_TRUE(r2);
  EXPECT_EQ(r2[0], "ab59yyy");
}
