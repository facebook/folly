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

#include <type_traits>

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;

// ===== 1. Compilation Artifact Sharing =====
// ForceBacktracking and ForceNFA are execution-only flags stripped by
// compilationFlags(), so they share the same AstHolder (keyed on
// pattern + compilation flags).

static_assert(
    compilationFlags(Flags::ForceBacktracking) ==
        compilationFlags(Flags::ForceNFA),
    "Execution-only flags should produce identical compilation flags");

static_assert(
    &Regex<"abc", Flags::ForceBacktracking>::parsed_ ==
        &Regex<"abc", Flags::ForceNFA>::parsed_,
    "ForceBacktracking and ForceNFA should share the same AstHolder");

static_assert(
    &Regex<"abc", Flags::ForceBacktracking>::parsed_ ==
        &Regex<"abc", Flags::ForceDFA>::parsed_,
    "ForceBacktracking and ForceDFA should share the same AstHolder");

static_assert(
    &Regex<"abc">::parsed_ == &Regex<"abc", Flags::ForceBacktracking>::parsed_,
    "None and ForceBacktracking should share the same AstHolder");

// CaseInsensitive is a compilation flag, so it produces a different holder.
static_assert(
    compilationFlags(Flags::CaseInsensitive) != compilationFlags(Flags::None),
    "CaseInsensitive should differ from None in compilation flags");

// Different compilation flags produce different AstHolder types (and thus
// different parsed_ objects). The ParseResult types differ because the AST
// structure changes, so we verify via type identity.
static_assert(
    !std::is_same_v<
        typename Regex<"abc">::Ast,
        typename Regex<"abc", Flags::CaseInsensitive>::Ast>,
    "Different compilation flags must produce different AstHolder types");

static_assert(
    !std::is_same_v<
        typename Regex<"abc">::Ast,
        typename Regex<"abc", Flags::Multiline>::Ast>,
    "Multiline changes compilation flags and produces a different holder type");

// ===== 2. kNeedsAutomata Gate =====

// Backtrack-safe pattern in auto mode: uses only backtracker, no NFA/DFA.
static_assert(
    !Regex<"a+b">::kNeedsAutomata,
    "Backtrack-safe pattern in auto mode should not need automata");

static_assert(
    !Regex<"hello">::kNeedsAutomata,
    "Literal pattern should not need automata");

// ForceNFA always requires automata.
static_assert(
    Regex<"a+b", Flags::ForceNFA>::kNeedsAutomata,
    "ForceNFA should always set kNeedsAutomata");

// ForceDFA always requires automata.
static_assert(
    Regex<"a+b", Flags::ForceDFA>::kNeedsAutomata,
    "ForceDFA should always set kNeedsAutomata");

// Nested quantifiers like (a*)*b are optimized to (a*)b which is
// backtrack-safe, so automata are not needed.
static_assert(
    !Regex<"(a*)*b">::kNeedsAutomata,
    "Optimized nested-quantifier pattern should not need automata");

// Nested quantifiers like (a*)*b are optimized to (a*)b which is
// backtrack-safe, so automata are not needed.
static_assert(
    !Regex<"(a*)*b">::kNeedsAutomata,
    "Optimized nested-quantifier pattern should not need automata");

// Non-backtrack-safe, NFA-compatible pattern in auto mode: needs automata
// for fallback. The inner sequence (a*b) prevents nested quantifier
// flattening.
static_assert(
    Regex<"(a*b)*c">::kNeedsAutomata,
    "Non-backtrack-safe NFA-compatible pattern should need automata");
// kNeedsAutomata is false (forced backtracking is the only option).
static_assert(
    !Regex<"(a)\\1">::kNeedsAutomata,
    "Backref pattern (NFA-incompatible) should not set kNeedsAutomata");

// ForceBacktracking explicitly disables automata even for non-safe patterns.
static_assert(
    !Regex<"(a*)*b", Flags::ForceBacktracking>::kNeedsAutomata,
    "ForceBacktracking should never set kNeedsAutomata");

// ===== 3. ConstexprHolder Budget Isolation =====
// Two complex patterns in the same TU both compile successfully. Each
// gets an independent constexpr step budget via ConstexprHolder, so
// neither exhausts the other's budget.

