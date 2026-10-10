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
#include <folly/regex/test/CrossEngineTestHelpers.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// ===== Lookaround NFA Compatibility =====

TEST(LookaroundProbe, NfaCompatible) {
  static_assert(Regex<R"(\w+(?=@))">::parsed_.nfa_compatible);
  static_assert(Regex<R"(\d+(?=px))">::parsed_.nfa_compatible);
  static_assert(Regex<R"(a(?=b))">::parsed_.nfa_compatible);
  static_assert(Regex<R"(a(?!b))">::parsed_.nfa_compatible);
  static_assert(Regex<R"((?<=a)b)">::parsed_.nfa_compatible);
  static_assert(Regex<R"((?<!a)b)">::parsed_.nfa_compatible);
}

TEST(LookaroundProbe, ProbeInfrastructure) {
  using R = Regex<R"(\d+(?=px))", Flags::ForceNFA>;
  EXPECT_TRUE(R::parsed_.nfa_compatible);
  EXPECT_GT(R::parsed_.probe_count, 0);
  EXPECT_GT(R::probeStore_.probe_count, 0);
  EXPECT_TRUE(R::probeStore_.hasProbe(0));
  EXPECT_GE(R::probeStore_.probes[0].root, 0);
  EXPECT_TRUE(R::nfaProg_.has_lookaround_probes);
  EXPECT_GT(R::nfaProg_.lookaround_probe_count, 0);
  EXPECT_GE(R::nfaProg_.lookaround_probe_start[0], 0);

  // Check DFA probe states
  using DH = detail::DfaHolder<R"(\d+(?=px))", Flags::None>;
  EXPECT_TRUE(DH::dfaProg_.valid);
  EXPECT_TRUE(DH::dfaProg_.has_lookaround_probes);
  EXPECT_GT(DH::dfaProg_.probe_state_count, 0)
      << "DFA should have probe states";
}

// ===== Cross-Engine Lookahead Tests =====

TEST(LookaroundProbe, PositiveLookaheadSimple) {
  expectSearchAllEngines<R"(\d+(?=px))">("100px", true, "100");
  expectSearchAllEngines<R"(\d+(?=px))">("100em", false);
  expectSearchAllEngines<R"(\d+(?=px))">("px", false);
}

TEST(LookaroundProbe, PositiveLookaheadWordAtSign) {
  expectSearchAllEngines<R"(\w+(?=@))">("user@host", true, "user");
  expectSearchAllEngines<R"(\w+(?=@))">("no at sign", false);
}

TEST(LookaroundProbe, NegativeLookaheadSimple) {
  expectSearchAllEngines<R"(\d+(?!px))">("100em", true, "100");
  expectSearchAllEngines<R"(\d+(?!px))">("100", true, "100");
}

TEST(LookaroundProbe, NegativeLookaheadFilter) {
  expectMatchAllEngines<R"(foo(?!bar).*)">("foobar", false);
  expectMatchAllEngines<R"(foo(?!bar).*)">("foobaz", true);
  expectMatchAllEngines<R"(foo(?!bar).*)">("foo", true);
}

TEST(LookaroundProbe, NegativeLookaheadNonDigit) {
  expectSearchAllEngines<R"(\d+(?!\d))">("abc 123 def", true, "123");
  expectSearchAllEngines<R"(\d+(?!\d))">("abc", false);
}

TEST(LookaroundProbe, PositiveLookaheadMatch) {
  expectMatchAllEngines<R"(a(?=b)b)">("ab", true);
  expectMatchAllEngines<R"(a(?=b)b)">("ac", false);
  expectMatchAllEngines<R"(a(?=b)b)">("a", false);
}

TEST(LookaroundProbe, NegativeLookaheadMatch) {
  expectMatchAllEngines<R"(a(?!b)\C)">("ac", true);
  expectMatchAllEngines<R"(a(?!b)\C)">("ab", false);
  expectMatchAllEngines<R"(a(?!b)\C)">("ad", true);
}

// ===== Cross-Engine Lookbehind Tests =====

TEST(LookaroundProbe, PositiveLookbehindSimple) {
  expectSearchAllEngines<R"((?<=\$)\d+)">("$100", true, "100");
  expectSearchAllEngines<R"((?<=\$)\d+)">("100", false);
  expectSearchAllEngines<R"((?<=\$)\d+)">("€100", false);
}

TEST(LookaroundProbe, PositiveLookbehindAtSign) {
  expectSearchAllEngines<R"((?<=@)\w+)">("user@host", true, "host");
  expectSearchAllEngines<R"((?<=@)\w+)">("no at sign", false);
}

TEST(LookaroundProbe, NegativeLookbehindSimple) {
  expectSearchAllEngines<R"((?<!\$)\d+)">("100", true, "100");
  expectSearchAllEngines<R"((?<!\$)\d+)">("x100", true, "100");
}

