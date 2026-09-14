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

// ===== Backtracker-Only Tests =====
//
// These tests exercise features that only the backtracker supports:
// backreferences and word boundaries. These features set
// nfa_compatible=false, so NFA/DFA engines cannot run them.

TEST(RegexBacktrackerOnlyTest, SimpleBackref) {
  constexpr auto re = compile<"(\\w+) \\1">();
  EXPECT_TRUE(re.match("hello hello"));
  EXPECT_FALSE(re.match("hello world"));
}

TEST(RegexBacktrackerOnlyTest, BackrefCapture) {
  constexpr auto re = compile<"(\\w+) \\1">();
  auto m = re.match("abc abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexBacktrackerOnlyTest, BackrefSearch) {
  constexpr auto re = compile<"(\\w+) \\1">();
  auto m = re.search("say hello hello there");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "hello hello");
  EXPECT_EQ(m[1], "hello");
}

TEST(RegexBacktrackerOnlyTest, BackrefNoMatch) {
  constexpr auto re = compile<"(a)\\1">();
  EXPECT_FALSE(re.match("ab"));
  EXPECT_TRUE(re.match("aa"));
}

TEST(RegexBacktrackerOnlyTest, MultipleBackrefs) {
  constexpr auto re = compile<"(\\w)(\\w) \\2\\1">();
  EXPECT_TRUE(re.match("ab ba"));
  EXPECT_FALSE(re.match("ab ab"));
}

TEST(RegexBacktrackerOnlyTest, WordBoundaryBasic) {
  constexpr auto re = compile<R"(\bword\b)">();
  EXPECT_TRUE(re.search("a word here"));
  EXPECT_TRUE(re.search("word"));
  EXPECT_FALSE(re.search("sword"));
  EXPECT_FALSE(re.search("wordy"));
  EXPECT_FALSE(re.search("swordy"));
}

TEST(RegexBacktrackerOnlyTest, WordBoundaryAtEdges) {
  constexpr auto re = compile<R"(\bfoo\b)">();
  EXPECT_TRUE(re.search("foo"));
  EXPECT_TRUE(re.search("foo bar"));
  EXPECT_TRUE(re.search("bar foo"));
  EXPECT_FALSE(re.search("foobar"));
}

TEST(RegexBacktrackerOnlyTest, NegWordBoundary) {
  constexpr auto re = compile<R"(\Boo\B)">();
  EXPECT_TRUE(re.search("foobar"));
  EXPECT_FALSE(re.search("oo"));
}
