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

// ===== Cross-Engine: Core Match Tests =====

TEST(RegexCrossEngineTest, SimpleLiteral) {
  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("world", false);
  expectMatchAllEngines<"hello">("hello world", false);
  expectMatchAllEngines<"hello">("", false);
}

TEST(RegexCrossEngineTest, EmptyPattern) {
  expectMatchAllEngines<"">("", true);
  expectMatchAllEngines<"">("a", false);
}

TEST(RegexCrossEngineTest, AnyChar) {
  expectMatchAllEngines<"a.b">("aXb", true);
  expectMatchAllEngines<"a.b">("a1b", true);
  expectMatchAllEngines<"a.b">("ab", false);
  expectMatchAllEngines<"a.b">("aXXb", false);
}

TEST(RegexCrossEngineTest, Anchors) {
  expectMatchAllEngines<"^hello$">("hello", true);
  expectMatchAllEngines<"^hello$">("hello world", false);
}

TEST(RegexCrossEngineTest, Alternation) {
  expectMatchAllEngines<"cat|dog">("cat", true);
  expectMatchAllEngines<"cat|dog">("dog", true);
  expectMatchAllEngines<"cat|dog">("bird", false);
}

TEST(RegexCrossEngineTest, StarQuantifier) {
  expectMatchAllEngines<"ab*c">("ac", true);
  expectMatchAllEngines<"ab*c">("abc", true);
  expectMatchAllEngines<"ab*c">("abbc", true);
  expectMatchAllEngines<"ab*c">("adc", false);
}

TEST(RegexCrossEngineTest, PlusQuantifier) {
  expectMatchAllEngines<"ab+c">("ac", false);
  expectMatchAllEngines<"ab+c">("abc", true);
  expectMatchAllEngines<"ab+c">("abbc", true);
}

TEST(RegexCrossEngineTest, QuestionQuantifier) {
  expectMatchAllEngines<"ab?c">("ac", true);
  expectMatchAllEngines<"ab?c">("abc", true);
  expectMatchAllEngines<"ab?c">("abbc", false);
}

TEST(RegexCrossEngineTest, CountedRepetition) {
  expectMatchAllEngines<"a{2,4}">("a", false);
  expectMatchAllEngines<"a{2,4}">("aa", true);
  expectMatchAllEngines<"a{2,4}">("aaa", true);
  expectMatchAllEngines<"a{2,4}">("aaaa", true);
  expectMatchAllEngines<"a{2,4}">("aaaaa", false);
}

TEST(RegexCrossEngineTest, ExactRepetition) {
  expectMatchAllEngines<"a{3}">("aa", false);
  expectMatchAllEngines<"a{3}">("aaa", true);
  expectMatchAllEngines<"a{3}">("aaaa", false);
}

TEST(RegexCrossEngineTest, CharClassBasic) {
  expectMatchAllEngines<"[abc]">("a", true);
  expectMatchAllEngines<"[abc]">("b", true);
  expectMatchAllEngines<"[abc]">("c", true);
  expectMatchAllEngines<"[abc]">("d", false);
}

TEST(RegexCrossEngineTest, CharClassRange) {
  expectMatchAllEngines<"[a-z]+">("hello", true);
  expectMatchAllEngines<"[a-z]+">("Hello", false);
  expectMatchAllEngines<"[a-z]+">("123", false);
}

TEST(RegexCrossEngineTest, CharClassNegated) {
  expectMatchAllEngines<"[^0-9]+">("hello", true);
  expectMatchAllEngines<"[^0-9]+">("123", false);
}

TEST(RegexCrossEngineTest, CharClassDash) {
  expectMatchAllEngines<"[-a]">("-", true);
  expectMatchAllEngines<"[-a]">("a", true);
  expectMatchAllEngines<"[-a]">("b", false);
}

TEST(RegexCrossEngineTest, ShorthandDigit) {
  expectMatchAllEngines<"\\d+">("123", true);
  expectMatchAllEngines<"\\d+">("abc", false);
}

TEST(RegexCrossEngineTest, ShorthandWord) {
  expectMatchAllEngines<"\\w+">("hello_123", true);
  expectMatchAllEngines<"\\w+">("hello world", false);
}

