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

TEST(RegexCrossEngineTest, PossessiveStarNoBacktrack) {
  expectMatchAllEngines<R"(a*+a)">("aaa", false);
  expectMatchAllEngines<R"(a*+a)">("a", false);
}

TEST(RegexCrossEngineTest, PossessivePlusNoBacktrack) {
  expectMatchAllEngines<R"(a++a)">("aaa", false);
  expectMatchAllEngines<R"(a++a)">("a", false);
}

TEST(RegexCrossEngineTest, PossessiveOptNoBacktrack) {
  expectMatchAllEngines<R"(a?+a)">("a", false);
}

TEST(RegexCrossEngineTest, PossessiveStarSuccess) {
  expectMatchAllEngines<R"(a*+b)">("aaab", true);
  expectMatchAllEngines<R"(a*+b)">("b", true);
  expectMatchAllEngines<R"(a*+b)">("ab", true);
}

TEST(RegexCrossEngineTest, PossessivePlusSuccess) {
  expectMatchAllEngines<R"(a++b)">("aaab", true);
  expectMatchAllEngines<R"(a++b)">("b", false);
  expectMatchAllEngines<R"(a++b)">("ab", true);
}

TEST(RegexCrossEngineTest, PossessiveCountedNoBacktrack) {
  expectMatchAllEngines<R"(a{2,4}+a)">("aaa", false);
  expectMatchAllEngines<R"(a{2,4}+a)">("aaaa", false);
  expectMatchAllEngines<R"(a{2,4}+a)">("aaaaa", true);
}

TEST(RegexCrossEngineTest, PossessiveCountedSuccess) {
  expectMatchAllEngines<R"(a{2,4}+b)">("aab", true);
  expectMatchAllEngines<R"(a{2,4}+b)">("aaaab", true);
  expectMatchAllEngines<R"(a{2,4}+b)">("ab", false);
}

TEST(RegexCrossEngineTest, PossessiveSearch) {
  expectSearchAllEngines<R"(a++b)">("xxxaaabyyy", true, "aaab");
  expectSearchAllEngines<R"([a-z]++\d)">("123abc1xyz", true, "abc1");
}

TEST(RegexCrossEngineTest, PossessiveNfaCompat) {
  static_assert(Regex<"a++">::parsed_.nfa_compatible);
  static_assert(Regex<"a*+">::parsed_.nfa_compatible);
  static_assert(Regex<"a?+">::parsed_.nfa_compatible);
  static_assert(Regex<"a{2,4}+">::parsed_.nfa_compatible);
}

TEST(RegexCrossEngineTest, PossessiveMultiCharInner) {
  // 2-char literal inner: (ab)*+ followed by ab — overlapping
  expectMatchAllEngines<R"((ab)*+ab)">("ab", false);
  expectMatchAllEngines<R"((ab)*+ab)">("abab", false);
  expectMatchAllEngines<R"((ab)*+ab)">("ababab", false);
  expectMatchAllEngines<R"((ab)*+ab)">("", false);

  // 2-char literal inner: (ab)*+ followed by cd — disjoint
  expectMatchAllEngines<R"((ab)*+cd)">("cd", true);
  expectMatchAllEngines<R"((ab)*+cd)">("abcd", true);
  expectMatchAllEngines<R"((ab)*+cd)">("ababcd", true);

  // 2-char literal inner: (ab)++ followed by ab — overlapping, plus
  expectMatchAllEngines<R"((ab)++ab)">("ab", false);
  expectMatchAllEngines<R"((ab)++ab)">("abab", false);

  // 3-char counted inner: (abc){1,3}+ followed by abc — overlapping
  expectMatchAllEngines<R"((abc){1,3}+abc)">("abc", false);
  expectMatchAllEngines<R"((abc){1,3}+abc)">("abcabc", false);
  expectMatchAllEngines<R"((abc){1,3}+abc)">("abcabcabc", false);
  expectMatchAllEngines<R"((abc){1,3}+abc)">("abcabcabcabc", true);

  // 3-char counted inner: (abc){1,3}+ followed by def — disjoint
  expectMatchAllEngines<R"((abc){1,3}+def)">("abcdef", true);
  expectMatchAllEngines<R"((abc){1,3}+def)">("abcabcabcdef", true);

  // Mixed char inner: ([a-z]\d)*+ followed by [a-z]\d — overlapping
  expectMatchAllEngines<R"(([a-z]\d)*+[a-z]\d)">("a1", false);
  expectMatchAllEngines<R"(([a-z]\d)*+[a-z]\d)">("a1b2", false);
  expectMatchAllEngines<R"(([a-z]\d)*+[a-z]\d)">("a1b2c3", false);

  // Mixed char inner: ([a-z]\d)++ followed by pure digits — disjoint
  expectMatchAllEngines<R"(([a-z]\d)++\d+)">("a1b23", true);
}

TEST(RegexCrossEngineTest, PossessiveComplexInner) {
  // (?:ab)++c — possessive repeat of "ab", then c
  expectMatchAllEngines<"(?:ab)++c">("ababababc", true);
  expectMatchAllEngines<"(?:ab)++c">("abababab", false);

  // (?:ab)++ab — possessive consumes all "ab" pairs, can't give back
  expectMatchAllEngines<"(?:ab)++ab">("ababab", false);
}

TEST(RegexCrossEngineTest, PossessiveNoStackOverflow) {
  // Multi-char possessive with long input — iterative version uses O(1)
  // stack frames.
  std::string longInput;
  for (int i = 0; i < 10000; ++i) {
    longInput += "ab";
  }
  longInput += "c";
  constexpr auto re = compile<"(?:ab)++c">();
  EXPECT_TRUE(re.match(longInput));
}

// ===== Cross-Engine: Atomic Group Tests =====

TEST(RegexCrossEngineTest, AtomicGroupBasic) {
  // (?>a+)b — atomic consumes all a's, b matches after
  expectMatchAllEngines<"(?>a+)b">("aaab", true);
  expectMatchAllEngines<"(?>a+)b">("b", false);

  // (?>a+)a — atomic consumes all a's, trailing a can't match
  expectMatchAllEngines<"(?>a+)a">("aaa", false);
  expectMatchAllEngines<"(?>a+)a">("aa", false);
}

TEST(RegexCrossEngineTest, AtomicVsRegularGroup) {
  // Regular group backtracks — a+a matches "aa" (backtracks to 1+1)
  expectMatchAllEngines<"(?:a+)a">("aa", true);

  // Atomic group doesn't backtrack — a+a fails (consumed all, can't give back)
  expectMatchAllEngines<"(?>a+)a">("aa", false);
}

TEST(RegexCrossEngineTest, AtomicAlternation) {
  // (?>ab|a)b — atomic tries "ab" first, commits. trailing "b" needs to match.
  expectMatchAllEngines<"(?>ab|a)b">(
      "ab", false); // "ab" matches, trailing "b" fails
  expectMatchAllEngines<"(?>ab|a)b">(
      "abb", true); // "ab" matches, trailing "b" matches
  expectMatchAllEngines<"(?>ab|a)b">("abc", false);
}

TEST(RegexCrossEngineTest, NestedAtomicGroups) {
  // (?>a(?>b|c)d) — inner atomic commits b vs c
  expectMatchAllEngines<"(?>a(?>b|c)d)">("abd", true);
  expectMatchAllEngines<"(?>a(?>b|c)d)">("acd", true);
  expectMatchAllEngines<"(?>a(?>b|c)d)">("abcd", false);
}
