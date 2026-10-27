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

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstOptimizer.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex::detail;

// Helper: parse and optimize a pattern using AstBuilder (the mutable form),
// then compact into a ParseResult. This mirrors the compile<>() pipeline in
// Regex.h, which runs optimization on a mutable AstBuilder and only
// produces the fixed-size ParseResult after optimization completes.
//
// Tests that need to inspect the post-optimization AST should use this
// helper instead of calling optimizeAst() on a ParseResult, since
// ParseResult is meant to be immutable.
template <
    std::size_t PatLen,
    ParseErrorMode Mode = ParseErrorMode::RuntimeReport>
constexpr auto parseAndOptimize(std::string_view pattern) {
  AstBuilder builder;
  builder.valid = true;
  Parser parser(pattern, builder);
  builder.root = parser.parseRegex(folly::regex::Flags::None);

  if (builder.valid && !parser.atEnd()) {
    parser.error("Unexpected character after pattern");
  }

  if constexpr (Mode == ParseErrorMode::CompileTime) {
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
  }

  if (builder.valid) {
    assignProbeIds(builder, builder.root);
    optimizeAst(builder, builder.root, false);
  }

  markUnreachableNodes(builder);

  return compact<ParseSizes{
      (PatLen < 4 ? 8 : PatLen * 2),
      (PatLen < 4 ? 16 : PatLen * 4),
      (PatLen < 4 ? 4 : PatLen),
      0,
      PatLen,
      0,
      0}>(builder);
}

template <ParseErrorMode Mode = ParseErrorMode::RuntimeReport, std::size_t N>
constexpr auto parseAndOptimize(const char (&pattern)[N]) {
  return parseAndOptimize<N - 1, Mode>(std::string_view(pattern, N - 1));
}

// static_assert tests for compile-time parsing correctness

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("abc");
  return r.valid && r.group_count == 0 && r.nfa_compatible;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("(a)(b)");
  return r.valid && r.group_count == 2;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("(?:ab)");
  return r.valid && r.group_count == 0;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("a|b|c");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("a{2,4}");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("\\d+");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[a-z]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[-abc]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[abc-]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("^start$");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>(".");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("");
  return r.valid;
}());

class ParserTest : public ::testing::Test {};

TEST_F(ParserTest, SimpleLiteral) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("hello");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 0);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, CaptureGroups) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(a)(b)(c)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 3);
}

TEST_F(ParserTest, NonCaptureGroup) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?:abc)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 0);
}

TEST_F(ParserTest, MixedGroups) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(a)(?:b)(c)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 2);
}

TEST_F(ParserTest, NestedGroups) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("((a)(b))");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 3);
}

TEST_F(ParserTest, Alternation) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("cat|dog");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassBasic) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassRange) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("[a-z0-9]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassNegated) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[^abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassLeadingDash) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[-abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassTrailingDash) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc-]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierStar) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a*");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierPlus) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a+");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierQuestion) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a?");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCounted) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{2,4}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCountedExact) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{3}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCountedOpenEnd) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{2,}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, LazyQuantifiers) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("a*?b+?c??");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ShorthandClasses) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\d\\w\\s");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, NegatedShorthandClasses) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\D\\W\\S");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, Anchors) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("^hello$");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EscapeSequences) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\n\\r\\t");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EscapedSpecialChars) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\(\\)\\[\\]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, AnyChar) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a.b");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EmptyPattern) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ComplexPattern) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(\\d+)\\.(\\d+)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 2);
}

// Parse error tests using RuntimeReport mode

TEST_F(ParserTest, ErrorUnmatchedOpenParen) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorUnmatchedCloseParen) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc)");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorTrailingBackslash) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc\\");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, IdentityEscape) {
  // Unknown escapes like \q are treated as identity escapes (literal 'q'),
  // matching PCRE/Perl behavior.
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\q");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ErrorUnmatchedBracket) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorQuantifierWithoutElement) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("*abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorQuantifierPlusWithoutElement) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("+");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorCountedRepetitionMinGreaterThanMax) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{3,2}");
  EXPECT_FALSE(result.valid);
}

