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

// Tests that verify NFA/DFA compilation limits and provide baseline
// measurements for the interval partition optimization. Each test records
// the NFA state count and whether DFA construction succeeded.
//
// Patterns that exceed the current constexpr budget are tested via the
// parser/NFA builder directly (without DFA construction) to record their
// NFA state counts for before/after comparison.

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Nfa.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

using namespace folly::regex;
using namespace folly::regex::detail;

// Helper to get NFA state count without attempting DFA construction.
// This always succeeds regardless of pattern complexity.
template <FixedPattern Pat>
struct NfaInfo {
  static constexpr auto parsed_ = [] {
    auto ast = parse<ParseErrorMode::CompileTime>(
        std::string_view{Pat.data, Pat.length()});
    if (ast.valid) {
      ast.root = optimizeAst(ast, ast.root);
    }
    return ast;
  }();

  static constexpr auto nfaProg_ = [] {
    if constexpr (parsed_.nfa_compatible) {
      return buildNfa<parsed_.node_count * 4 + 16>(parsed_);
    } else {
      return NfaProgram<1>{};
    }
  }();

  static constexpr int nfaStates = nfaProg_.state_count;
  static constexpr bool nfaCompatible = parsed_.nfa_compatible;

  // The DFA gate threshold from Regex.h (updated for interval optimization)
  static constexpr int maxDfaStates = !parsed_.nfa_compatible ? 1
      : nfaProg_.state_count > 64                             ? 0
      : nfaProg_.state_count <= 16                            ? 64
      : nfaProg_.state_count <= 32                            ? 64
      : nfaProg_.state_count <= 48
      ? 48
      : 32;

  static constexpr bool dfaAttempted = maxDfaStates > 0;
};

// Helper for patterns where Regex<> itself can compile (DFA succeeds)
template <FixedPattern Pat>
struct FullRegexInfo {
  using R = Regex<Pat>;
  static constexpr int nfaStates = R::nfaProg_.state_count;
  static constexpr bool dfaValid = R::dfaProg_.valid;
  static constexpr int maxDfaStates = R::kMaxDfaStates;
};

// === Simple patterns — always produce valid DFA ===

TEST(RegexComplexityTest, SimpleDigit) {
  using Info = FullRegexInfo<R"(\d+)">;
  EXPECT_TRUE(Info::dfaValid);
  EXPECT_LT(Info::nfaStates, 12);
}

TEST(RegexComplexityTest, SimpleLiteral) {
  using Info = FullRegexInfo<"hello">;
  EXPECT_TRUE(Info::dfaValid);
  EXPECT_LT(Info::nfaStates, 12);
}

TEST(RegexComplexityTest, SimpleCharClass) {
  using Info = FullRegexInfo<"[a-zA-Z0-9]+">;
  EXPECT_TRUE(Info::dfaValid);
  EXPECT_LT(Info::nfaStates, 12);
}

TEST(RegexComplexityTest, SimpleAlternation) {
  using Info = FullRegexInfo<"cat|dog|bird">;
  EXPECT_TRUE(Info::dfaValid);
}

// === Medium patterns — produce valid DFA ===

TEST(RegexComplexityTest, MediumEmailLike) {
  using Info = FullRegexInfo<R"(\w+@\w+\.\w+)">;
  EXPECT_TRUE(Info::dfaValid);
  EXPECT_GT(Info::nfaStates, 10);
  EXPECT_LE(Info::nfaStates, 24);
}

TEST(RegexComplexityTest, MediumCountedRepetition) {
  using Info = FullRegexInfo<R"([a-z]{3}\d{3})">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, MediumVersionPattern) {
  using Info = FullRegexInfo<R"(\d+\.\d+\.\d+)">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, MediumLongAlternation) {
  using Info = FullRegexInfo<"error|warning|info|debug|trace|fatal|critical">;
  // This alternation has many NFA states — DFA may or may not succeed
  // depending on constexpr budget. Record the result.
  EXPECT_GT(Info::nfaStates, 10);
}

// === Complex patterns — near or at the DFA gate ===

TEST(RegexComplexityTest, ComplexIPLike) {
  using Info = NfaInfo<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">;
  EXPECT_TRUE(Info::nfaCompatible);
  // CountedRepeat optimization reduces NFA states significantly
  EXPECT_GT(Info::nfaStates, 8);
}

TEST(RegexComplexityTest, ComplexDatePattern) {
  using Info = NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">;
  EXPECT_TRUE(Info::nfaCompatible);
  EXPECT_GT(Info::nfaStates, 10);
}

// === Very complex patterns — record NFA state counts via NfaInfo ===
// These patterns may exceed the current constexpr budget for DFA
// construction. After the interval optimization, some of these
// should become DFA-constructible.