TEST(RegexCrossEngineTest, ShorthandSpace) {
  expectMatchAllEngines<"\\s+">("  \t\n", true);
  expectMatchAllEngines<"\\s+">("abc", false);
}

TEST(RegexCrossEngineTest, NegatedShorthand) {
  expectMatchAllEngines<"\\D+">("abc", true);
  expectMatchAllEngines<"\\D+">("123", false);
}

TEST(RegexCrossEngineTest, EscapeSequences) {
  expectMatchAllEngines<"a\\tb">("a\tb", true);
  expectMatchAllEngines<"a\\tb">("ab", false);
}

TEST(RegexCrossEngineTest, EscapedSpecialChars) {
  expectMatchAllEngines<"a\\.b">("a.b", true);
  expectMatchAllEngines<"a\\.b">("axb", false);
}

TEST(RegexCrossEngineTest, NonCaptureGroup) {
  expectMatchAllEngines<"(?:ab)+">("ab", true);
  expectMatchAllEngines<"(?:ab)+">("abab", true);
  expectMatchAllEngines<"(?:ab)+">("a", false);
}

// ===== Cross-Engine: Core Search Tests =====

TEST(RegexCrossEngineTest, SearchFindInMiddle) {
  expectSearchAllEngines<"\\d+">("abc 123 def", true, "123");
}

TEST(RegexCrossEngineTest, SearchFindAtStart) {
  expectSearchAllEngines<"hello">("hello world", true, "hello");
}

TEST(RegexCrossEngineTest, SearchNoMatch) {
  expectSearchAllEngines<"\\d+">("no digits here", false);
}

TEST(RegexCrossEngineTest, SearchAnchored) {
  expectSearchAllEngines<"^hello">("hello world", true, "hello");
  expectSearchAllEngines<"^hello">("say hello", false);
}

// ===== Cross-Engine: Capture Group Tests =====

