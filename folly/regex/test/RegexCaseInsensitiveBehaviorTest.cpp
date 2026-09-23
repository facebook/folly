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
#include <folly/regex/detail/Parser.h>
#include <folly/regex/test/AstTestHelpers.h>
#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::testing;
using detail::AnchorKind;
using detail::NodeKind;
using detail::parse;
using detail::ParseErrorMode;

// =====================================================================
// Behavioral tests: case-insensitive matching across all engines.
// =====================================================================

TEST(RegexCaseInsensitiveTest, SimpleLiteralMatchesAllCases) {
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("hello", true);
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("HeLLo", true);
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("hElLo", true);
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("hello!", false);
  expectMatchAllEngines<"hello", Flags::CaseInsensitive>("hell", false);
}

TEST(RegexCaseInsensitiveTest, WithoutFlagIsCaseSensitive) {
  expectMatchAllEngines<"hello">("hello", true);
  expectMatchAllEngines<"hello">("HELLO", false);
  expectMatchAllEngines<"hello">("Hello", false);
}

TEST(RegexCaseInsensitiveTest, NonAlphaUnaffected) {
  expectMatchAllEngines<"abc123", Flags::CaseInsensitive>("abc123", true);
  expectMatchAllEngines<"abc123", Flags::CaseInsensitive>("ABC123", true);
  expectMatchAllEngines<"abc123", Flags::CaseInsensitive>("AbC123", true);
  // Digits stay literal — case-insensitive doesn't change them.
  expectMatchAllEngines<"abc123", Flags::CaseInsensitive>("ABC456", false);
}

TEST(RegexCaseInsensitiveTest, MixedAlphaAndPunctuation) {
  expectMatchAllEngines<"a-b", Flags::CaseInsensitive>("A-B", true);
  expectMatchAllEngines<"a-b", Flags::CaseInsensitive>("a-b", true);
  expectMatchAllEngines<"a-b", Flags::CaseInsensitive>("A-b", true);
  expectMatchAllEngines<"a-b", Flags::CaseInsensitive>("a_b", false);
}

TEST(RegexCaseInsensitiveTest, CharClassExpansionLowerToUpper) {
  expectMatchAllEngines<"[a-z]+", Flags::CaseInsensitive>("Hello", true);
  expectMatchAllEngines<"[a-z]+", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"[a-z]+", Flags::CaseInsensitive>("123", false);
}

TEST(RegexCaseInsensitiveTest, CharClassExpansionUpperToLower) {
  expectMatchAllEngines<"[A-Z]+", Flags::CaseInsensitive>("hello", true);
  expectMatchAllEngines<"[A-Z]+", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"[A-Z]+", Flags::CaseInsensitive>("HeLLo", true);
}

TEST(RegexCaseInsensitiveTest, NegatedCharClassExcludesBothCases) {
  // [^a-z] with /i should also exclude A-Z.
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("a", false);
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("A", false);
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("Z", false);
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("z", false);
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("1", true);
  expectMatchAllEngines<"[^a-z]", Flags::CaseInsensitive>("!", true);
}

TEST(RegexCaseInsensitiveTest, MixedAlphaNonAlphaRange) {
  // [0-Z] (48-90) with /i should match both [0-Z] AND [a-z]. Verified
  // against Perl: [0-Z] with /i matches 'a' but without /i does not.
  expectMatchAllEngines<"[0-Z]+", Flags::CaseInsensitive>("abc", true);
  expectMatchAllEngines<"[0-Z]+", Flags::CaseInsensitive>("ABC", true);
  expectMatchAllEngines<"[0-Z]+", Flags::CaseInsensitive>("123ABCabc", true);
  expectMatchAllEngines<"[0-Z]+">("abc", false);
}

TEST(RegexCaseInsensitiveTest, OddRangeAroundCaseGap) {
  // [Z-a] (90-97) with /i: Z (90) expands to add z (122), a (97) expands
  // to add A (65). Result: matches A, Z-a (incl punctuation), z. Verified
  // against Perl.
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("z", true);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("^", true);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("Y", false);
  expectMatchAllEngines<"[Z-a]", Flags::CaseInsensitive>("b", false);
}

