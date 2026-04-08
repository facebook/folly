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

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// ===== Optimization Correctness Tests =====

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingMatch) {
  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("hell", false);
  expectMatchAllEngines<"hello">("helloo", false);
  expectMatchAllEngines<"hello">("", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixStrippingSearch) {
  expectSearchAllEngines<"world">("hello world", true, "world");
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchNotFound) {
  expectSearchAllEngines<"xyz">("hello world", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixSearchAtStart) {
  expectSearchAllEngines<"hello">("hello world", true, "hello");
}

TEST(RegexCrossEngineOptTest, PrefixWithRepeat) {
  expectMatchAllEngines<"a+">("a", true);
  expectMatchAllEngines<"a+">("aaa", true);
  expectMatchAllEngines<"a+">("", false);
}

TEST(RegexCrossEngineOptTest, LiteralPrefixWithSuffix) {
  expectMatchAllEngines<"abc.*xyz">("abcxyz", true);
  expectMatchAllEngines<"abc.*xyz">("abc123xyz", true);
  expectMatchAllEngines<"abc.*xyz">("abcxy", false);
  expectMatchAllEngines<"abc.*xyz">("bc123xyz", false);
}

TEST(RegexCrossEngineOptTest, SuffixFastRejectMatch) {
  expectMatchAllEngines<"abc">("abc", true);
  expectMatchAllEngines<"abc">("abd", false);
  expectMatchAllEngines<"abc">("xbc", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithCapturingGroup) {
  auto m = expectMatchCapturesAgree<"(abc)def">("abcdef");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "abcdef");
  EXPECT_EQ(m[1], "abc");
}

TEST(RegexCrossEngineOptTest, PrefixStrippedWithCaptures) {
  auto m = expectMatchCapturesAgree<"hello(world)">("helloworld");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "helloworld");
  EXPECT_EQ(m[1], "world");
}

TEST(RegexCrossEngineOptTest, SearchWithPrefixAndCaptures) {
  auto m = expectSearchCapturesAgree<"foo(bar)">("xxxfoobaryyyy");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "foobar");
  EXPECT_EQ(m[1], "bar");
}

TEST(RegexCrossEngineOptTest, TestWithPrefix) {
  expectTestAllEngines<"hello">("say hello there", true);
  expectTestAllEngines<"hello">("no match here", false);
}

TEST(RegexCrossEngineOptTest, AlternationCommonPrefixMatch) {
  expectMatchAllEngines<"dev|devvm|devrs">("dev", true);
  expectMatchAllEngines<"dev|devvm|devrs">("devvm", true);
  expectMatchAllEngines<"dev|devvm|devrs">("devrs", true);
  expectMatchAllEngines<"dev|devvm|devrs">("de", false);
  expectMatchAllEngines<"dev|devvm|devrs">("devx", false);
}

TEST(RegexCrossEngineOptTest, AlternationPartialCommonPrefix) {
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("dev", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devvm", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devrs", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("shellserver", true);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("shell", false);
  expectMatchAllEngines<"dev|devvm|devrs|shellserver">("devbig", false);
}

TEST(RegexCrossEngineOptTest, AlternationNoCommonPrefix) {
  expectMatchAllEngines<"abc|def|ghi">("abc", true);
  expectMatchAllEngines<"abc|def|ghi">("def", true);
  expectMatchAllEngines<"abc|def|ghi">("ghi", true);
  expectMatchAllEngines<"abc|def|ghi">("abcdef", false);
}

TEST(RegexCrossEngineOptTest, AlternationSearchWithFactoring) {
  auto m =
      expectSearchCapturesAgree<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
          "host=devvm.example.com");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "devvm");
}

TEST(RegexCrossEngineOptTest, AlternationSearchMultipleBranches) {
  expectSearchAllEngines<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
      "shellserver.example.com", true);
  expectSearchAllEngines<"(devvm|devrs|devbig|devgpu|dev|shellserver)">(
      "webserver.example.com", false);
}