TEST(RegexCrossEngineTest, SingleGroupCapture) {
  auto m = expectSearchCapturesAgree<"(\\d+)">("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
}

TEST(RegexCrossEngineTest, MultipleGroupCapture) {
  auto m = expectMatchCapturesAgree<"(\\d+)-(\\w+)">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123-hello");
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexCrossEngineTest, NestedGroupCapture) {
  auto m = expectMatchCapturesAgree<"((\\d+)-(\\w+))">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123-hello");
  EXPECT_EQ(m[2], "123");
  EXPECT_EQ(m[3], "hello");
}

// ===== Cross-Engine: Test Function =====

TEST(RegexCrossEngineTest, TestFunction) {
  expectTestAllEngines<"\\d+">("abc 123 def", true);
  expectTestAllEngines<"\\d+">("no digits", false);
}

// ===== Cross-Engine: Quantifier Edge Cases =====

TEST(RegexCrossEngineTest, LazyStarVsGreedy) {
  expectMatchAllEngines<"a.*b">("aXXbYYb", true);
  expectMatchAllEngines<"a.*?b">("aXXbYYb", true);
}

TEST(RegexCrossEngineTest, OpenEndedRepetition) {
  expectMatchAllEngines<"a{2,}">("a", false);
  expectMatchAllEngines<"a{2,}">("aa", true);
  expectMatchAllEngines<"a{2,}">("aaa", true);
  expectMatchAllEngines<"a{2,}">("aaaaaaa", true);
}

// ===== Cross-Engine: Edge Cases =====

TEST(RegexCrossEngineTest, EmptyInput) {
  expectMatchAllEngines<"a*">("", true);
}

TEST(RegexCrossEngineTest, SingleCharPattern) {
  expectMatchAllEngines<"a">("a", true);
  expectMatchAllEngines<"a">("b", false);
  expectMatchAllEngines<"a">("", false);
}

TEST(RegexCrossEngineTest, AlternationWithEmpty) {
  expectMatchAllEngines<"a|">("a", true);
  expectMatchAllEngines<"a|">("", true);
}

// ===== Cross-Engine: String-Level Anchors =====

TEST(RegexCrossEngineTest, StartOfStringAnchor) {
  expectSearchAllEngines<R"(\Afoo)">("foobar", true, "foo");
  expectSearchAllEngines<R"(\Afoo)">("barfoo", false);
}

TEST(RegexCrossEngineTest, EndOfStringAnchor) {
  expectSearchAllEngines<R"(foo\z)">("barfoo", true, "foo");
  expectSearchAllEngines<R"(foo\z)">("foobar", false);
}

TEST(RegexCrossEngineTest, EndOfStringOrNewlineAnchor) {
  expectSearchAllEngines<R"(foo\Z)">("barfoo", true, "foo");
  expectSearchAllEngines<R"(foo\Z)">("barfoo\n", true, "foo");
  expectSearchAllEngines<R"(foo\Z)">("foobar", false);
}

// ===== Cross-Engine: AnyByte =====

TEST(RegexCrossEngineTest, AnyByteMatchesNewline) {
  expectMatchAllEngines<R"(a\Cb)">("a\nb", true);
  expectMatchAllEngines<R"(a\Cb)">("axb", true);
}

TEST(RegexCrossEngineTest, AnyByteVsDot) {
  expectMatchAllEngines<"a.b">("a\nb", false);
  expectMatchAllEngines<R"(a\Cb)">("a\nb", true);
}

// ===== Cross-Engine: Hex Escapes =====

TEST(RegexCrossEngineTest, HexEscapeTwoDigit) {
  expectMatchAllEngines<R"(\x41)">("A", true);
  expectMatchAllEngines<R"(\x41)">("B", false);
}

TEST(RegexCrossEngineTest, HexEscapeBraced) {
  expectMatchAllEngines<R"(\x{0A})">("\n", true);
  expectMatchAllEngines<R"(\x{0A})">("a", false);
}

TEST(RegexCrossEngineTest, HexEscapeInCharClass) {
  expectMatchAllEngines<R"([\x41-\x5A]+)">("ABC", true);
  expectMatchAllEngines<R"([\x41-\x5A]+)">("abc", false);
}

// ===== Cross-Engine: Octal/Bell Escapes =====

TEST(RegexCrossEngineTest, OctalEscape) {
  expectMatchAllEngines<R"(\012)">("\n", true);
  expectMatchAllEngines<R"(\012)">("a", false);
}

TEST(RegexCrossEngineTest, BellEscape) {
  expectMatchAllEngines<R"(\a)">("\a", true);
  expectMatchAllEngines<R"(\a)">("a", false);
}

// ===== Cross-Engine: Shorthand in Char Classes =====

TEST(RegexCrossEngineTest, ShorthandDigitInCharClass) {
  expectMatchAllEngines<R"([\d]+)">("123", true);
  expectMatchAllEngines<R"([\d]+)">("abc", false);
}

TEST(RegexCrossEngineTest, ShorthandWordInCharClass) {
  expectMatchAllEngines<R"([\w]+)">("abc123_", true);
  expectMatchAllEngines<R"([\w]+)">("!@#", false);
}

TEST(RegexCrossEngineTest, ShorthandSpaceInCharClass) {
  expectMatchAllEngines<R"([\s]+)">(" \t\n", true);
  expectMatchAllEngines<R"([\s]+)">("abc", false);
}

TEST(RegexCrossEngineTest, MixedShorthandInCharClass) {
  expectMatchAllEngines<R"([\d\s]+)">("1 2 3", true);
  expectMatchAllEngines<R"([\d\s]+)">("abc", false);
}

TEST(RegexCrossEngineTest, NegatedShorthandInCharClass) {
  expectMatchAllEngines<R"([\D]+)">("abc", true);
  expectMatchAllEngines<R"([\D]+)">("123", false);
}

TEST(RegexCrossEngineTest, NegatedCharClassComplement) {
  expectMatchAllEngines<"[^abc]">("d", true);
  expectMatchAllEngines<"[^abc]">("a", false);
  expectMatchAllEngines<"[^abc]">("b", false);
  expectMatchAllEngines<"[^a-z]">("1", true);
  expectMatchAllEngines<"[^a-z]">("a", false);
  expectSearchAllEngines<"[^a-z]+">("abc123def", true, "123");
}

TEST(RegexCrossEngineTest, NegatedShorthandComplement) {
  expectMatchAllEngines<"\\D">("a", true);
  expectMatchAllEngines<"\\D">("1", false);
  expectMatchAllEngines<"\\W">("!", true);
  expectMatchAllEngines<"\\W">("a", false);
  expectMatchAllEngines<"\\S">("a", true);
  expectMatchAllEngines<"\\S">(" ", false);
}

TEST(RegexCrossEngineTest, NegatedCharClassInAlternation) {
  expectSearchAllEngines<"[^a]|[^b]">("a", true);
  expectSearchAllEngines<"[^a]|[^b]">("c", true);
}

// ===== Cross-Engine: Multiline Flag =====

TEST(RegexCrossEngineTest, MultilineBeginAnchor) {
  expectSearchAllEngines<"^line", Flags::Multiline>(
      "first\nline two", true, "line");
  expectSearchAllEngines<"^line", Flags::Multiline>("line one", true, "line");
}

TEST(RegexCrossEngineTest, MultilineEndAnchor) {
  expectSearchAllEngines<"end$", Flags::Multiline>(
      "the end\nnext line", true, "end");
  expectSearchAllEngines<"end$", Flags::Multiline>("the end", true, "end");
}

TEST(RegexCrossEngineTest, MultilineDoesNotAffectStringAnchors) {
  expectSearchAllEngines<R"(\Afirst)", Flags::Multiline>(
      "first\nsecond", true, "first");
  expectSearchAllEngines<R"(\Afirst)", Flags::Multiline>(
      "second\nfirst", false);

  expectSearchAllEngines<R"(last\z)", Flags::Multiline>(
      "first\nlast", true, "last");
  expectSearchAllEngines<R"(last\z)", Flags::Multiline>("last\nfirst", false);
}

// ===== Cross-Engine: DotAll Flag =====

TEST(RegexCrossEngineTest, DotAllDotMatchesNewline) {
  expectMatchAllEngines<"a.b", Flags::DotAll>("a\nb", true);
  expectMatchAllEngines<"a.b", Flags::DotAll>("axb", true);
}

TEST(RegexCrossEngineTest, DotAllDefault) {
  expectMatchAllEngines<"a.b">("a\nb", false);
  expectMatchAllEngines<"a.b">("axb", true);
}

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

// ===== Backtracker-Only Tests =====
//
// These tests exercise features that only the backtracker supports:
// lookahead, lookbehind, backreferences, possessive quantifiers,
// and word boundaries.

TEST(RegexBacktrackerOnlyTest, PositiveLookahead) {
  constexpr auto re = compile<"\\w+(?=@)">();
  auto m = re.search("user@host");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "user");
}