TEST(LookaroundProbe, NegativeLookbehindDigit) {
  expectSearchAllEngines<R"((?<!\d)\d+)">("abc 42 def", true, "42");
  expectSearchAllEngines<R"((?<!\d)\d+)">("abc", false);
}

TEST(LookaroundProbe, PositiveLookbehindMatch) {
  expectMatchAllEngines<R"((?<=a)b)">("b", false);
  expectSearchAllEngines<R"((?<=a)b)">("ab", true, "b");
  expectSearchAllEngines<R"((?<=a)b)">("cb", false);
}

TEST(LookaroundProbe, NegativeLookbehindMatch) {
  expectSearchAllEngines<R"((?<!a)b)">("cb", true, "b");
  expectSearchAllEngines<R"((?<!a)b)">("ab", false);
  expectSearchAllEngines<R"((?<!a)b)">("b", true, "b");
}

TEST(LookaroundProbe, LookbehindMultiChar) {
  expectSearchAllEngines<R"((?<=abc)d)">("abcd", true, "d");
  expectSearchAllEngines<R"((?<=abc)d)">("xbcd", false);
  expectSearchAllEngines<R"((?<=abc)d)">("d", false);
}

TEST(LookaroundProbe, LookbehindCountedRepeat) {
  // (?<=\d{3}) — fixed-width lookbehind with counted repeat
  expectSearchAllEngines<R"((?<=\d{3})x)">("123x", true, "x");
  expectSearchAllEngines<R"((?<=\d{3})x)">("12x", false);
  expectSearchAllEngines<R"((?<=\d{3})x)">("abcx", false);
}

TEST(LookaroundProbe, NegLookbehindCharClass) {
  // (?<![A-Z]) — negative lookbehind with char class range
  expectSearchAllEngines<R"((?<![A-Z])\d+)">("a123", true, "123");
  expectSearchAllEngines<R"((?<![A-Z])\d+)">("123", true, "123");
}

// ===== Variable-Width Lookbehind Tests =====

TEST(LookaroundProbe, VariableWidthLookbehind) {
  // (?<=\d+)x — variable-width positive lookbehind
  expectSearchAllEngines<R"((?<=\d+)x)">("123x", true, "x");
  expectSearchAllEngines<R"((?<=\d+)x)">("abcx", false);
}

// ===== Regression Tests =====

TEST(LookaroundProbe, SlidingWindowRegression) {
  expectSearchAllEngines<R"([a-z]{2,}+\d)">("abcdef1", true, "abcdef1");
}

TEST(LookaroundProbe, PossessiveInsideLookahead) {
  // Possessive quantifier inside a lookahead
  expectMatchAllEngines<R"(a(?=b++c)bbc)">("abbc", true);
  expectMatchAllEngines<R"(a(?=b++c)bbc)">("abc", false);
}

TEST(LookaroundProbe, PossessiveInsideLookbehind) {
  // Possessive quantifier inside a fixed-width lookbehind
  expectSearchAllEngines<R"((?<=a{3})b)">("aaab", true, "b");
  expectSearchAllEngines<R"((?<=a{3})b)">("aab", false);
}

TEST(LookaroundProbe, Regression_LookaheadAfterBarrier) {
  // Full match must consume the entire input. `a+b(?=a)` consumes `aab` and
  // only asserts the final `a`, so whole-string match on `aaba` should fail.
  expectMatchAllEngines<"a+b(?=a)">("aaba", false);
  expectSearchAllEngines<"a+b(?=a)">("xxaabayy", true, "aab");
}

TEST(LookaroundProbe, Regression_NegLookaheadAfterBarrier) {
  // Same full-match rule: `a+b(?!a)` consumes `aab` and asserts the next char
  // is not `a`, but it does not consume the trailing `b` in `aabb`.
  expectMatchAllEngines<"a+b(?!a)">("aabb", false);
  expectSearchAllEngines<"a+b(?!a)">("xxaabbyy", true, "aab");
}

TEST(LookaroundProbe, Regression_BarrierThenLookbehind) {
  expectMatchAllEngines<"a*b+(?<=ab+)">("abbb", true);
  expectSearchAllEngines<"a*b+(?<=ab+)">("xxabbbyy", true, "abbb");
}

TEST(LookaroundProbe, Regression_BarrierThenNegLookbehind) {
  expectMatchAllEngines<"a*b+(?<!aab+)">("abbb", true);
  expectMatchAllEngines<"a*b+(?<!aab+)">("aabbb", false);
  expectSearchAllEngines<"a*b+(?<!aab+)">("xxabbbyy", true, "abbb");
}

TEST(LookaroundProbe, LookaheadAfterPossessive) {
  // Possessive repeat followed by lookahead
  expectMatchAllEngines<R"(a++(?=b)b)">("aab", true);
  expectMatchAllEngines<R"(a++(?=b)b)">("aac", false);
}
