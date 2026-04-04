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

#include <string>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly::regex;

// ===== Full Match Tests =====

TEST(RegexMatchTest, SimpleLiteral) {
  constexpr auto re = compile<"hello">();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("world"));
  EXPECT_FALSE(re.match("hello world"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexMatchTest, EmptyPattern) {
  constexpr auto re = compile<"">();
  EXPECT_TRUE(re.match(""));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexMatchTest, AnyChar) {
  constexpr auto re = compile<"a.b">();
  EXPECT_TRUE(re.match("aXb"));
  EXPECT_TRUE(re.match("a1b"));
  EXPECT_FALSE(re.match("ab"));
  EXPECT_FALSE(re.match("aXXb"));
}

TEST(RegexMatchTest, Anchors) {
  constexpr auto re = compile<"^hello$">();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("hello world"));
}

TEST(RegexMatchTest, AlternationMatch) {
  constexpr auto re = compile<"cat|dog">();
  EXPECT_TRUE(re.match("cat"));
  EXPECT_TRUE(re.match("dog"));
  EXPECT_FALSE(re.match("bird"));
}

TEST(RegexMatchTest, StarQuantifier) {
  constexpr auto re = compile<"ab*c">();
  EXPECT_TRUE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("abbc"));
  EXPECT_FALSE(re.match("adc"));
}

TEST(RegexMatchTest, PlusQuantifier) {
  constexpr auto re = compile<"ab+c">();
  EXPECT_FALSE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("abbc"));
}

TEST(RegexMatchTest, QuestionQuantifier) {
  constexpr auto re = compile<"ab?c">();
  EXPECT_TRUE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("abbc"));
}

TEST(RegexMatchTest, CountedRepetition) {
  constexpr auto re = compile<"a{2,4}">();
  EXPECT_FALSE(re.match("a"));
  EXPECT_TRUE(re.match("aa"));
  EXPECT_TRUE(re.match("aaa"));
  EXPECT_TRUE(re.match("aaaa"));
  EXPECT_FALSE(re.match("aaaaa"));
}

TEST(RegexMatchTest, ExactRepetition) {
  constexpr auto re = compile<"a{3}">();
  EXPECT_FALSE(re.match("aa"));
  EXPECT_TRUE(re.match("aaa"));
  EXPECT_FALSE(re.match("aaaa"));
}

TEST(RegexMatchTest, CharClassBasic) {
  constexpr auto re = compile<"[abc]">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("b"));
  EXPECT_TRUE(re.match("c"));
  EXPECT_FALSE(re.match("d"));
}

TEST(RegexMatchTest, CharClassRange) {
  constexpr auto re = compile<"[a-z]+">();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("Hello"));
  EXPECT_FALSE(re.match("123"));
}

TEST(RegexMatchTest, CharClassNegated) {
  constexpr auto re = compile<"[^0-9]+">();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("123"));
}

TEST(RegexMatchTest, CharClassDash) {
  constexpr auto re = compile<"[-a]">();
  EXPECT_TRUE(re.match("-"));
  EXPECT_TRUE(re.match("a"));
  EXPECT_FALSE(re.match("b"));
}

TEST(RegexMatchTest, ShorthandDigit) {
  constexpr auto re = compile<"\\d+">();
  EXPECT_TRUE(re.match("123"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexMatchTest, ShorthandWord) {
  constexpr auto re = compile<"\\w+">();
  EXPECT_TRUE(re.match("hello_123"));
  EXPECT_FALSE(re.match("hello world"));
}

TEST(RegexMatchTest, ShorthandSpace) {
  constexpr auto re = compile<"\\s+">();
  EXPECT_TRUE(re.match("  \t\n"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexMatchTest, NegatedShorthand) {
  constexpr auto re = compile<"\\D+">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("123"));
}

TEST(RegexMatchTest, EscapeSequences) {
  constexpr auto re = compile<"a\\tb">();
  EXPECT_TRUE(re.match("a\tb"));
  EXPECT_FALSE(re.match("ab"));
}

TEST(RegexMatchTest, EscapedSpecialChars) {
  constexpr auto re = compile<"a\\.b">();
  EXPECT_TRUE(re.match("a.b"));
  EXPECT_FALSE(re.match("axb"));
}

TEST(RegexMatchTest, NonCaptureGroup) {
  constexpr auto re = compile<"(?:ab)+">();
  EXPECT_TRUE(re.match("ab"));
  EXPECT_TRUE(re.match("abab"));
  EXPECT_FALSE(re.match("a"));
}

// ===== Search Tests =====

TEST(RegexSearchTest, FindInMiddle) {
  constexpr auto re = compile<"\\d+">();
  auto m = re.search("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123");
}

TEST(RegexSearchTest, FindAtStart) {
  constexpr auto re = compile<"hello">();
  auto m = re.search("hello world");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "hello");
}

TEST(RegexSearchTest, NoMatch) {
  constexpr auto re = compile<"\\d+">();
  auto m = re.search("no digits here");
  EXPECT_FALSE(m);
}

TEST(RegexSearchTest, AnchoredSearch) {
  constexpr auto re = compile<"^hello">();
  EXPECT_TRUE(re.search("hello world"));
  EXPECT_FALSE(re.search("say hello"));
}

// ===== Capture Group Tests =====

TEST(RegexCaptureTest, SingleGroup) {
  constexpr auto re = compile<"(\\d+)">();
  auto m = re.search("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
}

TEST(RegexCaptureTest, MultipleGroups) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto m = re.match("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123-hello");
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexCaptureTest, NestedGroups) {
  constexpr auto re = compile<"((\\d+)-(\\w+))">();
  auto m = re.match("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123-hello");
  EXPECT_EQ(m[2], "123");
  EXPECT_EQ(m[3], "hello");
}

// ===== Structured Bindings Tests =====

TEST(RegexStructuredBindingsTest, BasicDestructuring) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("123-hello");
  EXPECT_EQ(full, "123-hello");
  EXPECT_EQ(num, "123");
  EXPECT_EQ(word, "hello");
}

TEST(RegexStructuredBindingsTest, FailedMatch) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("no-match-here!!!");
  EXPECT_TRUE(full.empty());
  EXPECT_TRUE(num.empty());
  EXPECT_TRUE(word.empty());
}

