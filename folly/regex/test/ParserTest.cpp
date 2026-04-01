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

// static_assert tests for compile-time parsing correctness

static_assert([] {
  auto r = parse<ParseErrorMode::RuntimeReport>("abc");
  return r.valid && r.group_count == 0;
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
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("hello");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 0);
  EXPECT_TRUE(result.nfa_compatible);
}

TEST_F(ParserTest, CaptureGroups) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(a)(b)(c)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 3);
}

TEST_F(ParserTest, NonCaptureGroup) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(?:abc)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 0);
}

TEST_F(ParserTest, MixedGroups) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(a)(?:b)(c)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 2);
}

TEST_F(ParserTest, NestedGroups) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("((a)(b))");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 3);
}

TEST_F(ParserTest, Alternation) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("cat|dog");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassBasic) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassRange) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[a-z0-9]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassNegated) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[^abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassLeadingDash) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[-abc]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, CharClassTrailingDash) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc-]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierStar) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a*");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierPlus) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a+");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierQuestion) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a?");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCounted) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{2,4}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCountedExact) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{3}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, QuantifierCountedOpenEnd) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{2,}");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, LazyQuantifiers) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a*?b+?c??");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ShorthandClasses) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\d\\w\\s");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, NegatedShorthandClasses) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\D\\W\\S");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, Anchors) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("^hello$");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EscapeSequences) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\n\\r\\t");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EscapedSpecialChars) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\(\\)\\[\\]");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, AnyChar) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a.b");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, EmptyPattern) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ComplexPattern) {
  constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("(\\d+)\\.(\\d+)");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.group_count, 2);
}

// Parse error tests using RuntimeReport mode

TEST_F(ParserTest, ErrorUnmatchedOpenParen) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorUnmatchedCloseParen) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc)");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorTrailingBackslash) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc\\");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, IdentityEscape) {
  // Unknown escapes like \q are treated as identity escapes (literal 'q'),
  // matching PCRE/Perl behavior.
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\q");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ErrorUnmatchedBracket) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorQuantifierWithoutElement) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("*abc");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorQuantifierPlusWithoutElement) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("+");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, ErrorCountedRepetitionMinGreaterThanMax) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a{3,2}");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, NfaCompatibleFlag) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(a+)(b*)");
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.nfa_compatible);
}

// Lookaround parser tests

TEST_F(ParserTest, PositiveLookahead) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a(?=b)");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, NegativeLookahead) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("a(?!b)");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, PositiveLookbehind) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(?<=a)b");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, NegativeLookbehind) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(?<!a)b");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, LookbehindFixedWidth) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(?<=abc)d");
  EXPECT_TRUE(result.valid);
}

TEST_F(ParserTest, ErrorLookbehindVariableWidth) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(?<=a+)b");
  EXPECT_FALSE(result.valid);
}

TEST_F(ParserTest, Backreference) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("(a)\\1");
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.nfa_compatible);
}

TEST_F(ParserTest, ErrorBackrefToNonexistentGroup) {
  constexpr auto result = parse<ParseErrorMode::RuntimeReport>("\\1");
  EXPECT_FALSE(result.valid);
}

// Nested quantifier flattening: patterns that become backtrack_safe
// parse() alone does not run the optimizer; we must call optimizeAst()
// to mirror the compile<>() pipeline.  These run at runtime because the
// optimizer exceeds the constexpr step limit for the default ParseResult size.

TEST_F(ParserTest, NestedQuantifierFlattenLiteral) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+)+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenStandalone) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenCharClass) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("([abc]+)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierFlattenAnyChar) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(.+)+x");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// Nested quantifier patterns that should NOT be flattened

TEST_F(ParserTest, NestedQuantifierNoFlattenInnerMinZero) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a*)+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenOuterMinZero) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+)*b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenOuterMinTwo) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+){2,5}b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenSequenceChild) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+b)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, NestedQuantifierNoFlattenMixedGreediness) {
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+)+?b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

// ===== Alternation-to-Optional (Empty branch simplification) =====

