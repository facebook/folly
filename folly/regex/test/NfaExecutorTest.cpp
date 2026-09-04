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

// ---------- 1. NFA search leftmost-longest ----------

TEST(NfaExecutorTest, SearchLeftmostLongest_CharClass) {
  auto result = Regex<"[a-z]+", Flags::ForceNFA>::search("abc123");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "abc");
}

TEST(NfaExecutorTest, SearchLeftmostLongest_Greedy) {
  auto result = Regex<"a+", Flags::ForceNFA>::search("aaa");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "aaa");
}

TEST(NfaExecutorTest, SearchLeftmostLongest_FirstMatch) {
  auto result = Regex<R"(\d+)", Flags::ForceNFA>::search("abc 123 def 456");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "123");
}

// ---------- 2. NFA match anchored ----------

TEST(NfaExecutorTest, MatchAnchored_ExactMatch) {
  auto result = Regex<"abc", Flags::ForceNFA>::match("abc");
  EXPECT_TRUE(result);
}

TEST(NfaExecutorTest, MatchAnchored_NoPartialMatch) {
  auto result = Regex<"abc", Flags::ForceNFA>::match("abcd");
  EXPECT_FALSE(result);
}

TEST(NfaExecutorTest, MatchAnchored_StarQuantifier) {
  auto result = Regex<"a*b", Flags::ForceNFA>::match("aaab");
  EXPECT_TRUE(result);
}

// ---------- 3. NFA capture groups ----------

TEST(NfaExecutorTest, CaptureGroups_MatchTwoGroups) {
  auto result = Regex<R"((\d+)-(\w+))", Flags::ForceNFA>::match("123-hello");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "123-hello");
  EXPECT_EQ(result[1], "123");
  EXPECT_EQ(result[2], "hello");
}

TEST(NfaExecutorTest, CaptureGroups_SearchSingleGroup) {
  auto result = Regex<R"((\d+))", Flags::ForceNFA>::search("abc 42 def");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "42");
  EXPECT_EQ(result[1], "42");
}

// ---------- 4. NFA with counted repeats ----------

TEST(NfaExecutorTest, CountedRepeats_ExactMatch) {
  auto result = Regex<"a{3}", Flags::ForceNFA>::match("aaa");
  EXPECT_TRUE(result);
}

TEST(NfaExecutorTest, CountedRepeats_TooFew) {
  auto result = Regex<"a{3}", Flags::ForceNFA>::match("aa");
  EXPECT_FALSE(result);
}

TEST(NfaExecutorTest, CountedRepeats_Range) {
  auto result = Regex<"a{2,4}", Flags::ForceNFA>::match("aaa");
  EXPECT_TRUE(result);
}

// ---------- 6. NFA with possessive quantifiers ----------

TEST(NfaExecutorTest, PossessiveQuantifier_Match) {
  auto result = Regex<"a++b", Flags::ForceNFA>::match("aaab");
  EXPECT_TRUE(result);
}

TEST(NfaExecutorTest, PossessiveQuantifier_NoBacktrack) {
  auto result = Regex<"a++a", Flags::ForceNFA>::match("aaa");
  EXPECT_FALSE(result);
}

// ---------- 7. NFA with alternation ----------

TEST(NfaExecutorTest, Alternation_MatchSecond) {
  auto result = Regex<"cat|dog|bird", Flags::ForceNFA>::match("dog");
  EXPECT_TRUE(result);
}

TEST(NfaExecutorTest, Alternation_NoMatch) {
  auto result = Regex<"cat|dog|bird", Flags::ForceNFA>::match("fish");
  EXPECT_FALSE(result);
}

TEST(NfaExecutorTest, Alternation_Search) {
  auto result = Regex<"cat|dog|bird", Flags::ForceNFA>::search("a dog here");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "dog");
}

// ---------- 8. NFA with lookaround probes ----------

TEST(NfaExecutorTest, Lookahead_Positive) {
  auto result = Regex<R"(\d+(?=px))", Flags::ForceNFA>::search("100px");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "100");
}

TEST(NfaExecutorTest, Lookahead_Negative) {
  auto result = Regex<R"(\d+(?!px))", Flags::ForceNFA>::search("100em");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "100");
}

TEST(NfaExecutorTest, Lookbehind_Positive) {
  auto result = Regex<R"((?<=\$)\d+)", Flags::ForceNFA>::search("$100");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "100");
}

// ---------- 9. NFA with anchors ----------

TEST(NfaExecutorTest, Anchor_StartOfString_Match) {
  auto result = Regex<R"(\Afoo)", Flags::ForceNFA>::search("foobar");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "foo");
}

TEST(NfaExecutorTest, Anchor_StartOfString_NoMatch) {
  auto result = Regex<R"(\Afoo)", Flags::ForceNFA>::search("barfoo");
  EXPECT_FALSE(result);
}

TEST(NfaExecutorTest, Anchor_EndOfString_Match) {
  auto result = Regex<R"(foo\z)", Flags::ForceNFA>::search("barfoo");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "foo");
}

// ---------- 5. NFA test function ----------

TEST(NfaExecutorTest, TestFunction_Found) {
  auto found = Regex<R"(\d+)", Flags::ForceNFA>::test("abc 123 def");
  EXPECT_TRUE(found);
}

TEST(NfaExecutorTest, TestFunction_NotFound) {
  auto notFound = Regex<R"(\d+)", Flags::ForceNFA>::test("no digits");
  EXPECT_FALSE(notFound);
}

// ---------- 10. NFA reverse mode ----------

TEST(NfaExecutorTest, ReverseMode_Match) {
  auto result =
      Regex<"abc", Flags::ForceNFA | Flags::ForceReverseExecution>::match(
          "abc");
  EXPECT_TRUE(result);
}

TEST(NfaExecutorTest, ReverseMode_Search) {
  auto result =
      Regex<R"(\d+)", Flags::ForceNFA | Flags::ForceReverseExecution>::search(
          "abc123def");
  ASSERT_TRUE(result);
  EXPECT_EQ(result[0], "123");
}