TEST(RegexStructuredBindingsTest, SingleGroup) {
  constexpr auto re = compile<"(\\w+)">();
  auto [full, word] = re.match("hello");
  EXPECT_EQ(full, "hello");
  EXPECT_EQ(word, "hello");
}

TEST(RegexStructuredBindingsTest, ZeroGroups) {
  constexpr auto re = compile<"hello">();
  auto [full] = re.match("hello");
  EXPECT_EQ(full, "hello");
}

TEST(RegexStructuredBindingsTest, WithSearch) {
  constexpr auto re = compile<"(\\d+)">();
  auto [full, digits] = re.search("abc 42 def");
  EXPECT_EQ(full, "42");
  EXPECT_EQ(digits, "42");
}

TEST(RegexStructuredBindingsTest, DateParsing) {
  constexpr auto re = compile<"(\\d{4})-(\\d{2})-(\\d{2})">();
  auto [full, year, month, day] = re.match("2026-03-23");
  EXPECT_EQ(year, "2026");
  EXPECT_EQ(month, "03");
  EXPECT_EQ(day, "23");
}

// ===== Freestanding Function Tests =====

TEST(RegexFreestandingTest, Match) {
  auto m = folly::regex::match<"(\\d+)-(\\w+)">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexFreestandingTest, Search) {
  auto m = folly::regex::search<"\\d+">("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexFreestandingTest, Test) {
  EXPECT_TRUE(folly::regex::test<"\\d+">("abc 42 def"));
  EXPECT_FALSE(folly::regex::test<"\\d+">("no digits"));
}

TEST(RegexFreestandingTest, WithFlags) {
  auto m = folly::regex::match<"hello", Flags::None>("hello");
  EXPECT_TRUE(m);
}

TEST(RegexFreestandingTest, StructuredBindings) {
  auto [full, num] = folly::regex::match<"(\\d+)">("42");
  EXPECT_EQ(num, "42");
}

// ===== MatchAll Tests =====

TEST(RegexMatchAllTest, MultipleMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("12 abc 34 def 56")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"12", "34", "56"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexMatchAllTest, NoMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("no digits")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_TRUE(matches.empty());
}

TEST(RegexMatchAllTest, SingleMatch) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abc42def")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"42"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexMatchAllTest, FreestandingMatchAll) {
  std::vector<std::string> matches;
  for (auto m : folly::regex::matchAll<"\\d+">("1 2 3")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"1", "2", "3"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexMatchAllTest, WithCaptures) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  std::vector<std::string> firsts, seconds;
  for (auto m : re.matchAll("12-ab 34-cd")) {
    firsts.emplace_back(m[1]);
    seconds.emplace_back(m[2]);
  }
  std::vector<std::string> expectedFirsts{"12", "34"};
  std::vector<std::string> expectedSeconds{"ab", "cd"};
  EXPECT_EQ(firsts, expectedFirsts);
  EXPECT_EQ(seconds, expectedSeconds);
}

// ===== Test Function Tests =====

TEST(RegexTestTest, BasicTest) {
  constexpr auto re = compile<"\\d+">();
  EXPECT_TRUE(re.test("abc 123 def"));
  EXPECT_FALSE(re.test("no digits"));
}

// ===== Quantifier Edge Cases =====

TEST(RegexQuantifierTest, LazyStarVsGreedy) {
  constexpr auto greedy = compile<"a.*b">();
  constexpr auto lazy = compile<"a.*?b">();

  EXPECT_TRUE(greedy.match("aXXbYYb"));
  EXPECT_TRUE(lazy.match("aXXbYYb"));
}

TEST(RegexQuantifierTest, OpenEndedRepetition) {
  constexpr auto re = compile<"a{2,}">();
  EXPECT_FALSE(re.match("a"));
  EXPECT_TRUE(re.match("aa"));
  EXPECT_TRUE(re.match("aaa"));
  EXPECT_TRUE(re.match("aaaaaaa"));
}

// ===== Execution Mode Tests =====

TEST(RegexExecutionModeTest, ForceBacktracking) {
  constexpr auto re = compile<"(\\d+)-(\\w+)", Flags::ForceBacktracking>();
  auto m = re.match("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexExecutionModeTest, ForceNFA) {
  constexpr auto re = compile<"(\\d+)-(\\w+)", Flags::ForceNFA>();
  auto m = re.match("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexExecutionModeTest, AutoModeFallback) {
  // Pattern that could cause excessive backtracking
  constexpr auto re = compile<"(a*)*b">();
  // With auto mode, this should complete in bounded time due to NFA fallback
  EXPECT_FALSE(re.match("aaaaaaaaaaaaaaaa"));
  EXPECT_TRUE(re.match("aaab"));
}

TEST(RegexExecutionModeTest, NestedQuantifierOptimized) {
  // After nested quantifier flattening, (a+)+b becomes backtrack-safe
  // and runs via the fast backtracking path
  constexpr auto re = compile<"(a+)+b">();
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_FALSE(re.match("aaaaaaaaaaaaaaaa"));
  auto m = re.match("aaab");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "aaab");
  EXPECT_EQ(m[1], "aaa");

  auto s = re.search("xaaab");
  EXPECT_TRUE(s);
  EXPECT_EQ(s[0], "aaab");
  EXPECT_EQ(s[1], "aaa");

  // Edge case: single char before b
  auto m2 = re.match("ab");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "a");

  // All three modes agree on result
  constexpr auto reBT = compile<"(a+)+b", Flags::ForceBacktracking>();
  constexpr auto reNFA = compile<"(a+)+b", Flags::ForceNFA>();
  auto mBT = reBT.match("aaab");
  auto mNFA = reNFA.match("aaab");
  EXPECT_TRUE(mBT);
  EXPECT_TRUE(mNFA);
  EXPECT_EQ(mBT[1], "aaa");
  EXPECT_EQ(mNFA[1], "aaa");
}