TEST(RegexBacktrackerOnlyTest, PositiveLookaheadNoMatch) {
  constexpr auto re = compile<"\\w+(?=@)">();
  auto m = re.search("no at sign");
  EXPECT_FALSE(m);
}

TEST(RegexBacktrackerOnlyTest, NegativeLookahead) {
  constexpr auto re = compile<"\\d+(?!\\d)">();
  auto m = re.search("abc 123 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123");
}

TEST(RegexBacktrackerOnlyTest, NegativeLookaheadFilter) {
  constexpr auto re = compile<"foo(?!bar).*">();
  EXPECT_FALSE(re.match("foobar"));
  EXPECT_TRUE(re.match("foobaz"));
}

TEST(RegexBacktrackerOnlyTest, PositiveLookbehind) {
  constexpr auto re = compile<"(?<=@)\\w+">();
  auto m = re.search("user@host");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "host");
}

TEST(RegexBacktrackerOnlyTest, PositiveLookbehindNoMatch) {
  constexpr auto re = compile<"(?<=@)\\w+">();
  auto m = re.search("no at sign");
  EXPECT_FALSE(m);
}

TEST(RegexBacktrackerOnlyTest, NegativeLookbehind) {
  constexpr auto re = compile<"(?<!\\d)\\d+">();
  auto m = re.search("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexBacktrackerOnlyTest, MultiCharLookbehind) {
  constexpr auto re = compile<"(?<=abc)def">();
  EXPECT_TRUE(re.search("abcdef"));
  EXPECT_FALSE(re.search("xxdef"));
}

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

TEST(RegexCrossEngineTest, FlatSequenceCaptures) {
  // Captures in non-backtracking (flat) children
  constexpr auto re = compile<"(abc)(def)">();
  auto m = re.match("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
  EXPECT_EQ(m[2], "def");

  // Captures with backtracking child in the middle
  constexpr auto re2 = compile<"(abc)([a-z]+)(xyz)">();
  auto m2 = re2.match("abcdefxyz");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "abc");
  EXPECT_EQ(m2[2], "def");
  EXPECT_EQ(m2[3], "xyz");
}

TEST(RegexCrossEngineTest, FlatSequenceStateRestore) {
  // (abc)(def)ghi — if trailing fails, captures must be rolled back
  constexpr auto re = compile<"(abc)(def)ghi">();
  auto m = re.match("abcdefghi");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "abc");
  EXPECT_EQ(m[2], "def");

  // Failed match — captures should not leak
  auto m2 = re.match("abcdefxxx");
  EXPECT_FALSE(m2);
}

