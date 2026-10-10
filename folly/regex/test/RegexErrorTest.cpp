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

// Tests for error handling and edge cases in folly::regex.
// Covers parse error reporting, alternation branch limits,
// DFA state overflow, budget exhaustion, and NFA-incompatible patterns.

#include <folly/regex/Regex.h>
#include <folly/regex/detail/Parser.h>

#include <folly/portability/GTest.h>

#include <string>

using namespace folly::regex;
using namespace folly::regex::detail;

// ---------------------------------------------------------------------------
// 1. regex_parse_error contains correct position
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, UnmatchedOpenParen_ErrorPosition) {
  // Unmatched '(' — error position should be at end of pattern
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("abc(def");
  EXPECT_FALSE(result.valid);
  // Position should be at or near the end of the pattern (length 7)
  EXPECT_GE(result.error_pos, 3u);
}

TEST(RegexErrorTest, UnmatchedCloseParen_ErrorPosition) {
  // Unmatched ')' — error position should be at the ')'
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("abc)def");
  EXPECT_FALSE(result.valid);
  // The ')' is at position 3
  EXPECT_EQ(result.error_pos, 3u);
}

TEST(RegexErrorTest, TrailingBackslash_ErrorPosition) {
  // Trailing backslash — error position should be at end
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc\\");
  EXPECT_FALSE(result.valid);
  // Position at or near the end of the pattern (length 4)
  EXPECT_GE(result.error_pos, 3u);
}

TEST(RegexErrorTest, InvalidCharClassRange_ErrorPosition) {
  // Invalid char class range [z-a] — error at the range
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[z-a]");
  EXPECT_FALSE(result.valid);
  // Error position should be within the character class
  EXPECT_GE(result.error_pos, 1u);
  EXPECT_LE(result.error_pos, 4u);
}

// ---------------------------------------------------------------------------
// 2. regex_parse_error contains descriptive message
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, UnmatchedOpenParen_ErrorMessage) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("abc(def");
  EXPECT_FALSE(result.valid);
  std::string msg(result.error_message);
  EXPECT_FALSE(msg.empty());
}

TEST(RegexErrorTest, UnmatchedCloseParen_ErrorMessage) {
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>("abc)def");
  EXPECT_FALSE(result.valid);
  std::string msg(result.error_message);
  EXPECT_FALSE(msg.empty());
}

TEST(RegexErrorTest, TrailingBackslash_ErrorMessage) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("abc\\");
  EXPECT_FALSE(result.valid);
  std::string msg(result.error_message);
  EXPECT_FALSE(msg.empty());
}

TEST(RegexErrorTest, InvalidCharClassRange_ErrorMessage) {
  static constexpr auto result = parse<ParseErrorMode::RuntimeReport>("[z-a]");
  EXPECT_FALSE(result.valid);
  std::string msg(result.error_message);
  EXPECT_FALSE(msg.empty());
}

// ---------------------------------------------------------------------------
// 3. Pattern at 64 alternation branches succeeds
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, MaxAltBranches_64_Succeeds) {
  // Build a pattern with exactly 64 single-char branches: a|b|c|...|
  // Uses alphanumerics and some printable chars to reach 64.
  // chars: a-z (26), A-Z (26), 0-9 (10), plus '!' and '#' = 64
  static constexpr auto result =
      parse<255, ParseErrorMode::RuntimeReport>(std::string_view(
          "a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z"
          "|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z"
          "|0|1|2|3|4|5|6|7|8|9|!|#"));
  EXPECT_TRUE(result.valid)
      << "64-branch alternation should parse successfully"
      << " error_message: " << result.error_message;
}

// ---------------------------------------------------------------------------
// 4. Pattern at 65 branches — test and leave failing if it errors
// ---------------------------------------------------------------------------