TEST(RegexExecutionModeTest, ModesAgree) {
  constexpr auto bt = compile<"(\\d+)\\.(\\d+)", Flags::ForceBacktracking>();
  constexpr auto nfa = compile<"(\\d+)\\.(\\d+)", Flags::ForceNFA>();
  constexpr auto autoMode = compile<"(\\d+)\\.(\\d+)">();

  auto m1 = bt.match("123.456");
  auto m2 = nfa.match("123.456");
  auto m3 = autoMode.match("123.456");

  EXPECT_TRUE(m1);
  EXPECT_TRUE(m2);
  EXPECT_TRUE(m3);
  EXPECT_EQ(m1[1], m2[1]);
  EXPECT_EQ(m1[2], m2[2]);
  EXPECT_EQ(m1[1], m3[1]);
  EXPECT_EQ(m1[2], m3[2]);
}

TEST(RegexExecutionModeTest, ForceNFASearch) {
  constexpr auto re = compile<"\\d+", Flags::ForceNFA>();
  auto m = re.search("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexExecutionModeTest, ForceBacktrackingSearch) {
  constexpr auto re = compile<"\\d+", Flags::ForceBacktracking>();
  auto m = re.search("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexExecutionModeTest, ForceNFATest) {
  constexpr auto re = compile<"\\d+", Flags::ForceNFA>();
  EXPECT_TRUE(re.test("abc 42 def"));
  EXPECT_FALSE(re.test("no digits"));
}

// ===== Edge Cases =====

TEST(RegexEdgeCaseTest, EmptyInput) {
  constexpr auto re = compile<"a*">();
  EXPECT_TRUE(re.match(""));
}

TEST(RegexEdgeCaseTest, SingleCharPattern) {
  constexpr auto re = compile<"a">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_FALSE(re.match("b"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexEdgeCaseTest, AlternationWithEmpty) {
  constexpr auto re = compile<"a|">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match(""));
}

// ===== UDL Tests =====

TEST(RegexUDLTest, BasicUDL) {
  using namespace folly::regex::literals;
  constexpr auto re = "\\d+"_re;
  EXPECT_TRUE(re.match("123"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexUDLTest, UDLSearch) {
  using namespace folly::regex::literals;
  constexpr auto re = "(\\w+)@(\\w+)"_re;
  auto m = re.search("contact user@host please");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "user");
  EXPECT_EQ(m[2], "host");
}

// ===== Lookahead Tests =====

TEST(RegexLookaheadTest, PositiveLookahead) {
  constexpr auto re = compile<"\\w+(?=@)">();
  auto m = re.search("user@host");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "user");
}

TEST(RegexLookaheadTest, PositiveLookaheadNoMatch) {
  constexpr auto re = compile<"\\w+(?=@)">();
  auto m = re.search("no at sign");
  EXPECT_FALSE(m);
}

TEST(RegexLookaheadTest, NegativeLookahead) {
  constexpr auto re = compile<"\\d+(?!\\d)">();
  auto m = re.search("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123");
}

TEST(RegexLookaheadTest, NegativeLookaheadFilter) {
  constexpr auto re = compile<"foo(?!bar).*">();
  EXPECT_FALSE(re.match("foobar"));
  EXPECT_TRUE(re.match("foobaz"));
}

// ===== Lookbehind Tests =====

TEST(RegexLookbehindTest, PositiveLookbehind) {
  constexpr auto re = compile<"(?<=@)\\w+">();
  auto m = re.search("user@host");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "host");
}

TEST(RegexLookbehindTest, PositiveLookbehindNoMatch) {
  constexpr auto re = compile<"(?<=@)\\w+">();
  auto m = re.search("no at sign");
  EXPECT_FALSE(m);
}

TEST(RegexLookbehindTest, NegativeLookbehind) {
  constexpr auto re = compile<"(?<!\\d)\\d+">();
  auto m = re.search("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexLookbehindTest, MultiCharLookbehind) {
  constexpr auto re = compile<"(?<=abc)def">();
  EXPECT_TRUE(re.search("abcdef"));
  EXPECT_FALSE(re.search("xxdef"));
}

// ===== Backreference Tests =====

TEST(RegexBackrefTest, SimpleBackref) {
  constexpr auto re = compile<"(\\w+) \\1">();
  EXPECT_TRUE(re.match("hello hello"));
  EXPECT_FALSE(re.match("hello world"));
}

TEST(RegexBackrefTest, BackrefCapture) {
  constexpr auto re = compile<"(\\w+) \\1">();
  auto m = re.match("abc abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexBackrefTest, BackrefSearch) {
  constexpr auto re = compile<"(\\w+) \\1">();
  auto m = re.search("say hello hello there");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "hello hello");
  EXPECT_EQ(m[1], "hello");
}

TEST(RegexBackrefTest, BackrefNoMatch) {
  constexpr auto re = compile<"(a)\\1">();
  EXPECT_FALSE(re.match("ab"));
  EXPECT_TRUE(re.match("aa"));
}

TEST(RegexBackrefTest, MultipleBackrefs) {
  constexpr auto re = compile<"(\\w)(\\w) \\2\\1">();
  EXPECT_TRUE(re.match("ab ba"));
  EXPECT_FALSE(re.match("ab ab"));
}

// ===== DFA Tests (ForceDFA flag) =====

TEST(RegexDfaTest, SimpleLiteral) {
  constexpr auto re = compile<"hello", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("world"));
  EXPECT_FALSE(re.match("hello world"));
}

TEST(RegexDfaTest, EmptyPattern) {
  constexpr auto re = compile<"", Flags::ForceDFA>();
  EXPECT_TRUE(re.match(""));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexDfaTest, AnyChar) {
  constexpr auto re = compile<"a.b", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("aXb"));
  EXPECT_TRUE(re.match("a1b"));
  EXPECT_FALSE(re.match("ab"));
  EXPECT_FALSE(re.match("aXXb"));
}

TEST(RegexDfaTest, Alternation) {
  constexpr auto re = compile<"cat|dog", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("cat"));
  EXPECT_TRUE(re.match("dog"));
  EXPECT_FALSE(re.match("bird"));
  EXPECT_FALSE(re.match("cats"));
}