TEST(RegexCrossEngineTest, FlatSequenceFixedWidthAlternation) {
  // Multi-char alternation branches with equal fixed width are
  // non-backtracking: each branch consumes exactly 3 chars.
  // Single-char alternations get optimized to CharClass, so use
  // multi-char branches with mostly disjoint prefixes.
  expectMatchAllEngines<"(abc|def|ghi)xyz">("abcxyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("defxyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("ghixyz", true);
  expectMatchAllEngines<"(abc|def|ghi)xyz">("abcdef", false);

  // Mixed-width branches are NOT non-backtracking
  // (ab|cde) has widths 2 and 3 — must stay in continuation-nesting
  expectMatchAllEngines<"(ab|cde)f">("abf", true);
  expectMatchAllEngines<"(ab|cde)f">("cdef", true);

  // Fixed-width alternation with captures
  constexpr auto re = compile<"(abc|def)(ghi|jkl)!">();
  auto m3 = re.match("abcjkl!");
  EXPECT_TRUE(m3);
  EXPECT_EQ(m3[1], "abc");
  EXPECT_EQ(m3[2], "jkl");

  auto m4 = re.match("defghi!");
  EXPECT_TRUE(m4);
  EXPECT_EQ(m4[1], "def");
  EXPECT_EQ(m4[2], "ghi");
}

// ===== API Tests =====
//
// These tests exercise the public API surface (structured bindings,
// user-defined literals, freestanding functions, matchAll iterator)
// rather than engine correctness.

TEST(RegexAPITest, StructuredBindingsBasic) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("123-hello");
  EXPECT_EQ(full, "123-hello");
  EXPECT_EQ(num, "123");
  EXPECT_EQ(word, "hello");
}

TEST(RegexAPITest, StructuredBindingsFailedMatch) {
  constexpr auto re = compile<"(\\d+)-(\\w+)">();
  auto [full, num, word] = re.match("no-match-here!!!");
  EXPECT_TRUE(full.empty());
  EXPECT_TRUE(num.empty());
  EXPECT_TRUE(word.empty());
}

TEST(RegexAPITest, StructuredBindingsSingleGroup) {
  constexpr auto re = compile<"(\\w+)">();
  auto [full, word] = re.match("hello");
  EXPECT_EQ(full, "hello");
  EXPECT_EQ(word, "hello");
}

TEST(RegexAPITest, StructuredBindingsZeroGroups) {
  constexpr auto re = compile<"hello">();
  auto [full] = re.match("hello");
  EXPECT_EQ(full, "hello");
}

TEST(RegexAPITest, StructuredBindingsWithSearch) {
  constexpr auto re = compile<"(\\d+)">();
  auto [full, digits] = re.search("abc 42 def");
  EXPECT_EQ(full, "42");
  EXPECT_EQ(digits, "42");
}