TEST(RegexCrossEngineOptTest, AlternationWithCaptures) {
  auto m1 = expectMatchCapturesAgree<"(abc|abd)">("abc");
  EXPECT_TRUE(m1);
  EXPECT_EQ(m1[1], "abc");
  auto m2 = expectMatchCapturesAgree<"(abc|abd)">("abd");
  EXPECT_TRUE(m2);
  EXPECT_EQ(m2[1], "abd");
}

TEST(RegexCrossEngineOptTest, AlternationNestedFactoring) {
  expectMatchAllEngines<"foobar|foobaz|fooqux">("foobar", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("foobaz", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("fooqux", true);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("foo", false);
  expectMatchAllEngines<"foobar|foobaz|fooqux">("fooby", false);
}

TEST(RegexCrossEngineOptTest, AlternationSingleCharBranches) {
  expectMatchAllEngines<"a|b|c">("a", true);
  expectMatchAllEngines<"a|b|c">("b", true);
  expectMatchAllEngines<"a|b|c">("c", true);
  expectMatchAllEngines<"a|b|c">("d", false);
}

TEST(RegexCrossEngineOptTest, PrefixWithAnchor) {
  expectSearchAllEngines<"world$">("hello world", true, "world");
  expectSearchAllEngines<"world$">("world hello", false);
}

TEST(RegexCrossEngineOptTest, GroupFlatteningSingle) {
  expectMatchAllEngines<"(?:(?:a))">("a", true);
  expectMatchAllEngines<"(?:(?:a))">("b", false);
  expectMatchAllEngines<"(?:(?:a))">("aa", false);
}

TEST(RegexCrossEngineOptTest, GroupFlatteningNested) {
  expectMatchAllEngines<"(?:(?:(?:abc)))">("abc", true);
  expectMatchAllEngines<"(?:(?:(?:abc)))">("ab", false);
  expectMatchAllEngines<"(?:(?:(?:abc)))">("abcd", false);
}

TEST(RegexCrossEngineOptTest, GroupFlatteningPreservesCapturing) {
  auto m = expectMatchCapturesAgree<"(?:(a))">("a");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[1], "a");
}

TEST(RegexCrossEngineOptTest, RepeatExactlyOne) {
  expectMatchAllEngines<"a{1}">("a", true);
  expectMatchAllEngines<"a{1}">("aa", false);
  expectMatchAllEngines<"a{1}">("", false);
}

TEST(RegexCrossEngineOptTest, RepeatZeroTimes) {
  expectMatchAllEngines<"a{0,0}">("", true);
  expectMatchAllEngines<"a{0,0}">("a", false);
}

TEST(RegexCrossEngineOptTest, RepeatOneInSequence) {
  expectMatchAllEngines<"ab{1}c">("abc", true);
  expectMatchAllEngines<"ab{1}c">("abbc", false);
  expectMatchAllEngines<"ab{1}c">("ac", false);
}

TEST(RegexCrossEngineOptTest, DuplicateBranchElimination) {
  expectMatchAllEngines<"foo|foo|bar">("foo", true);
  expectMatchAllEngines<"foo|foo|bar">("bar", true);
  expectMatchAllEngines<"foo|foo|bar">("baz", false);
}

TEST(RegexCrossEngineOptTest, DuplicateBranchAllSame) {
  expectMatchAllEngines<"abc|abc|abc">("abc", true);
  expectMatchAllEngines<"abc|abc|abc">("ab", false);
  expectMatchAllEngines<"abc|abc|abc">("abcd", false);
}

TEST(RegexCrossEngineOptTest, SingleCharAlternationMerge) {
  expectMatchAllEngines<"a|b|c|d">("a", true);
  expectMatchAllEngines<"a|b|c|d">("b", true);
  expectMatchAllEngines<"a|b|c|d">("c", true);
  expectMatchAllEngines<"a|b|c|d">("d", true);
  expectMatchAllEngines<"a|b|c|d">("e", false);
  expectMatchAllEngines<"a|b|c|d">("ab", false);
}

TEST(RegexCrossEngineOptTest, CharClassMergeInAlternation) {
  expectMatchAllEngines<"[a-m]|[n-z]">("a", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("m", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("n", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("z", true);
  expectMatchAllEngines<"[a-m]|[n-z]">("A", false);
  expectMatchAllEngines<"[a-m]|[n-z]">("0", false);
}

TEST(RegexCrossEngineOptTest, MixedCharMerge) {
  expectMatchAllEngines<"a|[b-d]|e">("a", true);
  expectMatchAllEngines<"a|[b-d]|e">("b", true);
  expectMatchAllEngines<"a|[b-d]|e">("c", true);
  expectMatchAllEngines<"a|[b-d]|e">("d", true);
  expectMatchAllEngines<"a|[b-d]|e">("e", true);
  expectMatchAllEngines<"a|[b-d]|e">("f", false);
}

TEST(RegexCrossEngineOptTest, AnchorHoistingBegin) {
  expectSearchAllEngines<"^foo|^bar">("foo123", true);
  expectSearchAllEngines<"^foo|^bar">("bar456", true);
  expectSearchAllEngines<"^foo|^bar">("xfoo", false);
  expectSearchAllEngines<"^foo|^bar">("xbar", false);
}

TEST(RegexCrossEngineOptTest, AnchorHoistingEnd) {
  expectSearchAllEngines<"foo$|bar$">("123foo", true);
  expectSearchAllEngines<"foo$|bar$">("456bar", true);
  expectSearchAllEngines<"foo$|bar$">("foox", false);
  expectSearchAllEngines<"foo$|bar$">("barx", false);
}

TEST(RegexCrossEngineOptTest, AnchorHoistingNoMix) {
  expectSearchAllEngines<"^foo|bar$">("foobar", true);
  expectSearchAllEngines<"^foo|bar$">("xbar", true);
  expectSearchAllEngines<"^foo|bar$">("foox", true);
  expectSearchAllEngines<"^foo|bar$">("xbaz", false);
}

TEST(RegexCrossEngineOptTest, AlternationToOptional) {
  expectMatchAllEngines<"(a|ab)+c">("ac", true);
  expectMatchAllEngines<"(a|ab)+c">("abc", true);
  expectMatchAllEngines<"(a|ab)+c">("aac", true);
  expectMatchAllEngines<"(a|ab)+c">("ababc", true);
  expectMatchAllEngines<"(a|ab)+c">("abac", true);
  expectMatchAllEngines<"(a|ab)+c">("aababc", true);
  expectMatchAllEngines<"(a|ab)+c">("c", false);
  expectMatchAllEngines<"(a|ab)+c">("ab", false);
  expectMatchAllEngines<"(a|ab)+c">("", false);
  expectMatchAllEngines<"(a|ab)+c">("bc", false);

  expectSearchAllEngines<"(a|ab)+c">("xababcx", true, "ababc");

  // Equivalent optimized pattern produces same results
  expectMatchAllEngines<"(ab?)+c">("ac", true);
  expectMatchAllEngines<"(ab?)+c">("abc", true);
  expectMatchAllEngines<"(ab?)+c">("ababc", true);
  expectMatchAllEngines<"(ab?)+c">("c", false);
  expectMatchAllEngines<"(ab?)+c">("ab", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedNestedQuant) {
  // (?:a*)*b — after optimization becomes a*b
  expectMatchAllEngines<"(?:a*)*b">("b", true);
  expectMatchAllEngines<"(?:a*)*b">("ab", true);
  expectMatchAllEngines<"(?:a*)*b">("aaab", true);
  expectMatchAllEngines<"(?:a*)*b">("aaa", false);
  expectMatchAllEngines<"(?:a*)*b">("", false);

  // (?:a+)*b — after optimization becomes a*b
  expectMatchAllEngines<"(?:a+)*b">("b", true);
  expectMatchAllEngines<"(?:a+)*b">("ab", true);
  expectMatchAllEngines<"(?:a+)*b">("aaab", true);
  expectMatchAllEngines<"(?:a+)*b">("aaa", false);
}

TEST(RegexCrossEngineOptTest, AdjacentRepeatMerge) {
  // a+a+b — after merging becomes a{2,}b
  expectMatchAllEngines<"a+a+b">("ab", false);
  expectMatchAllEngines<"a+a+b">("aab", true);
  expectMatchAllEngines<"a+a+b">("aaab", true);
  expectMatchAllEngines<"a+a+b">("b", false);
  expectMatchAllEngines<"a+a+b">("", false);
}

TEST(RegexCrossEngineOptTest, FixedLengthSequence) {
  // (?:ab)+ — fixed-length sequence repeat
  expectMatchAllEngines<"(?:ab)+">("ab", true);
  expectMatchAllEngines<"(?:ab)+">("abab", true);
  expectMatchAllEngines<"(?:ab)+">("ababab", true);
  expectMatchAllEngines<"(?:ab)+">("a", false);
  expectMatchAllEngines<"(?:ab)+">("aba", false);
  expectMatchAllEngines<"(?:ab)+">("", false);

  expectSearchAllEngines<"(?:ab)+">("xababx", true, "abab");
}

TEST(RegexCrossEngineOptTest, SingleCharClassToLiteral) {
  // [a]+b — after converting [a]→a, becomes a+b
  expectMatchAllEngines<"[a]+b">("ab", true);
  expectMatchAllEngines<"[a]+b">("aaab", true);
  expectMatchAllEngines<"[a]+b">("b", false);
  expectMatchAllEngines<"[a]+b">("", false);
}

TEST(RegexCrossEngineOptTest, PossessiveInference) {
  expectMatchAllEngines<"[a-z]+[0-9]+">("abc123", true);
  expectMatchAllEngines<"[a-z]+[0-9]+">("abc", false);
  expectMatchAllEngines<"[a-z]+[0-9]+">("123", false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatExactTimesExact) {
  // (?:a{4}){5} → a{20}
  expectMatchAllEngines<"(?:a{4}){5}">(std::string(19, 'a'), false);
  expectMatchAllEngines<"(?:a{4}){5}">(std::string(20, 'a'), true);
  expectMatchAllEngines<"(?:a{4}){5}">(std::string(21, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatRangeTimesExact) {
  // (?:a{2,3}){4} → a{8,12}
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(8, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(12, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4}">(std::string(13, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatExactTimesRange) {
  // (?:a{3}){2,5} → a{6,15}
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(15, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,5}">(std::string(16, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatRangeTimesRange) {
  // (?:a{2,3}){4,5} → a{8,15}
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(7, 'a'), false);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(8, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(11, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(15, 'a'), true);
  expectMatchAllEngines<"(?:a{2,3}){4,5}">(std::string(16, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatCharClass) {
  // (?:[a-z]{3}){2} → [a-z]{6}
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcde", false);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcdef", true);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcdefg", false);
  expectMatchAllEngines<"(?:[a-z]{3}){2}">("abcDE1", false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatUnboundedInner) {
  // (?:a{2,}){5} → a{10,}
  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(9, 'a'), false);
  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(10, 'a'), true);
  expectMatchAllEngines<"(?:a{2,}){5}">(std::string(50, 'a'), true);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatUnboundedOuter) {
  // (?:a{3}){2,} → a{6,}
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{3}){2,}">(std::string(50, 'a'), true);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatCapturingBlocks) {
  // Capturing group blocks flattening — semantics preserved.
  std::string capInput(6, 'a');
  auto m = expectMatchCapturesAgree<"(a{2}){3}">(capInput);
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "aaaaaa");
  EXPECT_EQ(m[1], "aa");
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatGreedinessBlocks) {
  // Greedy/lazy mismatch blocks flattening — match still correct.
  expectMatchAllEngines<"(?:a{2}?){3}">(std::string(5, 'a'), false);
  expectMatchAllEngines<"(?:a{2}?){3}">(std::string(6, 'a'), true);
  expectMatchAllEngines<"(?:a{2}?){3}">(std::string(7, 'a'), false);
}

TEST(RegexCrossEngineOptTest, NestedCountedRepeatPossessiveBlocks) {
  // Possessive inner repeat blocks flattening — match still correct.
  constexpr auto re = compile<"(?:a{2}+){3}">();
  EXPECT_FALSE(re.match(std::string(5, 'a')));
  EXPECT_TRUE(re.match(std::string(6, 'a')));
  EXPECT_FALSE(re.match(std::string(7, 'a')));
}

TEST(RegexCrossEngineOptTest, BranchSubsumption) {
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123", false);

  // Equivalent to just (?:\w+)+z
  expectMatchAllEngines<"(?:\\w+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123", false);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionBasic) {
  // \w+ subsumes \d+
  expectMatchAllEngines<"(?:\\w+|\\d+)">("abc", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)">("123", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)">("!", false);

  // [a-z] subsumes [a-f]
  expectMatchAllEngines<"(?:[a-z]|[a-f])">("c", true);
  expectMatchAllEngines<"(?:[a-z]|[a-f])">("z", true);

  // AnyChar subsumes \w
  expectMatchAllEngines<"(?:.|\\w)">("a", true);
  expectMatchAllEngines<"(?:.|\\w)">("!", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionQuantified) {
  // \w+ subsumes \d+ (same quantifier, char subset)
  expectMatchAllEngines<"(?:\\w+|\\d+)z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)z">("123z", true);

  // \w+ subsumes \d (single char subsumed by + quantifier)
  expectMatchAllEngines<"(?:\\w+|\\d)z">("abcz", true);
  expectMatchAllEngines<"(?:\\w+|\\d)z">("1z", true);

  // \w* subsumes \d+ ([1,inf) subsumed by [0,inf))
  expectMatchAllEngines<"(?:\\w*|\\d+)z">("z", true);
  expectMatchAllEngines<"(?:\\w*|\\d+)z">("123z", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionNoElimination) {
  // \d doesn't subsume \w
  expectMatchAllEngines<"(?:\\d|\\w)">("a", true);
  expectMatchAllEngines<"(?:\\d|\\w)">("1", true);

  // Different repeat bounds, not subsumed (\d+ not subsumed by \d{3})
  expectMatchAllEngines<"(?:\\d{3}|\\d+)">("12", true);
  expectMatchAllEngines<"(?:\\d{3}|\\d+)">("123", true);
}

TEST(RegexCrossEngineOptTest, BranchSubsumptionEquivalence) {
  // (\w+|\d+)+z should behave identically to (\w+)+z
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+|\\d+)+z">("abc123", false);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123z", true);
  expectMatchAllEngines<"(?:\\w+)+z">("abc123", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedPrefixFactoring) {
  // \w+ common prefix → \w+[xy]
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcx", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcy", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y)">("abcz", false);

  // CharClass common prefix → [a-z](?:1|2)
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("a1", true);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("z2", true);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("a3", false);
  expectMatchAllEngines<"(?:[a-z]1|[a-z]2)">("A1", false);

  // Multi-node common prefix → \w+\d+[xy]
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123x", true);
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123y", true);
  expectMatchAllEngines<"(?:\\w+\\d+x|\\w+\\d+y)">("abc123z", false);

  // Partial group — only some branches share prefix
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("abcx", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("123z", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("123x", true);
  expectMatchAllEngines<"(?:\\w+x|\\w+y|\\d+z)">("abcz", false);

  // Combined with literal prefix: foo\w+x|foo\w+y → foo\w+[xy]
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcx", true);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcy", true);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("fooabcz", false);
  expectMatchAllEngines<"(?:foo\\w+x|foo\\w+y)">("barax", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedSuffixFactoring) {
  // \d+ common suffix → [xy]\d+
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("x123", true);
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("y456", true);
  expectMatchAllEngines<"(?:x\\d+|y\\d+)">("z789", false);

  // \w+ common suffix
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("abc", true);
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("bxy", true);
  expectMatchAllEngines<"(?:a\\w+|b\\w+)">("cxy", false);
}

TEST(RegexCrossEngineOptTest, GeneralizedFactoringSearchMode) {
  // Verify search mode works with generalized factoring
  expectSearchAllEngines<"(?:\\w+x|\\w+y)">("...abcx...", true);
  expectSearchAllEngines<"(?:\\w+x|\\w+y)">("...abcy...", true);
  expectSearchAllEngines<"(?:x\\d+|y\\d+)">("...x42...", true);
}