TEST(RegexTemplateTest, ComplexPatternsCompileIndependently) {
  // UUID pattern
  using UuidRe =
      Regex<"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}">;
  // Date+time pattern
  using DateTimeRe = Regex<"\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}">;

  // Both compiled successfully and produce correct results.
  auto uuidMatch = UuidRe::match("550e8400-e29b-41d4-a716-446655440000");
  EXPECT_TRUE(uuidMatch);

  auto dtMatch = DateTimeRe::match("2024-01-15T10:30:00");
  EXPECT_TRUE(dtMatch);

  // Negative cases to confirm the patterns are not trivially accepting.
  EXPECT_FALSE(UuidRe::match("not-a-uuid"));
  EXPECT_FALSE(DateTimeRe::match("not-a-date"));
}

// ===== 4. Engine Selection Behavioral Verification =====

TEST(RegexTemplateTest, AutoModeBacktrackSafe) {
  // "a+b" is backtrack-safe → auto mode uses pure backtracker.
  // Verify correct results across match/search/test.
  EXPECT_TRUE(Regex<"a+b">::match("ab"));
  EXPECT_TRUE(Regex<"a+b">::match("aaab"));
  EXPECT_FALSE(Regex<"a+b">::match("b"));
  EXPECT_FALSE(Regex<"a+b">::match(""));

  auto result = Regex<"a+b">::search("xxaabxx");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "aab");

  EXPECT_TRUE(Regex<"a+b">::test("xxaabxx"));
  EXPECT_FALSE(Regex<"a+b">::test("xxxx"));
}

TEST(RegexTemplateTest, AutoModeNonSafeFallback) {
  // "(a*)*b" is NFA-compatible but not backtrack-safe. Auto mode starts
  // with budget-limited backtracking and may fall back to NFA. Regardless
  // of engine, the result must be correct.
  EXPECT_TRUE(Regex<"(a*)*b">::match("b"));
  EXPECT_TRUE(Regex<"(a*)*b">::match("ab"));
  EXPECT_TRUE(Regex<"(a*)*b">::match("aaab"));
  EXPECT_FALSE(Regex<"(a*)*b">::match("aaa"));

  auto result = Regex<"(a*)*b">::search("xxaaabxx");
  EXPECT_TRUE(result);
  EXPECT_EQ(result[0], "aaab");
}

TEST(RegexTemplateTest, ForceNFAProducesCorrectResults) {
  using Re = Regex<"(\\d+)-(\\w+)", Flags::ForceNFA>;
  auto m = Re::match("123-abc");
  EXPECT_TRUE(m);
  EXPECT_EQ(m[0], "123-abc");
  EXPECT_EQ(m[1], "123");
  EXPECT_EQ(m[2], "abc");

  EXPECT_FALSE(Re::match("no-match-here!"));
}

TEST(RegexTemplateTest, ForceDFAProducesCorrectResults) {
  using Re = Regex<"a+b+c", Flags::ForceDFA>;
  EXPECT_TRUE(Re::match("aaabbc"));
  EXPECT_FALSE(Re::match("ac"));

  EXPECT_TRUE(Re::test("xxaabcxx"));
  EXPECT_FALSE(Re::test("xxxx"));
}

TEST(RegexTemplateTest, AllEnginesAgreeOnMatch) {
  // The same pattern with different engine overrides should agree.
  using BT = Regex<"a+b+", Flags::ForceBacktracking>;
  using NFA = Regex<"a+b+", Flags::ForceNFA>;
  using DFA = Regex<"a+b+", Flags::ForceDFA>;
  using Auto = Regex<"a+b+">;
  std::string_view input = "aaabb";

  auto backtrack = BT::match(input);
  auto nfa = NFA::match(input);
  auto dfa = DFA::match(input);
  auto autoMode = Auto::match(input);

  EXPECT_TRUE(backtrack);
  EXPECT_TRUE(nfa);
  EXPECT_TRUE(dfa);
  EXPECT_TRUE(autoMode);

  EXPECT_EQ(backtrack[0], "aaabb");
  EXPECT_EQ(nfa[0], "aaabb");
  EXPECT_EQ(dfa[0], "aaabb");
  EXPECT_EQ(autoMode[0], "aaabb");
}

TEST(RegexTemplateTest, AllEnginesAgreeOnNoMatch) {
  using BT = Regex<"a+b+", Flags::ForceBacktracking>;
  using NFA = Regex<"a+b+", Flags::ForceNFA>;
  using DFA = Regex<"a+b+", Flags::ForceDFA>;
  using Auto = Regex<"a+b+">;
  std::string_view input = "cccc";

  EXPECT_FALSE(BT::match(input));
  EXPECT_FALSE(NFA::match(input));
  EXPECT_FALSE(DFA::match(input));
  EXPECT_FALSE(Auto::match(input));
}