// Unescaped '{' that does not form a valid {n,m} quantifier should be treated
// as a literal character, matching RE2/PCRE2/Python behavior.

TEST_F(ParserTest, UnescapedBraceAsLiteral) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{b}c");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, UnescapedBraceAtEnd) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, UnescapedEmptyBraces) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{}b");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, UnescapedBraceNoClosing) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{3,5");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, UnescapedBraceAlone) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("{");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, UnescapedBracesInText) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("{error_code: (\\d+)}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ErrorUnicodePropertyEscape) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\p{Lu}");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorNegatedUnicodePropertyEscape) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("\\P{Ll}");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorUnicodePropertyInCharClass) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("[\\p{Lu}]");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, NfaCompatibleFlag) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(a+)(b*)");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

// Nested quantifier flattening: patterns that become backtrack_safe.
// parse() alone does not run the optimizer; we must use parseAndOptimize()
// to mirror the compile<>() pipeline. Optimization runs on the mutable
// AstBuilder; the result is then compacted into a ParseResult. These tests
// run at runtime because the optimizer exceeds the constexpr step limit
// for the default ParseResult size.

TEST_F(ParserTest, NestedQuantifierFlattenLiteral) {
  auto result = parseAndOptimize("(a+)+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenStandalone) {
  auto result = parseAndOptimize("(a+)+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenCharClass) {
  auto result = parseAndOptimize("([abc]+)+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenAnyChar) {
  auto result = parseAndOptimize("(.+)+x");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// Nested quantifier patterns that should NOT be flattened

TEST_F(ParserTest, NestedQuantifierFlattenInnerMinZero) {
  // (a*)+b → (a*)b — outer + dropped, inner a* preserved
  auto result = parseAndOptimize("(a*)+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenOuterMinZero) {
  // (a+)*b → (a*)b — inner min adjusted from 1→0, outer * dropped
  auto result = parseAndOptimize("(a+)*b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantCapturingStarFlattened) {
  // (a*)* with capturing group — flattened to (a*), backtrack_safe
  auto result = parseAndOptimize("(a*)*b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantCapturingPlusStarFlattened) {
  // (a+)* with capturing — flattened to (a*), backtrack_safe
  auto result = parseAndOptimize("(a+)*b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenOuterBounded) {
  // (a+){2,5}b — bounded outer, cannot flatten (changes capture semantics)
  auto result = parseAndOptimize("(a+){2,5}b");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenSequenceChild) {
  auto result = parseAndOptimize("(a+b)+");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenMixedGreediness) {
  auto result = parseAndOptimize("(a+)+?b");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

// ===== Alternation-to-Optional (Empty branch simplification) =====

TEST_F(ParserTest, AlternationToOptionalSimple) {
  // (?:|b) should be converted to b?? (lazy optional)
  auto result = parseAndOptimize("(?:|b)c");
  EXPECT_TRUE(result.valid);
  // After conversion: b??c — Repeat{0,1} over Literal is backtrack_safe
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalReversed) {
  // (?:b|) should be converted to b? (greedy optional)
  auto result = parseAndOptimize("(?:b|)c");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalCharClass) {
  // (?:|[abc]) should become [abc]?? (lazy)
  auto result = parseAndOptimize("(?:|[abc])d");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalFromPrefixFactor) {
  // (a|ab) gets prefix-factored to a(?:|b), then (?:|b) → b??
  // Result: a followed by b?? — no outer Repeat, so backtrack_safe
  auto result = parseAndOptimize("(a|ab)c");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalNotSequenceBranch) {
  // (?:|ab) — non-empty branch is a Sequence. Phase 6 now allows
  // simplifyEmptyAlternation to convert it to (?:ab)?? → backtrack_safe.
  auto result = parseAndOptimize("(?:|ab)c");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalWithOuterRepeat) {
  // (a|ab)+c — outer Repeat{1,∞} wraps Group(cap) containing Sequence.
  // The inner alternation gets prefix-factored and simplified, but
  // computeBacktrackSafe sees Repeat over Group (not Literal/CharClass)
  // so backtrack_safe remains false.
  auto result = parseAndOptimize("(a|ab)+c");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalThreeBranchMerge) {
  // (?:a|b|)c — mergeCharBranches merges a|b into [ab], leaving (?:[ab]|).
  // Then simplifyEmptyAlternation converts to [ab]?c → backtrack_safe.
  auto result = parseAndOptimize("(?:a|b|)c");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalBothEmpty) {
  // (?:|) — both branches are empty, should NOT be converted
  auto result = parseAndOptimize("(?:|)c");
  EXPECT_TRUE(result.valid);
}

// ===== Phase 1: Generalized nested quantifier table =====

TEST_F(ParserTest, GeneralizedNestedQuantStarStar) {
  // (?:a*)* → a* — backtrack_safe
  auto result = parseAndOptimize("(?:a*)*b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantStarPlus) {
  // (?:a*)+  → a* — backtrack_safe
  // (inner min=0, outer min=1 → combined min=0)
  auto result = parseAndOptimize("(?:a*)+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantPlusStar) {
  // (?:a+)* → a* — backtrack_safe (non-capturing only)
  auto result = parseAndOptimize("(?:a+)*b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 2: Groups transparent to computeBacktrackSafe =====

TEST_F(ParserTest, GroupTransparentToBacktrackSafe) {
  // (a)+ — capturing group wrapping Literal inside Repeat → backtrack_safe
  auto result = parseAndOptimize("(a)+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GroupTransparentCharClass) {
  // ([a-z])+ — capturing group wrapping CharClass → backtrack_safe
  auto result = parseAndOptimize("([a-z])+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GroupTransparentNotForSequence) {
  // (ab)+ — Group wrapping Sequence of Literals. Phase 2 unwraps the Group,
  // Phase 5 recognizes the fixed-length char-test Sequence → backtrack_safe.
  auto result = parseAndOptimize("(ab)+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 3: Adjacent repeat merging =====

TEST_F(ParserTest, AdjacentRepeatMerge) {
  // a+a+ → a{2,∞} — the merged repeat wraps a Literal → backtrack_safe
  auto result = parseAndOptimize("a+a+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AdjacentRepeatMergeDifferentChars) {
  // a+b+ — different inner expressions, NOT merged
  auto result = parseAndOptimize("a+b+c");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 4: Single-char CharClass to Literal =====

TEST_F(ParserTest, SingleCharClassToLiteral) {
  // [a]+ → a+ — backtrack_safe
  auto result = parseAndOptimize("[a]+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, SingleCharClassNegatedNotConverted) {
  // [^a] — complement ranges [\x00-\x60\x62-\xff], NOT converted to Literal
  // because it has 2 ranges (range_count != 1)
  auto result = parseAndOptimize("[^a]+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe); // Still safe — CharClass is a char test
}

TEST_F(ParserTest, MultiCharClassNotConverted) {
  // [a-z] — range, NOT converted (lo != hi)
  auto result = parseAndOptimize("[a-z]+b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe); // Still safe — CharClass is a char test
}

// ===== Phase 5: Fixed-length sequence inside Repeat → backtrack safe =====

TEST_F(ParserTest, FixedLengthSequenceBacktrackSafe) {
  // (?:ab)+ — Repeat over Sequence of Literals → backtrack_safe
  auto result = parseAndOptimize("(?:ab)+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, FixedLengthSequenceWithCharClass) {
  // (?:a[0-9])+ — Sequence of Literal and CharClass → backtrack_safe
  auto result = parseAndOptimize("(?:a[0-9])+");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, SequenceWithRepeatNotFixed) {
  // (?:a+b)+ — Sequence contains a Repeat → NOT fixed-length → NOT safe
  auto result = parseAndOptimize("(?:a+b)+");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

// ===== Phase 6: Allow Sequence branches in simplifyEmptyAlternation =====

TEST_F(ParserTest, EmptyAlternationWithSequenceBranch) {
  // (?:|ab)c — now converted to (?:ab)??c
  auto result = parseAndOptimize("(?:|ab)c");
  EXPECT_TRUE(result.valid);
  // After conversion: (?:ab)?? wrapping a Sequence → Repeat{0,1}(Sequence)
  // Phase 5 fixed-length check → all CharTests → backtrack_safe
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== CharClass set operations (Phase 1 optimizations) =====

TEST_F(ParserTest, CharRangesDisjointBasic) {
  // [a-z]+[0-9] — disjoint char classes, should be possessified
  auto result = parseAndOptimize("[a-z]+[0-9]");
  EXPECT_TRUE(result.valid);
}

// ===== Phase 2: Possessive inference =====

TEST_F(ParserTest, PossessiveInferenceDisjoint) {
  // [a-z]+[0-9] — possessive inference should fire (disjoint classes)
  // Verify by checking the pattern still matches correctly after optimization
  auto result = parseAndOptimize("[a-z]+[0-9]");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, PossessiveInferenceNotDisjoint) {
  // [a-z]+[a-z] — char classes overlap, possessive inference does not fire,
  // but adjacent repeat merge converts to [a-z]{2,} → backtrack_safe
  auto result = parseAndOptimize("[a-z]+[a-z]");
  EXPECT_TRUE(result.valid);
  // Adjacent repeat merge → [a-z]{2,} → backtrack_safe via tight loop
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 3: Branch subsumption =====

TEST_F(ParserTest, BranchSubsumptionWordDigit) {
  // (?:\w+|\d+)+z — after subsumption \d ⊆ \w, branch eliminated
  auto result = parseAndOptimize("(?:\\w+|\\d+)+z");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, BranchSubsumptionLowerAlpha) {
  // (?:[a-z]+|[a-m]+) — [a-m] ⊆ [a-z], branch eliminated
  auto result = parseAndOptimize("(?:[a-z]+|[a-m]+)x");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, BranchSubsumptionNoSubset) {
  // (?:[a-m]+|[n-z]+) — NOT subset, branches not eliminated by subsumption.
  // However, the optimizer makes this backtrack_safe through other passes
  // (e.g., char class merging of contiguous disjoint ranges, possessive
  // inference since both ranges are disjoint from the trailing literal 'x').
  auto result = parseAndOptimize("(?:[a-m]+|[n-z]+)x");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== POSIX Character Class Parser Tests =====

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:alpha:]]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:digit:]]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:^alpha:]]");
  return r.valid;
}());

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:alpha:][:digit:]]");
  return r.valid;
}());

static_assert([] {
  // Unknown POSIX class name should produce a parse error
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:unknown:]]");
  return !r.valid;
}());

static_assert([] {
  // [: without closing :] is treated as literal characters (no parse error)
  auto r = parse<ParseErrorMode::RuntimeReport>("[[:abc]");
  return r.valid;
}());

TEST_F(ParserTest, PosixClassUnknownNameError) {
  auto result = parse<ParseErrorMode::RuntimeReport>("[[:unknown:]]");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, PosixClassIncompleteIsLiteral) {
  // [: without closing :] — treated as literal [ and :
  auto result = parse<ParseErrorMode::RuntimeReport>("[[:abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, PosixClassAlphaParsesSuccessfully) {
  auto result = parse<ParseErrorMode::RuntimeReport>("[[:alpha:]]");
  EXPECT_TRUE(result.valid);
}

// Lookaround parser tests

TEST_F(ParserTest, PositiveLookahead) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a(?=b)");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, NegativeLookahead) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a(?!b)");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, PositiveLookbehind) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?<=a)b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, NegativeLookbehind) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?<!a)b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, LookbehindFixedWidth) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?<=abc)d");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, LookbehindVariableWidth) {
  // Variable-width lookbehind is now supported (uses reverse probes)
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?<=a+)b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, AtomicGroup) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(?>a+)b");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, Backreference) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(a)\\1");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, ErrorBackrefToNonexistentGroup) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\1");
  EXPECT_FALSE(result.valid);
}