TEST_F(ParserTest, AlternationToOptionalSimple) {
  // (?:|b) should be converted to b?? (lazy optional)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:|b)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  // After conversion: b??c — Repeat{0,1} over Literal is backtrack_safe
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalReversed) {
  // (?:b|) should be converted to b? (greedy optional)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:b|)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalCharClass) {
  // (?:|[abc]) should become [abc]?? (lazy)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:|[abc])d");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalFromPrefixFactor) {
  // (a|ab) gets prefix-factored to a(?:|b), then (?:|b) → b??
  // Result: a followed by b?? — no outer Repeat, so backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a|ab)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalNotSequenceBranch) {
  // (?:|ab) — non-empty branch is a Sequence. Phase 6 now allows
  // simplifyEmptyAlternation to convert it to (?:ab)?? → backtrack_safe.
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:|ab)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalWithOuterRepeat) {
  // (a|ab)+c — outer Repeat{1,∞} wraps Group(cap) containing Sequence.
  // The inner alternation gets prefix-factored and simplified, but
  // computeBacktrackSafe sees Repeat over Group (not Literal/CharClass)
  // so backtrack_safe remains false.
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a|ab)+c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalThreeBranchMerge) {
  // (?:a|b|)c — mergeCharBranches merges a|b into [ab], leaving (?:[ab]|).
  // Then simplifyEmptyAlternation converts to [ab]?c → backtrack_safe.
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a|b|)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AlternationToOptionalBothEmpty) {
  // (?:|) — both branches are empty, should NOT be converted
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:|)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
}

// ===== Phase 1: Generalized nested quantifier table =====

TEST_F(ParserTest, GeneralizedNestedQuantStarStar) {
  // (?:a*)* → a* — backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a*)*b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantStarPlus) {
  // (?:a*)+  → a* — backtrack_safe
  // (inner min=0, outer min=1 → combined min=0)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a*)+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantPlusStar) {
  // (?:a+)* → a* — backtrack_safe (non-capturing only)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a+)*b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantCapturingStarNotFlattened) {
  // (a*)* with capturing group — NOT flattened (capture semantics differ)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a*)*b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

TEST_F(ParserTest, GeneralizedNestedQuantCapturingPlusStarNotFlattened) {
  // (a+)* with capturing — NOT flattened
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a+)*b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

// ===== Phase 2: Groups transparent to computeBacktrackSafe =====

TEST_F(ParserTest, GroupTransparentToBacktrackSafe) {
  // (a)+ — capturing group wrapping Literal inside Repeat → backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(a)+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GroupTransparentCharClass) {
  // ([a-z])+ — capturing group wrapping CharClass → backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("([a-z])+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, GroupTransparentNotForSequence) {
  // (ab)+ — Group wrapping Sequence of Literals. Phase 2 unwraps the Group,
  // Phase 5 recognizes the fixed-length char-test Sequence → backtrack_safe.
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(ab)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 3: Adjacent repeat merging =====

TEST_F(ParserTest, AdjacentRepeatMerge) {
  // a+a+ → a{2,∞} — the merged repeat wraps a Literal → backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("a+a+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, AdjacentRepeatMergeDifferentChars) {
  // a+b+ — different inner expressions, NOT merged
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("a+b+c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

// ===== Phase 4: Single-char CharClass to Literal =====

TEST_F(ParserTest, SingleCharClassToLiteral) {
  // [a]+ → a+ — backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("[a]+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, SingleCharClassNegatedNotConverted) {
  // [^a] — negated, NOT converted to Literal
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("[^a]+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe); // Still safe — CharClass is a char test
}

TEST_F(ParserTest, MultiCharClassNotConverted) {
  // [a-z] — range, NOT converted (lo != hi)
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("[a-z]+b");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe); // Still safe — CharClass is a char test
}

// ===== Phase 5: Fixed-length sequence inside Repeat → backtrack safe =====

TEST_F(ParserTest, FixedLengthSequenceBacktrackSafe) {
  // (?:ab)+ — Repeat over Sequence of Literals → backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:ab)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, FixedLengthSequenceWithCharClass) {
  // (?:a[0-9])+ — Sequence of Literal and CharClass → backtrack_safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a[0-9])+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.backtrack_safe);
}

TEST_F(ParserTest, SequenceWithRepeatNotFixed) {
  // (?:a+b)+ — Sequence contains a Repeat → NOT fixed-length → NOT safe
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:a+b)+");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.backtrack_safe);
}

// ===== Phase 6: Allow Sequence branches in simplifyEmptyAlternation =====

TEST_F(ParserTest, EmptyAlternationWithSequenceBranch) {
  // (?:|ab)c — now converted to (?:ab)??c
  auto result = [] {
    auto ast = parse<ParseErrorMode::RuntimeReport>("(?:|ab)c");
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();
  EXPECT_TRUE(result.valid);
  // After conversion: (?:ab)?? wrapping a Sequence → Repeat{0,1}(Sequence)
  // Phase 5 fixed-length check → all CharTests → backtrack_safe
  EXPECT_TRUE(result.backtrack_safe);
}