TEST(RegexDfaTest, CharClass) {
  constexpr auto re = compile<"[abc]+", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("aaa"));
  EXPECT_FALSE(re.match("xyz"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexDfaTest, Repetition) {
  constexpr auto re = compile<"ab*c", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("abbc"));
  EXPECT_FALSE(re.match("abbd"));
}

TEST(RegexDfaTest, AnchorBegin) {
  constexpr auto re = compile<"^hello", Flags::ForceDFA>();
  EXPECT_TRUE(re.search("hello world"));
  EXPECT_FALSE(re.search("say hello"));
}

TEST(RegexDfaTest, AnchorEnd) {
  constexpr auto re = compile<"world$", Flags::ForceDFA>();
  EXPECT_TRUE(re.search("hello world"));
  EXPECT_FALSE(re.search("world hello"));
}

TEST(RegexDfaTest, AnchorBoth) {
  constexpr auto re = compile<"^exact$", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("exact"));
  EXPECT_FALSE(re.match("not exact"));
  EXPECT_FALSE(re.search("not exact either"));
}

TEST(RegexDfaTest, SearchSimple) {
  constexpr auto re = compile<"world", Flags::ForceDFA>();
  auto m = re.search("hello world");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "world");
}

TEST(RegexDfaTest, SearchNotFound) {
  constexpr auto re = compile<"xyz", Flags::ForceDFA>();
  EXPECT_FALSE(re.search("hello world"));
}

TEST(RegexDfaTest, TestSimple) {
  constexpr auto re = compile<"\\d+", Flags::ForceDFA>();
  EXPECT_TRUE(re.test("abc123def"));
  EXPECT_FALSE(re.test("abcdef"));
}

TEST(RegexDfaTest, CaptureGroups) {
  constexpr auto re = compile<"(\\w+)@(\\w+)", Flags::ForceDFA>();
  auto m = re.search("email: user@host ok");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "user@host");
  EXPECT_EQ(m[1], "user");
  EXPECT_EQ(m[2], "host");
}

TEST(RegexDfaTest, DateCapture) {
  constexpr auto re = compile<"(\\d{4})-(\\d{2})-(\\d{2})", Flags::ForceDFA>();
  auto m = re.search("date: 2024-01-15 ok");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "2024-01-15");
  EXPECT_EQ(m[1], "2024");
  EXPECT_EQ(m[2], "01");
  EXPECT_EQ(m[3], "15");
}

TEST(RegexDfaTest, MatchAnchored) {
  constexpr auto re = compile<"(a+)(b+)", Flags::ForceDFA>();
  auto m = re.match("aaabbb");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "aaabbb");
  EXPECT_EQ(m[1], "aaa");
  EXPECT_EQ(m[2], "bbb");
}

TEST(RegexDfaTest, DigitClasses) {
  constexpr auto re = compile<"\\d\\d:\\d\\d", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("12:34"));
  EXPECT_FALSE(re.match("ab:cd"));
  EXPECT_TRUE(re.search("time is 09:30 now"));
}

TEST(RegexDfaTest, EmptyMatch) {
  constexpr auto re = compile<"a*", Flags::ForceDFA>();
  EXPECT_TRUE(re.match(""));
  EXPECT_TRUE(re.match("aaa"));
}

TEST(RegexDfaTest, QuantifierPlus) {
  constexpr auto re = compile<"a+", Flags::ForceDFA>();
  EXPECT_FALSE(re.match(""));
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("aaa"));
}

TEST(RegexDfaTest, QuantifierQuestion) {
  constexpr auto re = compile<"ab?c", Flags::ForceDFA>();
  EXPECT_TRUE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("abbc"));
}

// ===== AST Optimization Tests =====

TEST(RegexOptimizationTest, LiteralPrefixStrippingMatch) {
  // Full literal pattern — prefix is stripped entirely
  constexpr auto re = compile<"hello">();
  EXPECT_TRUE(re.match("hello"));
  EXPECT_FALSE(re.match("hell"));
  EXPECT_FALSE(re.match("helloo"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexOptimizationTest, LiteralPrefixStrippingSearch) {
  // Search for a fully-literal pattern
  constexpr auto re = compile<"world">();
  auto m = re.search("hello world");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "world");
}

TEST(RegexOptimizationTest, LiteralPrefixSearchNotFound) {
  constexpr auto re = compile<"xyz">();
  EXPECT_FALSE(re.search("hello world"));
}

TEST(RegexOptimizationTest, LiteralPrefixSearchAtStart) {
  constexpr auto re = compile<"hello">();
  auto m = re.search("hello world");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "hello");
}