TEST(RegexCaseInsensitiveTest, HexRange) {
  // [0-9A-F] with /i: A-F overlap expands to a-f.
  expectMatchAllEngines<"[0-9A-F]+", Flags::CaseInsensitive>("deadbeef", true);
  expectMatchAllEngines<"[0-9A-F]+", Flags::CaseInsensitive>("DEADBEEF", true);
  expectMatchAllEngines<"[0-9A-F]+", Flags::CaseInsensitive>("dEaDbEeF", true);
  expectMatchAllEngines<"[0-9A-F]+", Flags::CaseInsensitive>("xyz", false);
}

// =====================================================================
// Partial-alpha-overlap range tests (verified against Perl).
//
// These exercise ranges that don't span the entire [a-z] or [A-Z] block.
// The expansion logic must add only the case-flipped portion of the
// overlap, leaving non-alpha bytes in the range untouched.
// =====================================================================

TEST(RegexCaseInsensitiveTest, SubsetLowerRange) {
  // [a-f] with /i: matches a-f and A-F only.
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("f", true);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("F", true);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("g", false);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("G", false);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("z", false);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("Z", false);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("0", false);
  expectMatchAllEngines<"[a-f]", Flags::CaseInsensitive>("_", false);
}

TEST(RegexCaseInsensitiveTest, SubsetUpperRange) {
  // [A-F] with /i: matches A-F and a-f only.
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("F", true);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("f", true);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("g", false);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("G", false);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("z", false);
  expectMatchAllEngines<"[A-F]", Flags::CaseInsensitive>("Z", false);
}

TEST(RegexCaseInsensitiveTest, MidAlphaSubsetLower) {
  // [d-w] with /i: matches d-w and D-W; excludes both cases of letters
  // outside [d-w] / [D-W].
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("d", true);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("D", true);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("w", true);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("W", true);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("c", false);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("C", false);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("x", false);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("X", false);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("z", false);
  expectMatchAllEngines<"[d-w]", Flags::CaseInsensitive>("Z", false);
}

TEST(RegexCaseInsensitiveTest, MidAlphaSubsetUpper) {
  // [D-W] with /i: equivalent to [d-w] under /i.
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("D", true);
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("d", true);
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("W", true);
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("w", true);
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("c", false);
  expectMatchAllEngines<"[D-W]", Flags::CaseInsensitive>("x", false);
}