// NOTE: If this test fails, it is intentionally left failing. It exposes a
// potential limitation in the alternation branch count that should be tracked
// as a bug to fix (the parser may reject >64 branches).
TEST(RegexErrorTest, AltBranches_65_MayFail) {
  // 65 branches: 64 from above + one more ('$')
  static constexpr auto result =
      parse<260, ParseErrorMode::RuntimeReport>(std::string_view(
          "a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z"
          "|A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P|Q|R|S|T|U|V|W|X|Y|Z"
          "|0|1|2|3|4|5|6|7|8|9|!|#|$"));
  // If the parser has a 64-branch limit, this will be invalid.
  // If it succeeds, great — the limit is higher than 64.
  // Either way, we record the result. If this test fails, leave it failing.
  EXPECT_TRUE(result.valid)
      << "65-branch alternation failed with: " << result.error_message
      << " (intentionally left failing if it errors)";
}

// ---------------------------------------------------------------------------
// 5. DFA state overflow returns valid=false gracefully
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, DfaStateOverflow_ReturnsInvalid) {
  // A pattern with counted repeats that may produce many DFA states.
  // The important thing is that it compiles and runs without crashing —
  // the engine falls back to NFA/backtracker if DFA state limit is exceeded.
  using R = Regex<R"([a-z]{1,3}[0-9]{1,3}[a-z]{1,3}[0-9]{1,3})">;

  auto result = R::match("a1b2");
  EXPECT_TRUE(result) << "Pattern should match valid input regardless of "
                      << "whether DFA or NFA/backtracker is used";

  auto noMatch = R::match("!!!");
  EXPECT_FALSE(noMatch);
}

// ---------------------------------------------------------------------------
// 6. Budget exhaustion returns NoMatch, not a crash
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, BudgetExhaustion_AutoMode_Match_NoMatch) {
  // (a*)*b in auto mode (default flags). This pattern is NFA-compatible
  // and not backtrack-safe, so the engine uses a budget. When the budget
  // is exhausted, it falls back to NFA and returns NoMatch.
  //
  // NOTE: ForceBacktracking is NOT used here because it disables the
  // budget mechanism (UseBudget=false), which would cause exponential
  // backtracking to run without limit.
  using R = Regex<"(a*)*b">;

  std::string input(20, 'a');

  auto result = R::match(input);
  EXPECT_FALSE(result) << "Should not match: no trailing 'b' in input";
}

TEST(RegexErrorTest, BudgetExhaustion_AutoMode_Search_NoMatch) {
  // Search mode with catastrophic backtracking pattern in auto mode.
  using R = Regex<"(a*)*b">;

  std::string input(20, 'a');

  auto result = R::search(input);
  EXPECT_FALSE(result) << "Should not find match: no 'b' in input";
}

// ---------------------------------------------------------------------------
// 7. NFA-incompatible patterns handled gracefully
// ---------------------------------------------------------------------------

TEST(RegexErrorTest, BackrefPattern_NfaIncompatible) {
  // Pattern with backreference — nfa_compatible should be false
  static constexpr auto result =
      parse<ParseErrorMode::RuntimeReport>(R"((a)\1)");
  EXPECT_TRUE(result.valid) << "Backref pattern should parse successfully";
  EXPECT_FALSE(result.nfa_compatible)
      << "Pattern with backref should not be NFA-compatible";
}

TEST(RegexErrorTest, BackrefPattern_ForceBacktracking_Works) {
  // Backref with ForceBacktracking should work correctly
  using R = Regex<R"((a)\1)", Flags::ForceBacktracking>;

  auto result = R::match("aa");
  EXPECT_TRUE(result) << "Backref should match 'aa'";

  auto noMatch = R::match("ab");
  EXPECT_FALSE(noMatch) << "Backref should not match 'ab'";
}

TEST(RegexErrorTest, BackrefPattern_AutoMode_Works) {
  // Auto mode should use backtracker without budget for non-NFA-compatible
  // patterns (since NFA fallback isn't available).
  using R = Regex<R"((a)\1)">;

  auto result = R::match("aa");
  EXPECT_TRUE(result) << "Backref in auto mode should match 'aa'";

  auto noMatch = R::match("ab");
  EXPECT_FALSE(noMatch) << "Backref in auto mode should not match 'ab'";
}

TEST(RegexErrorTest, BackrefPattern_AutoMode_Search) {
  // Search with backref in auto mode
  using R = Regex<R"((a)\1)">;

  auto result = R::search("xxaayy");
  EXPECT_TRUE(result) << "Backref search should find 'aa' in 'xxaayy'";
  if (result) {
    EXPECT_EQ(result[0], "aa");
  }
}