TEST(RegexOptimizationTest, PrefixWithRepeat) {
  // Pattern with prefix followed by repeat — prefix should not extract
  // through Repeat, so the AST stays intact
  constexpr auto re = compile<"a+">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("aaa"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexOptimizationTest, LiteralPrefixWithSuffix) {
  // Pattern: literal prefix + non-literal + literal suffix
  constexpr auto re = compile<"abc.*xyz">();
  EXPECT_TRUE(re.match("abcxyz"));
  EXPECT_TRUE(re.match("abc123xyz"));
  EXPECT_FALSE(re.match("abcxy"));
  EXPECT_FALSE(re.match("bc123xyz"));
}

TEST(RegexOptimizationTest, SuffixFastRejectMatch) {
  // Suffix mismatch should reject early
  constexpr auto re = compile<"abc">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("abd"));
  EXPECT_FALSE(re.match("xbc"));
}

TEST(RegexOptimizationTest, PrefixWithCapturingGroup) {
  // Prefix stripping should NOT extract through capturing groups
  constexpr auto re = compile<"(abc)def">();
  auto m = re.match("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdef");
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexOptimizationTest, PrefixStrippedWithCaptures) {
  // Literal prefix stripped, engine matches remainder with captures
  constexpr auto re = compile<"hello(world)">();
  auto m = re.match("helloworld");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "world");
}

TEST(RegexOptimizationTest, SearchWithPrefixAndCaptures) {
  constexpr auto re = compile<"foo(bar)">();
  auto m = re.search("xxxfoobaryyyy");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "foobar");
  EXPECT_EQ(m[1], "bar");
}

TEST(RegexOptimizationTest, TestWithPrefix) {
  constexpr auto re = compile<"hello">();
  EXPECT_TRUE(re.test("say hello there"));
  EXPECT_FALSE(re.test("no match here"));
}

// ===== Alternation Factoring Tests =====

TEST(RegexOptimizationTest, AlternationCommonPrefixMatch) {
  // (dev|devvm|devrs) should factor to dev(?:|vm|rs)
  constexpr auto re = compile<"dev|devvm|devrs">();
  EXPECT_TRUE(re.match("dev"));
  EXPECT_TRUE(re.match("devvm"));
  EXPECT_TRUE(re.match("devrs"));
  EXPECT_FALSE(re.match("de"));
  EXPECT_FALSE(re.match("devx"));
}

TEST(RegexOptimizationTest, AlternationPartialCommonPrefix) {
  // (dev|devvm|devrs|shellserver) -> (dev(?:|vm|rs)|shellserver)
  // Branches grouped by common prefix
  constexpr auto re = compile<"dev|devvm|devrs|shellserver">();
  EXPECT_TRUE(re.match("dev"));
  EXPECT_TRUE(re.match("devvm"));
  EXPECT_TRUE(re.match("devrs"));
  EXPECT_TRUE(re.match("shellserver"));
  EXPECT_FALSE(re.match("shell"));
  EXPECT_FALSE(re.match("devbig"));
}

TEST(RegexOptimizationTest, AlternationNoCommonPrefix) {
  // No common prefix — no factoring
  constexpr auto re = compile<"abc|def|ghi">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("def"));
  EXPECT_TRUE(re.match("ghi"));
  EXPECT_FALSE(re.match("abcdef"));
}

TEST(RegexOptimizationTest, AlternationSearchWithFactoring) {
  // Put longer alternatives first so backtracker picks the right one
  constexpr auto re = compile<"(devvm|devrs|devbig|devgpu|dev|shellserver)">();
  auto m = re.search("host=devvm.example.com");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "devvm");
}

TEST(RegexOptimizationTest, AlternationSearchMultipleBranches) {
  constexpr auto re = compile<"(devvm|devrs|devbig|devgpu|dev|shellserver)">();
  EXPECT_TRUE(re.search("shellserver.example.com"));
  EXPECT_FALSE(re.search("webserver.example.com"));
}

TEST(RegexOptimizationTest, AlternationWithCaptures) {
  // Capturing groups in alternation branches must be preserved
  constexpr auto re = compile<"(abc|abd)">();
  auto m = re.match("abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
  m = re.match("abd");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abd");
}

TEST(RegexOptimizationTest, AlternationNestedFactoring) {
  // (foobar|foobaz|fooqux) -> foo(bar|baz|qux) -> foo(ba(r|z)|qux)
  constexpr auto re = compile<"foobar|foobaz|fooqux">();
  EXPECT_TRUE(re.match("foobar"));
  EXPECT_TRUE(re.match("foobaz"));
  EXPECT_TRUE(re.match("fooqux"));
  EXPECT_FALSE(re.match("foo"));
  EXPECT_FALSE(re.match("fooby"));
}

TEST(RegexOptimizationTest, AlternationSingleCharBranches) {
  // Single-char branches with no common prefix
  constexpr auto re = compile<"a|b|c">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("b"));
  EXPECT_TRUE(re.match("c"));
  EXPECT_FALSE(re.match("d"));
}

TEST(RegexOptimizationTest, PrefixWithAnchor) {
  // Pattern with anchor: prefix should still be stripped
  constexpr auto re = compile<"world$">();
  EXPECT_TRUE(re.search("hello world"));
  EXPECT_FALSE(re.search("world hello"));
}

TEST(RegexOptimizationTest, PrefixIterator) {
  // Ensure the match iterator works correctly with prefix stripping
  constexpr auto re = compile<"hello">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("hello hello hello")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_EQ(matches.size(), 3u);
  for (const auto& m : matches) {
    EXPECT_EQ(m, "hello");
  }
}

// --- Non-capturing group flattening ---

TEST(RegexOptimizationTest, GroupFlatteningSingle) {
  // (?:(?:a)) should match the same as just "a"
  constexpr auto re = compile<"(?:(?:a))">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_FALSE(re.match("b"));
  EXPECT_FALSE(re.match("aa"));
}