TEST(RegexCaseInsensitiveTest, RangeStraddlingDigitsToUpperPartial) {
  // [5-D] (53-68) covers 5-9, :;<=>?@, A-D. Only the A-D portion is
  // alpha; under /i it expands to also include a-d. Non-alpha bytes
  // in the range stay; bytes outside the range stay out.
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("5", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("9", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>(":", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("@", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("D", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("d", true);
  // Outside the alpha overlap → no match
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("E", false);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("e", false);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("Y", false);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("Z", false);
  expectMatchAllEngines<"[5-D]", Flags::CaseInsensitive>("4", false);
}

TEST(RegexCaseInsensitiveTest, RangeStraddlingUpperToLowerPartial) {
  // [Y-d] (89-100) covers Y-Z, [\]^_`, a-d. Both alpha portions get
  // expanded: Y-Z → y-z, a-d → A-D.
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("Y", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("d", true);
  // Expansions: y-z (from Y-Z) and A-D (from a-d).
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("y", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("z", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("D", true);
  // Bytes outside both alpha portions and outside the original range.
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("X", false);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("e", false);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("E", false);
  expectMatchAllEngines<"[Y-d]", Flags::CaseInsensitive>("F", false);
}

TEST(RegexCaseInsensitiveTest, RangeStraddlingBothAlphaBlocksFully) {
  // [B-y] (66-121) covers B-Z, [\]^_`, a-y. Expansions: B-Z → b-z,
  // a-y → A-Y. Combined coverage = entire alpha range A-Z, a-z, plus
  // the punctuation between them.
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("B", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("Y", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("b", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("y", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("z", true);
  // Underscore (95) is in the [\]^_`] gap, so matches in the original
  // range. Digit and outside-range bytes don't match.
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("_", true);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("0", false);
  expectMatchAllEngines<"[B-y]", Flags::CaseInsensitive>("$", false);
}

TEST(RegexCaseInsensitiveTest, RangeFullySpansBothAlphaBlocks) {
  // [C-w] (67-119) covers C-Z + [\]^_`] + a-w. Expansions: C-Z → c-z,
  // a-w → A-W. Combined: A-z. Verifies that the union of the original
  // range and both case-flipped sub-ranges covers all letters even when
  // the original range only partially covers each block.
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("B", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("C", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("K", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("Y", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("w", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("x", true);
  expectMatchAllEngines<"[C-w]", Flags::CaseInsensitive>("z", true);
}

TEST(RegexCaseInsensitiveTest, MultipleSubsetRanges) {
  // [a-fA-F] with /i — both ranges are within alpha, both get expanded
  // (no-op since the union already covers both cases).
  expectMatchAllEngines<"[a-fA-F]+", Flags::CaseInsensitive>("aBcDeF", true);
  expectMatchAllEngines<"[a-fA-F]+", Flags::CaseInsensitive>("F", true);
  expectMatchAllEngines<"[a-fA-F]+", Flags::CaseInsensitive>("g", false);
  expectMatchAllEngines<"[a-fA-F]+", Flags::CaseInsensitive>("G", false);
  expectMatchAllEngines<"[a-fA-F]+", Flags::CaseInsensitive>("0", false);
}

TEST(RegexCaseInsensitiveTest, MultipleNonOverlappingSubsetRanges) {
  // [a-cX-Z] — disjoint ranges in different alpha blocks. Each gets
  // expanded independently: a-c → A-C, X-Z → x-z.
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("c", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("C", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("X", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("x", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("z", true);
  // Bytes between the two expanded sub-ranges should NOT match.
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("d", false);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("D", false);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("W", false);
  expectMatchAllEngines<"[a-cX-Z]", Flags::CaseInsensitive>("w", false);
}

TEST(RegexCaseInsensitiveTest, NegatedSubsetRange) {
  // [^a-f] with /i: pre-negation expands to [a-fA-F], so the negated
  // class excludes both cases of a-f.
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("a", false);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("A", false);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("f", false);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("F", false);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("g", true);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("G", true);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("z", true);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("Z", true);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("0", true);
  expectMatchAllEngines<"[^a-f]", Flags::CaseInsensitive>("_", true);
}

TEST(RegexCaseInsensitiveTest, NegatedMixedAlphaNonAlphaRange) {
  // [^5-D] with /i: pre-negation expands to [5-D] ∪ [a-d] (since the
  // A-D portion of [5-D] expands to a-d). The negation excludes all of
  // these bytes.
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("4", true);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("5", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("9", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("A", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("D", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("a", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("d", false);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("E", true);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("e", true);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("z", true);
  expectMatchAllEngines<"[^5-D]", Flags::CaseInsensitive>("Z", true);
}

TEST(RegexCaseInsensitiveTest, SingleCharRangeExpands) {
  // Degenerate range [a-a] with /i is equivalent to [aA].
  expectMatchAllEngines<"[a-a]", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"[a-a]", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"[a-a]", Flags::CaseInsensitive>("b", false);
  expectMatchAllEngines<"[a-a]", Flags::CaseInsensitive>("B", false);
  // Same for [M-M].
  expectMatchAllEngines<"[M-M]", Flags::CaseInsensitive>("M", true);
  expectMatchAllEngines<"[M-M]", Flags::CaseInsensitive>("m", true);
  expectMatchAllEngines<"[M-M]", Flags::CaseInsensitive>("N", false);
  expectMatchAllEngines<"[M-M]", Flags::CaseInsensitive>("n", false);
}

TEST(RegexCaseInsensitiveTest, SearchFindsMixedCase) {
  expectSearchAllEngines<"hello", Flags::CaseInsensitive>(
      "say HELLO there", true, "HELLO");
  expectSearchAllEngines<"hello", Flags::CaseInsensitive>(
      "Say Hello There", true, "Hello");
  expectSearchAllEngines<"hello", Flags::CaseInsensitive>(
      "no greeting here", false);
}

TEST(RegexCaseInsensitiveTest, PosixUpperWithFlag) {
  // [:upper:] with /i should match lowercase too. Verified against Perl.
  expectMatchAllEngines<"[[:upper:]]+", Flags::CaseInsensitive>("hello", true);
  expectMatchAllEngines<"[[:upper:]]+", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"[[:upper:]]+", Flags::CaseInsensitive>("123", false);
}

TEST(RegexCaseInsensitiveTest, PosixLowerWithFlag) {
  expectMatchAllEngines<"[[:lower:]]+", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"[[:lower:]]+", Flags::CaseInsensitive>("hello", true);
  expectMatchAllEngines<"[[:lower:]]+", Flags::CaseInsensitive>("123", false);
}

TEST(RegexCaseInsensitiveTest, PosixDigitUnaffected) {
  // No alpha overlap; should be identical with or without /i.
  expectMatchAllEngines<"[[:digit:]]+", Flags::CaseInsensitive>("12345", true);
  expectMatchAllEngines<"[[:digit:]]+", Flags::CaseInsensitive>("abc", false);
}

TEST(RegexCaseInsensitiveTest, PosixWordPerlExtension) {
  // [[:word:]] is a Perl/PCRE2 extension equivalent to \w. Already
  // covers both cases; CI is a no-op for it.
  expectMatchAllEngines<"[[:word:]]+", Flags::CaseInsensitive>(
      "Hello123_", true);
  expectMatchAllEngines<"[[:word:]]+", Flags::CaseInsensitive>("HELLO", true);
  expectMatchAllEngines<"[[:word:]]+">("Hello123_", true);
  expectSearchAllEngines<"[[:word:]]+">("hello world", true, "hello");
  expectMatchAllEngines<"[[:word:]]">("!", false);
  expectMatchAllEngines<"[[:word:]]">(" ", false);
  // Negated POSIX word.
  expectMatchAllEngines<"[[:^word:]]+">("!@# ", true);
  expectMatchAllEngines<"[[:^word:]]">("a", false);
}

TEST(RegexCaseInsensitiveTest, PosixUpperWithoutFlagIsStrict) {
  expectMatchAllEngines<"[[:upper:]]+">("hello", false);
  expectMatchAllEngines<"[[:upper:]]+">("HELLO", true);
}

TEST(RegexCaseInsensitiveTest, ShorthandWordUnchanged) {
  // \w already includes both cases — flag is a no-op.
  expectMatchAllEngines<"\\w+", Flags::CaseInsensitive>("Hello123", true);
  expectMatchAllEngines<"\\w+", Flags::CaseInsensitive>("!@#", false);
}

TEST(RegexCaseInsensitiveTest, EscapedCharIsCaseFolded) {
  // \x41 == 'A'. With /i this matches both 'A' and 'a'.
  expectMatchAllEngines<"\\x41", Flags::CaseInsensitive>("A", true);
  expectMatchAllEngines<"\\x41", Flags::CaseInsensitive>("a", true);
  expectMatchAllEngines<"\\x41">("a", false);
}

TEST(RegexCaseInsensitiveTest, MultilineCombinedWithCaseInsensitive) {
  expectMatchAllEngines<"a", Flags::CaseInsensitive | Flags::Multiline>(
      "A", true);
}

TEST(RegexCaseInsensitiveTest, DotAllCombinedWithCaseInsensitive) {
  // a.b with DotAll matches A\nB with case-insensitive.
  expectMatchAllEngines<"a.b", Flags::CaseInsensitive | Flags::DotAll>(
      std::string_view("A\nB", 3), true);
}