TEST(RegexComplexityTest, VeryComplexEmailFull) {
  using Info = NfaInfo<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">;
  EXPECT_TRUE(Info::nfaCompatible);
  EXPECT_GT(Info::nfaStates, 8);
}

TEST(RegexComplexityTest, VeryComplexLogPattern) {
  using Info = NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">;
  EXPECT_TRUE(Info::nfaCompatible);
  EXPECT_GT(Info::nfaStates, 15);
}

// === DFA limit stress tests ===
// These use FullRegexInfo which triggers actual DFA construction.
// Alternations produce the most NFA states since they aren't compressed
// by the CountedRepeat optimization.

TEST(RegexComplexityTest, DfaStress_LongAlternation) {
  // 7 branches of 5-8 chars each — significant NFA state count
  using Info = FullRegexInfo<"error|warning|info|debug|trace|fatal|critical">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, DfaStress_VeryLongAlternation) {
  // 10 branches — even more NFA states
  using Info = FullRegexInfo<
      "alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, DfaStress_MixedComplex) {
  // Alternation + char classes + counted repeats
  using Info = FullRegexInfo<R"(GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS)">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, DfaStress_UUIDFullRegex) {
  using Info = FullRegexInfo<
      "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, DfaStress_DateTimeLog) {
  using Info = FullRegexInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">;
  EXPECT_TRUE(Info::dfaValid);
}

TEST(RegexComplexityTest, DfaStress_IPAddress) {
  using Info = FullRegexInfo<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">;
  EXPECT_TRUE(Info::dfaValid);
}

// === Report all metrics ===

TEST(RegexComplexityTest, ReportBaseline) {
  printf("\n=== NFA/DFA Complexity Baseline ===\n");
  printf("%-50s  NFA  MaxDFA  DFA-attempted\n", "Pattern");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\d+)",
      NfaInfo<R"(\d+)">::nfaStates,
      NfaInfo<R"(\d+)">::maxDfaStates,
      NfaInfo<R"(\d+)">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      "hello",
      NfaInfo<"hello">::nfaStates,
      NfaInfo<"hello">::maxDfaStates,
      NfaInfo<"hello">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      "[a-zA-Z0-9]+",
      NfaInfo<"[a-zA-Z0-9]+">::nfaStates,
      NfaInfo<"[a-zA-Z0-9]+">::maxDfaStates,
      NfaInfo<"[a-zA-Z0-9]+">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\w+@\w+\.\w+)",
      NfaInfo<R"(\w+@\w+\.\w+)">::nfaStates,
      NfaInfo<R"(\w+@\w+\.\w+)">::maxDfaStates,
      NfaInfo<R"(\w+@\w+\.\w+)">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"([a-z]{3}\d{3})",
      NfaInfo<R"([a-z]{3}\d{3})">::nfaStates,
      NfaInfo<R"([a-z]{3}\d{3})">::maxDfaStates,
      NfaInfo<R"([a-z]{3}\d{3})">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\d+\.\d+\.\d+)",
      NfaInfo<R"(\d+\.\d+\.\d+)">::nfaStates,
      NfaInfo<R"(\d+\.\d+\.\d+)">::maxDfaStates,
      NfaInfo<R"(\d+\.\d+\.\d+)">::dfaAttempted ? "yes" : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})",
      NfaInfo<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">::nfaStates,
      NfaInfo<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">::maxDfaStates,
      NfaInfo<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">::dfaAttempted
          ? "yes"
          : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})",
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">::nfaStates,
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">::maxDfaStates,
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">::dfaAttempted
          ? "yes"
          : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})",
      NfaInfo<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">::nfaStates,
      NfaInfo<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">::maxDfaStates,
      NfaInfo<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">::dfaAttempted
          ? "yes"
          : "no");

  printf(
      "%-50s  %3d  %6d  %s\n",
      R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])",
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">::nfaStates,
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">::maxDfaStates,
      NfaInfo<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">::dfaAttempted
          ? "yes"
          : "no");

  // Very complex patterns to stress-test the limits
  printf(
      "%-50s  %3d  %6d  %s\n",
      "error|warning|info|debug|trace|fatal|critical",
      NfaInfo<"error|warning|info|debug|trace|fatal|critical">::nfaStates,
      NfaInfo<"error|warning|info|debug|trace|fatal|critical">::maxDfaStates,
      NfaInfo<"error|warning|info|debug|trace|fatal|critical">::dfaAttempted
          ? "yes"
          : "no");

  // UUID-like pattern (no capture groups)
  printf(
      "%-50s  %3d  %6d  %s\n",
      "[0-9a-f]{8}-[0-9a-f]{4}-...-[0-9a-f]{12}",
      NfaInfo<"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}">::
          nfaStates,
      NfaInfo<"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}">::
          maxDfaStates,
      NfaInfo<"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}">::
              dfaAttempted
          ? "yes"
          : "no");

  printf("=== End Baseline ===\n\n");
}