TEST(RegexOptimizationTest, GroupFlatteningNested) {
  // Deeply nested non-capturing groups
  constexpr auto re = compile<"(?:(?:(?:abc)))">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("ab"));
  EXPECT_FALSE(re.match("abcd"));
}

TEST(RegexOptimizationTest, GroupFlatteningPreservesCapturing) {
  // Capturing groups must not be flattened
  constexpr auto re = compile<"(?:(a))">();
  EXPECT_TRUE(re.match("a"));
  auto m = re.match("a");
  EXPECT_EQ(m[1], "a");
}

// --- Trivial repeat simplification ---

TEST(RegexOptimizationTest, RepeatExactlyOne) {
  // a{1} is equivalent to a
  constexpr auto re = compile<"a{1}">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_FALSE(re.match("aa"));
  EXPECT_FALSE(re.match(""));
}

TEST(RegexOptimizationTest, RepeatZeroTimes) {
  // a{0,0} matches only the empty string (epsilon)
  constexpr auto re = compile<"a{0,0}">();
  EXPECT_TRUE(re.match(""));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexOptimizationTest, RepeatOneInSequence) {
  // ab{1}c is equivalent to abc
  constexpr auto re = compile<"ab{1}c">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("abbc"));
  EXPECT_FALSE(re.match("ac"));
}

// --- Duplicate branch elimination ---

TEST(RegexOptimizationTest, DuplicateBranchElimination) {
  // (foo|foo|bar) should behave like (foo|bar)
  constexpr auto re = compile<"foo|foo|bar">();
  EXPECT_TRUE(re.match("foo"));
  EXPECT_TRUE(re.match("bar"));
  EXPECT_FALSE(re.match("baz"));
}

TEST(RegexOptimizationTest, DuplicateBranchAllSame) {
  // (abc|abc|abc) should behave like abc
  constexpr auto re = compile<"abc|abc|abc">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("ab"));
  EXPECT_FALSE(re.match("abcd"));
}

// --- Single-char alternation -> CharClass merging ---

TEST(RegexOptimizationTest, SingleCharAlternationMerge) {
  // (a|b|c|d) should behave like [abcd]
  constexpr auto re = compile<"a|b|c|d">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("b"));
  EXPECT_TRUE(re.match("c"));
  EXPECT_TRUE(re.match("d"));
  EXPECT_FALSE(re.match("e"));
  EXPECT_FALSE(re.match("ab"));
}

TEST(RegexOptimizationTest, CharClassMergeInAlternation) {
  // [a-m]|[n-z] should match the full lowercase range
  constexpr auto re = compile<"[a-m]|[n-z]">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("m"));
  EXPECT_TRUE(re.match("n"));
  EXPECT_TRUE(re.match("z"));
  EXPECT_FALSE(re.match("A"));
  EXPECT_FALSE(re.match("0"));
}

TEST(RegexOptimizationTest, MixedCharMerge) {
  // Mix of single chars and char classes: a|[b-d]|e
  constexpr auto re = compile<"a|[b-d]|e">();
  EXPECT_TRUE(re.match("a"));
  EXPECT_TRUE(re.match("b"));
  EXPECT_TRUE(re.match("c"));
  EXPECT_TRUE(re.match("d"));
  EXPECT_TRUE(re.match("e"));
  EXPECT_FALSE(re.match("f"));
}

// --- Anchor hoisting ---

TEST(RegexOptimizationTest, AnchorHoistingBegin) {
  // (^foo|^bar) -> ^(foo|bar): only matches at start
  constexpr auto re = compile<"^foo|^bar">();
  EXPECT_TRUE(re.search("foo123"));
  EXPECT_TRUE(re.search("bar456"));
  EXPECT_FALSE(re.search("xfoo"));
  EXPECT_FALSE(re.search("xbar"));
}

TEST(RegexOptimizationTest, AnchorHoistingEnd) {
  // (foo$|bar$) -> (foo|bar)$: only matches at end
  constexpr auto re = compile<"foo$|bar$">();
  EXPECT_TRUE(re.search("123foo"));
  EXPECT_TRUE(re.search("456bar"));
  EXPECT_FALSE(re.search("foox"));
  EXPECT_FALSE(re.search("barx"));
}

TEST(RegexOptimizationTest, AnchorHoistingNoMix) {
  // (^foo|bar$) should NOT hoist — anchors differ
  constexpr auto re = compile<"^foo|bar$">();
  EXPECT_TRUE(re.search("foobar"));
  EXPECT_TRUE(re.search("xbar"));
  EXPECT_TRUE(re.search("foox"));
  EXPECT_FALSE(re.search("xbaz"));
}

// ===== Word Boundaries =====

TEST(RegexNewFeaturesTest, WordBoundaryBasic) {
  constexpr auto re = compile<R"(\bword\b)">();
  EXPECT_TRUE(re.search("a word here"));
  EXPECT_TRUE(re.search("word"));
  EXPECT_FALSE(re.search("sword"));
  EXPECT_FALSE(re.search("wordy"));
  EXPECT_FALSE(re.search("swordy"));
}

TEST(RegexNewFeaturesTest, WordBoundaryAtEdges) {
  constexpr auto re = compile<R"(\bfoo\b)">();
  EXPECT_TRUE(re.search("foo"));
  EXPECT_TRUE(re.search("foo bar"));
  EXPECT_TRUE(re.search("bar foo"));
  EXPECT_FALSE(re.search("foobar"));
}

TEST(RegexNewFeaturesTest, NegWordBoundary) {
  constexpr auto re = compile<R"(\Boo\B)">();
  EXPECT_TRUE(re.search("foobar"));
  EXPECT_FALSE(re.search("oo"));
}

// ===== String-Level Anchors =====

TEST(RegexNewFeaturesTest, StartOfStringAnchor) {
  constexpr auto re = compile<R"(\Afoo)">();
  EXPECT_TRUE(re.search("foobar"));
  EXPECT_FALSE(re.search("barfoo"));
}