TEST(RegexAPITest, StructuredBindingsDateParsing) {
  constexpr auto re = compile<"(\\d{4})-(\\d{2})-(\\d{2})">();
  auto [full, year, month, day] = re.match("2026-03-23");
  EXPECT_EQ(year, "2026");
  EXPECT_EQ(month, "03");
  EXPECT_EQ(day, "23");
}

TEST(RegexAPITest, FreestandingMatch) {
  auto m = folly::regex::match<"(\\d+)-(\\w+)">("123-hello");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "hello");
}

TEST(RegexAPITest, FreestandingSearch) {
  auto m = folly::regex::search<"\\d+">("abc 42 def");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "42");
}

TEST(RegexAPITest, FreestandingTest) {
  EXPECT_TRUE(folly::regex::test<"\\d+">("abc 42 def"));
  EXPECT_FALSE(folly::regex::test<"\\d+">("no digits"));
}

TEST(RegexAPITest, FreestandingWithFlags) {
  auto m = folly::regex::match<"hello", Flags::None>("hello");
  EXPECT_TRUE(m);
}

TEST(RegexAPITest, FreestandingStructuredBindings) {
  auto [full, num] = folly::regex::match<"(\\d+)">("42");
  EXPECT_EQ(num, "42");
}

TEST(RegexAPITest, MatchAllMultipleMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("12 abc 34 def 56")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"12", "34", "56"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllNoMatches) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("no digits")) {
    matches.emplace_back(m[0]);
  }
  EXPECT_TRUE(matches.empty());
}