TEST(RegexNewFeaturesTest, EndOfStringAnchor) {
  constexpr auto re = compile<R"(foo\z)">();
  EXPECT_TRUE(re.search("barfoo"));
  EXPECT_FALSE(re.search("foobar"));
}

TEST(RegexNewFeaturesTest, EndOfStringOrNewlineAnchor) {
  constexpr auto re = compile<R"(foo\Z)">();
  EXPECT_TRUE(re.search("barfoo"));
  EXPECT_TRUE(re.search("barfoo\n"));
  EXPECT_FALSE(re.search("foobar"));
}

// ===== AnyByte (\C) =====

TEST(RegexNewFeaturesTest, AnyByteMatchesNewline) {
  constexpr auto re = compile<R"(a\Cb)">();
  EXPECT_TRUE(re.match("a\nb"));
  EXPECT_TRUE(re.match("axb"));
}

TEST(RegexNewFeaturesTest, AnyByteVsDot) {
  // In this implementation AnyChar already matches \n,
  // so \C and . behave identically.
  constexpr auto reDot = compile<"a.b">();
  constexpr auto reByte = compile<R"(a\Cb)">();
  EXPECT_TRUE(reDot.match("a\nb"));
  EXPECT_TRUE(reByte.match("a\nb"));
}

// ===== Hex Escapes =====

TEST(RegexNewFeaturesTest, HexEscapeTwoDigit) {
  constexpr auto re = compile<R"(\x41)">();
  EXPECT_TRUE(re.match("A"));
  EXPECT_FALSE(re.match("B"));
}

TEST(RegexNewFeaturesTest, HexEscapeBraced) {
  constexpr auto re = compile<R"(\x{0A})">();
  EXPECT_TRUE(re.match("\n"));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexNewFeaturesTest, HexEscapeInCharClass) {
  constexpr auto re = compile<R"([\x41-\x5A]+)">();
  EXPECT_TRUE(re.match("ABC"));
  EXPECT_FALSE(re.match("abc"));
}

// ===== Octal Escapes =====

TEST(RegexNewFeaturesTest, OctalEscape) {
  constexpr auto re = compile<R"(\012)">();
  EXPECT_TRUE(re.match("\n"));
  EXPECT_FALSE(re.match("a"));
}

// ===== Bell Escape =====

TEST(RegexNewFeaturesTest, BellEscape) {
  constexpr auto re = compile<R"(\a)">();
  EXPECT_TRUE(re.match("\a"));
  EXPECT_FALSE(re.match("a"));
}

// ===== Possessive Quantifiers =====

TEST(RegexNewFeaturesTest, PossessiveStarNoBacktrack) {
  // a*+a should never match because possessive * consumes all a's
  constexpr auto re = compile<R"(a*+a)">();
  EXPECT_FALSE(re.match("aaa"));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexNewFeaturesTest, PossessivePlusNoBacktrack) {
  constexpr auto re = compile<R"(a++a)">();
  EXPECT_FALSE(re.match("aaa"));
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexNewFeaturesTest, PossessiveOptNoBacktrack) {
  constexpr auto re = compile<R"(a?+a)">();
  EXPECT_FALSE(re.match("a"));
}

TEST(RegexNewFeaturesTest, PossessiveStarSuccess) {
  constexpr auto re = compile<R"(a*+b)">();
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_TRUE(re.match("b"));
}

TEST(RegexNewFeaturesTest, PossessivePlusSuccess) {
  constexpr auto re = compile<R"(a++b)">();
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_FALSE(re.match("b"));
}

// ===== Shorthand in Char Classes =====

TEST(RegexNewFeaturesTest, ShorthandDigitInCharClass) {
  constexpr auto re = compile<R"([\d]+)">();
  EXPECT_TRUE(re.match("123"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexNewFeaturesTest, ShorthandWordInCharClass) {
  constexpr auto re = compile<R"([\w]+)">();
  EXPECT_TRUE(re.match("abc123_"));
  EXPECT_FALSE(re.match("!@#"));
}

TEST(RegexNewFeaturesTest, ShorthandSpaceInCharClass) {
  constexpr auto re = compile<R"([\s]+)">();
  EXPECT_TRUE(re.match(" \t\n"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexNewFeaturesTest, MixedShorthandInCharClass) {
  constexpr auto re = compile<R"([\d\s]+)">();
  EXPECT_TRUE(re.match("1 2 3"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexNewFeaturesTest, NegatedShorthandInCharClass) {
  constexpr auto re = compile<R"([\D]+)">();
  EXPECT_TRUE(re.match("abc"));
  EXPECT_FALSE(re.match("123"));
}

// ===== Multiline Flag =====

TEST(RegexNewFeaturesTest, MultilineBeginAnchor) {
  constexpr auto re = compile<"^line", Flags::Multiline>();
  EXPECT_TRUE(re.search("first\nline two"));
  EXPECT_TRUE(re.search("line one"));
}

TEST(RegexNewFeaturesTest, MultilineEndAnchor) {
  constexpr auto re = compile<"end$", Flags::Multiline>();
  EXPECT_TRUE(re.search("the end\nnext line"));
  EXPECT_TRUE(re.search("the end"));
}

TEST(RegexNewFeaturesTest, MultilineDoesNotAffectStringAnchors) {
  constexpr auto reA = compile<R"(\Afirst)", Flags::Multiline>();
  EXPECT_TRUE(reA.search("first\nsecond"));
  EXPECT_FALSE(reA.search("second\nfirst"));

  constexpr auto rez = compile<R"(last\z)", Flags::Multiline>();
  EXPECT_TRUE(rez.search("first\nlast"));
  EXPECT_FALSE(rez.search("last\nfirst"));
}

// ===== DotAll Flag =====

TEST(RegexNewFeaturesTest, DotAllDotMatchesNewline) {
  constexpr auto re = compile<"a.b", Flags::DotAll>();
  EXPECT_TRUE(re.match("a\nb"));
  EXPECT_TRUE(re.match("axb"));
}

TEST(RegexNewFeaturesTest, DotAllDefault) {
  // In this implementation AnyChar already matches \n,
  // so DotAll has no additional effect.
  constexpr auto re = compile<"a.b">();
  EXPECT_TRUE(re.match("a\nb"));
  EXPECT_TRUE(re.match("axb"));
}

// ===== Alternation-to-Optional Optimization =====

TEST(RegexExecutionModeTest, AlternationToOptionalCorrectness) {
  constexpr auto re = compile<"(a|ab)+c">();

  // Basic matches
  EXPECT_TRUE(re.match("ac"));
  EXPECT_TRUE(re.match("abc"));
  EXPECT_TRUE(re.match("aac"));
  EXPECT_TRUE(re.match("ababc"));
  EXPECT_TRUE(re.match("abac"));
  EXPECT_TRUE(re.match("aababc"));

  // Non-matches
  EXPECT_FALSE(re.match("c"));
  EXPECT_FALSE(re.match("ab"));
  EXPECT_FALSE(re.match(""));
  EXPECT_FALSE(re.match("bc"));

  // Search
  auto s = re.search("xababcx");
  EXPECT_TRUE(s);
  EXPECT_EQ(s[0], "ababc");

  // Verify all modes agree on match/no-match
  constexpr auto reBT = compile<"(a|ab)+c", Flags::ForceBacktracking>();
  constexpr auto reNFA = compile<"(a|ab)+c", Flags::ForceNFA>();
  EXPECT_TRUE(reBT.match("ababc"));
  EXPECT_TRUE(reNFA.match("ababc"));
  EXPECT_FALSE(reBT.match("c"));
  EXPECT_FALSE(reNFA.match("c"));

  // Verify equivalent optimized pattern produces same match results
  constexpr auto reOpt = compile<"(ab?)+c">();
  EXPECT_TRUE(reOpt.match("ac"));
  EXPECT_TRUE(reOpt.match("abc"));
  EXPECT_TRUE(reOpt.match("ababc"));
  EXPECT_FALSE(reOpt.match("c"));
  EXPECT_FALSE(reOpt.match("ab"));
}

// ===== Generalized Nested Quantifier Optimization =====

TEST(RegexExecutionModeTest, GeneralizedNestedQuantCorrectness) {
  // (?:a*)*b — after optimization becomes a*b
  constexpr auto re = compile<"(?:a*)*b">();
  EXPECT_TRUE(re.match("b"));
  EXPECT_TRUE(re.match("ab"));
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_FALSE(re.match("aaa"));
  EXPECT_FALSE(re.match(""));

  // (?:a+)*b — after optimization becomes a*b
  constexpr auto re2 = compile<"(?:a+)*b">();
  EXPECT_TRUE(re2.match("b"));
  EXPECT_TRUE(re2.match("ab"));
  EXPECT_TRUE(re2.match("aaab"));
  EXPECT_FALSE(re2.match("aaa"));

  // Verify modes agree
  constexpr auto reBT = compile<"(?:a*)*b", Flags::ForceBacktracking>();
  constexpr auto reNFA = compile<"(?:a*)*b", Flags::ForceNFA>();
  EXPECT_TRUE(reBT.match("aaab"));
  EXPECT_TRUE(reNFA.match("aaab"));
  EXPECT_FALSE(reBT.match("aaa"));
  EXPECT_FALSE(reNFA.match("aaa"));
}

// ===== Adjacent Repeat Merge Optimization =====

TEST(RegexExecutionModeTest, AdjacentRepeatMergeCorrectness) {
  // a+a+b — after merging becomes a{2,}b
  constexpr auto re = compile<"a+a+b">();
  EXPECT_FALSE(re.match("ab")); // needs at least 2 a's
  EXPECT_TRUE(re.match("aab"));
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_FALSE(re.match("b"));
  EXPECT_FALSE(re.match(""));

  // Verify modes agree
  constexpr auto reBT = compile<"a+a+b", Flags::ForceBacktracking>();
  constexpr auto reNFA = compile<"a+a+b", Flags::ForceNFA>();
  EXPECT_TRUE(reBT.match("aab"));
  EXPECT_TRUE(reNFA.match("aab"));
  EXPECT_FALSE(reBT.match("ab"));
  EXPECT_FALSE(reNFA.match("ab"));
}

// ===== Fixed-Length Sequence Repeat Optimization =====

TEST(RegexExecutionModeTest, FixedLengthSequenceCorrectness) {
  // (?:ab)+ — fixed-length sequence repeat
  constexpr auto re = compile<"(?:ab)+">();
  EXPECT_TRUE(re.match("ab"));
  EXPECT_TRUE(re.match("abab"));
  EXPECT_TRUE(re.match("ababab"));
  EXPECT_FALSE(re.match("a"));
  EXPECT_FALSE(re.match("aba")); // partial last iteration
  EXPECT_FALSE(re.match(""));

  // Search
  auto s = re.search("xababx");
  EXPECT_TRUE(s);
  EXPECT_EQ(s[0], "abab");
}

// ===== Single-Char CharClass to Literal Optimization =====

TEST(RegexExecutionModeTest, SingleCharClassToLiteralCorrectness) {
  // [a]+b — after converting [a]→a, becomes a+b
  constexpr auto re = compile<"[a]+b">();
  EXPECT_TRUE(re.match("ab"));
  EXPECT_TRUE(re.match("aaab"));
  EXPECT_FALSE(re.match("b"));
  EXPECT_FALSE(re.match(""));
}