TEST(RegexAPITest, MatchAllSingleMatch) {
  constexpr auto re = compile<"\\d+">();
  std::vector<std::string> matches;
  for (auto m : re.matchAll("abc42def")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"42"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllFreestanding) {
  std::vector<std::string> matches;
  for (auto m : folly::regex::matchAll<"\\d+">("1 2 3")) {
    matches.emplace_back(m[0]);
  }
  std::vector<std::string> expected{"1", "2", "3"};
  EXPECT_EQ(matches, expected);
}

TEST(RegexAPITest, MatchAllWithCaptures) {
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

TEST(RegexAPITest, PrefixIterator) {
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

TEST(RegexAPITest, BasicUDL) {
  using namespace folly::regex::literals;
  constexpr auto re = "\\d+"_re;
  EXPECT_TRUE(re.match("123"));
  EXPECT_FALSE(re.match("abc"));
}

TEST(RegexAPITest, UDLSearch) {
  using namespace folly::regex::literals;
  constexpr auto re = "(\\w+)@(\\w+)"_re;
  auto m = re.search("contact user@host please");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "user");
  EXPECT_EQ(m[2], "host");
}

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
  if constexpr (Regex<"[a-z]+?">::dfaProg_.valid) {
    auto greedy = Regex<"[a-z]+", Flags::ForceDFA>::search("abc");
    auto lazy = Regex<"[a-z]+?", Flags::ForceDFA>::search("abc");
    EXPECT_EQ(std::string(greedy[0]), "abc");
    EXPECT_EQ(std::string(lazy[0]), "a");
  }
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

// ===== Cross-Engine: POSIX Character Classes =====

TEST(RegexCrossEngineTest, PosixCharClasses) {
  // [:alpha:]
  expectMatchAllEngines<"[[:alpha:]]">("a", true);
  expectMatchAllEngines<"[[:alpha:]]">("Z", true);
  expectMatchAllEngines<"[[:alpha:]]">("1", false);

  // [:digit:]
  expectMatchAllEngines<"[[:digit:]]">("5", true);
  expectMatchAllEngines<"[[:digit:]]">("a", false);

  // [:alnum:]
  expectMatchAllEngines<"[[:alnum:]]">("a", true);
  expectMatchAllEngines<"[[:alnum:]]">("5", true);
  expectMatchAllEngines<"[[:alnum:]]">("!", false);

  // [:upper:] / [:lower:]
  expectMatchAllEngines<"[[:upper:]]">("A", true);
  expectMatchAllEngines<"[[:upper:]]">("a", false);
  expectMatchAllEngines<"[[:lower:]]">("a", true);
  expectMatchAllEngines<"[[:lower:]]">("A", false);

  // [:space:] / [:blank:]
  expectMatchAllEngines<"[[:space:]]">(" ", true);
  expectMatchAllEngines<"[[:space:]]">("\t", true);
  expectMatchAllEngines<"[[:space:]]">("\n", true);
  expectMatchAllEngines<"[[:space:]]">("a", false);
  expectMatchAllEngines<"[[:blank:]]">(" ", true);
  expectMatchAllEngines<"[[:blank:]]">("\t", true);
  expectMatchAllEngines<"[[:blank:]]">("\n", false);

  // [:punct:]
  expectMatchAllEngines<"[[:punct:]]">("!", true);
  expectMatchAllEngines<"[[:punct:]]">(".", true);
  expectMatchAllEngines<"[[:punct:]]">("a", false);

  // [:xdigit:]
  expectMatchAllEngines<"[[:xdigit:]]">("a", true);
  expectMatchAllEngines<"[[:xdigit:]]">("F", true);
  expectMatchAllEngines<"[[:xdigit:]]">("9", true);
  expectMatchAllEngines<"[[:xdigit:]]">("g", false);

  // [:ascii:]
  expectMatchAllEngines<"[[:ascii:]]">("a", true);
  expectMatchAllEngines<"[[:ascii:]]">("\x7f", true);

  // [:cntrl:]
  expectMatchAllEngines<"[[:cntrl:]]">({"\x00", 1}, true);
  expectMatchAllEngines<"[[:cntrl:]]">("\x1f", true);
  expectMatchAllEngines<"[[:cntrl:]]">("a", false);

  // [:graph:] / [:print:]
  expectMatchAllEngines<"[[:graph:]]">("a", true);
  expectMatchAllEngines<"[[:graph:]]">(" ", false);
  expectMatchAllEngines<"[[:print:]]">("a", true);
  expectMatchAllEngines<"[[:print:]]">(" ", true);
  expectMatchAllEngines<"[[:print:]]">("\x01", false);
}

TEST(RegexCrossEngineTest, PosixClassCombinations) {
  // Mixed with ranges
  expectMatchAllEngines<"[[:digit:]a-f]">("5", true);
  expectMatchAllEngines<"[[:digit:]a-f]">("c", true);
  expectMatchAllEngines<"[[:digit:]a-f]">("g", false);

  // Multiple POSIX classes
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("a", true);
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("5", true);
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("!", false);

  // Negated POSIX class
  expectMatchAllEngines<"[[:^alpha:]]">("1", true);
  expectMatchAllEngines<"[[:^alpha:]]">("a", false);

  // Negated bracket with POSIX class
  expectMatchAllEngines<"[^[:digit:]]">("a", true);
  expectMatchAllEngines<"[^[:digit:]]">("5", false);
}

TEST(RegexCrossEngineTest, PosixClassSearch) {
  expectSearchAllEngines<"[[:digit:]]+">("abc123def", true, "123");
  expectSearchAllEngines<"[[:alpha:]]+">("123abc456", true, "abc");
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

// ===== Cross-Engine: Lookaround Tests =====

TEST(RegexCrossEngineTest, PositiveLookaheadSimple) {
  // \d+(?=px) — digits followed by "px"
  expectSearchAllEngines<R"(\d+(?=px))">("100px", true, "100");
  expectSearchAllEngines<R"(\d+(?=px))">("100em", false);
  expectSearchAllEngines<R"(\d+(?=px))">("px", false);
}

TEST(RegexCrossEngineTest, NegativeLookaheadSimple) {
  // \d+(?!px) — digits NOT followed by "px"
  expectSearchAllEngines<R"(\d+(?!px))">("100em", true, "100");
  expectSearchAllEngines<R"(\d+(?!px))">("100", true, "100");
}

TEST(RegexCrossEngineTest, PositiveLookaheadMatch) {
  // Full match: a(?=b)b — lookahead checks b, then pattern consumes it
  expectMatchAllEngines<R"(a(?=b)b)">("ab", true);
  expectMatchAllEngines<R"(a(?=b)b)">("ac", false);
  expectMatchAllEngines<R"(a(?=b)b)">("a", false);
}

TEST(RegexCrossEngineTest, NegativeLookaheadMatch) {
  // a(?!b). — a not followed by b, then any char
  expectMatchAllEngines<R"(a(?!b)\C)">("ac", true);
  expectMatchAllEngines<R"(a(?!b)\C)">("ab", false);
  expectMatchAllEngines<R"(a(?!b)\C)">("ad", true);
}

TEST(RegexCrossEngineTest, PositiveLookbehindSimple) {
  // (?<=\$)\d+ — digits preceded by $
  expectSearchAllEngines<R"((?<=\$)\d+)">("$100", true, "100");
  expectSearchAllEngines<R"((?<=\$)\d+)">("100", false);
  expectSearchAllEngines<R"((?<=\$)\d+)">("€100", false);
}

TEST(RegexCrossEngineTest, NegativeLookbehindSimple) {
  // (?<!\$)\d+ — digits NOT preceded by $
  expectSearchAllEngines<R"((?<!\$)\d+)">("100", true, "100");
  expectSearchAllEngines<R"((?<!\$)\d+)">("x100", true, "100");
}

TEST(RegexCrossEngineTest, PositiveLookbehindMatch) {
  // (?<=a)b — b preceded by a
  expectMatchAllEngines<R"((?<=a)b)">("b", false); // no preceding 'a'
  expectSearchAllEngines<R"((?<=a)b)">("ab", true, "b");
  expectSearchAllEngines<R"((?<=a)b)">("cb", false);
}

TEST(RegexCrossEngineTest, NegativeLookbehindMatch) {
  // (?<!a)b — b NOT preceded by a
  expectSearchAllEngines<R"((?<!a)b)">("cb", true, "b");
  expectSearchAllEngines<R"((?<!a)b)">("ab", false);
  expectSearchAllEngines<R"((?<!a)b)">("b", true, "b");
}

TEST(RegexCrossEngineTest, LookbehindMultiChar) {
  // (?<=abc)d — d preceded by "abc"
  expectSearchAllEngines<R"((?<=abc)d)">("abcd", true, "d");
  expectSearchAllEngines<R"((?<=abc)d)">("xbcd", false);
  expectSearchAllEngines<R"((?<=abc)d)">("d", false);
}

TEST(RegexCrossEngineTest, LookaheadNfaCompatible) {
  // Verify that lookahead patterns are NFA-compatible
  static_assert(Regex<R"(\w+(?=@))">::parsed_.nfa_compatible);
  static_assert(Regex<R"(a(?=b))">::parsed_.nfa_compatible);
  static_assert(Regex<R"(a(?!b))">::parsed_.nfa_compatible);
  static_assert(Regex<R"((?<=a)b)">::parsed_.nfa_compatible);
  static_assert(Regex<R"((?<!a)b)">::parsed_.nfa_compatible);
}

TEST(RegexCrossEngineTest, LookaheadProbeDebug) {
  // Diagnostic test: check probe infrastructure state
  using R = Regex<R"(\d+(?=px))">;
  EXPECT_TRUE(R::parsed_.nfa_compatible);
  EXPECT_GT(R::parsed_.probe_count, 0);

  // Probe store should have the probe
  EXPECT_GT(R::probeStore_.probe_count, 0);
  EXPECT_TRUE(R::probeStore_.hasProbe(0));
  EXPECT_GE(R::probeStore_.probes[0].root, 0);

  // NFA should have embedded lookaround probe
  EXPECT_TRUE(R::nfaProg_.has_lookaround_probes);
  EXPECT_GT(R::nfaProg_.lookaround_probe_count, 0);
  EXPECT_GE(R::nfaProg_.lookaround_probe_start[0], 0);

  // Individual engine tests
  auto bt = Regex<R"(\d+(?=px))", Flags::ForceBacktracking>::search("100px");
  EXPECT_TRUE(bool(bt)) << "BT should find match";

  auto nfa = Regex<R"(\d+(?=px))", Flags::ForceNFA>::search("100px");
  EXPECT_TRUE(bool(nfa)) << "NFA should find match";
}
