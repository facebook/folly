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

#include <folly/portability/GTest.h>

using namespace folly::regex;

constexpr Flags kNone = compilationFlags(Flags::None);

// ===========================================================================
// 1. DFA validity
// ===========================================================================

TEST(DfaTest, ValiditySimpleLiteral) {
  static_assert(detail::DfaHolder<"abc", kNone>::dfaProg_.valid);
}

TEST(DfaTest, ValiditySimpleCharClass) {
  static_assert(detail::DfaHolder<"[a-z]+", kNone>::dfaProg_.valid);
}

TEST(DfaTest, ValidityComplexPatternExceedsLimit) {
  // Backreference patterns are not NFA-compatible, so no DFA is built.
  static_assert(!detail::DfaHolder<"(a+)\\1", kNone>::dfaProg_.valid);
}

// ===========================================================================
// 2. DFA state counts
// ===========================================================================

TEST(DfaTest, StateCountSimpleLiteral) {
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(prog.state_count > 0);
  static_assert(prog.state_count < 10);
}

TEST(DfaTest, StateCountAlternationMoreStates) {
  constexpr auto& progSimple = detail::DfaHolder<"abc", kNone>::dfaProg_;
  constexpr auto& progAlt = detail::DfaHolder<"abc|def|ghi", kNone>::dfaProg_;
  static_assert(progAlt.valid);
  static_assert(progAlt.state_count > progSimple.state_count);
}

// ===========================================================================
// 3. Start states
// ===========================================================================

TEST(DfaTest, StartStatesValidForValidDfa) {
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(prog.start_anchored >= 0);
  static_assert(prog.start_unanchored >= 0);
  static_assert(prog.start_after_newline >= 0);
}

TEST(DfaTest, StartStatesEqualForNonAnchoredPattern) {
  // Without ^ anchor, anchored and unanchored closures are identical.
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(prog.start_anchored == prog.start_unanchored);
}

TEST(DfaTest, StartAfterNewlineDiffersForMultiline) {
  // With Multiline, ^ becomes AnchorBeginLine which is followable both at
  // string start and after newline, but NOT in the plain unanchored case.
  constexpr Flags kMultiline = compilationFlags(Flags::Multiline);
  constexpr auto& prog = detail::DfaHolder<"^abc", kMultiline>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.start_after_newline != prog.start_unanchored);
}

// ===========================================================================
// 4. Accepting states
// ===========================================================================

TEST(DfaTest, AcceptingBitsetNonEmpty) {
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(!prog.accepting.empty());
}

TEST(DfaTest, AcceptingAtEndForDollarAnchor) {
  constexpr auto& prog = detail::DfaHolder<"abc$", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(!prog.accepting_at_end.empty());
}

// ===========================================================================
// 5. MatchPreference classification
// ===========================================================================

TEST(DfaTest, MatchPreferenceAllGreedy) {
  constexpr auto& prog = detail::DfaHolder<"a+", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.match_preference == detail::MatchPreference::AllGreedy);
}

TEST(DfaTest, MatchPreferenceAllLazy) {
  constexpr auto& prog = detail::DfaHolder<"a+?", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.match_preference == detail::MatchPreference::AllLazy);
}

TEST(DfaTest, MatchPreferenceMixed) {
  // Alternation of a lazy branch and a greedy branch: the lazy branch
  // creates an accepting state with accept_early, while the greedy branch
  // creates one without — yielding Mixed preference.
  constexpr auto& prog = detail::DfaHolder<"a+?|b+", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.match_preference == detail::MatchPreference::Mixed);
}

// ===========================================================================
// 6. Equivalence classes
// ===========================================================================

TEST(DfaTest, EquivClassesCharClassSmall) {
  // [a-z] partitions the byte space into ~3 intervals: below-a, a-z, above-z.
  constexpr auto& prog = detail::DfaHolder<"[a-z]+", kNone>::dfaProg_;
  static_assert(prog.num_classes > 0);
  static_assert(prog.num_classes <= 5);
}

TEST(DfaTest, EquivClassesDotVeryFew) {
  // '.' (any byte) adds no partition boundaries; very few classes.
  constexpr auto& prog = detail::DfaHolder<".", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.num_classes >= 1);
  static_assert(prog.num_classes <= 3);
}

TEST(DfaTest, EquivClassesAlphanumModerate) {
  // [a-zA-Z0-9] has boundaries at 48,58,65,91,97,123 — more classes.
  constexpr auto& prog = detail::DfaHolder<"[a-zA-Z0-9]+", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(
      prog.num_classes >
      detail::DfaHolder<"[a-z]+", kNone>::dfaProg_.num_classes);
}

// ===========================================================================
// 7. Range classifier
// ===========================================================================

TEST(DfaTest, RangeClassifierSimpleCharClass) {
  // [a-z]+ decomposes into ≤2 contiguous byte-range boundaries,
  // so the executor can use immediate comparisons instead of a table.
  constexpr auto& prog = detail::DfaHolder<"[a-z]+", kNone>::dfaProg_;
  static_assert(prog.use_range_classifier);
}

TEST(DfaTest, RangeClassifierComplexCharClass) {
  // [a-zA-Z0-9]+ has >2 boundary transitions — range classifier disabled.
  constexpr auto& prog = detail::DfaHolder<"[a-zA-Z0-9]+", kNone>::dfaProg_;
  static_assert(!prog.use_range_classifier);
}

// ===========================================================================
// 8. Tag entry interning
// ===========================================================================

TEST(DfaTest, TagEntryCountWithCaptures) {
  // Pattern with explicit capture group: tag_entry_count > 1
  // (entry 0 is reserved for "no ops").
  constexpr auto& prog = detail::DfaHolder<"(abc)", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.tag_entry_count > 1);
}

TEST(DfaTest, TagEntryCountWithoutCaptures) {
  // Pattern with no explicit captures: tag_entry_count == 1.
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(prog.tag_entry_count == 1);
}

// ===========================================================================
// 9. Unanchored DFA
// ===========================================================================

TEST(DfaTest, UnanchoredDfaValidForNonAnchoredPattern) {
  // No anchors + small NFA → unanchored DFA is built.
  constexpr auto& prog =
      detail::UnanchoredDfaHolder<"abc", kNone>::dfaProgUnanchored_;
  static_assert(prog.valid);
}

TEST(DfaTest, UnanchoredDfaInvalidForAnchoredPattern) {
  // ^abc has kHasAnyAnchor = true → unanchored DFA is skipped.
  constexpr auto& prog =
      detail::UnanchoredDfaHolder<"^abc", kNone>::dfaProgUnanchored_;
  static_assert(!prog.valid);
}

TEST(DfaTest, UnanchoredDfaInvalidForEndAnchor) {
  // abc$ also has an anchor → unanchored DFA not built.
  constexpr auto& prog =
      detail::UnanchoredDfaHolder<"abc$", kNone>::dfaProgUnanchored_;
  static_assert(!prog.valid);
}

// ===========================================================================
// 10. Probe DFA construction (lookaround)
// ===========================================================================

TEST(DfaTest, ProbeDfaForLookahead) {
  constexpr auto& prog = detail::DfaHolder<R"(\d+(?=px))", kNone>::dfaProg_;
  static_assert(prog.valid);
  static_assert(prog.has_lookaround_probes);
  static_assert(prog.probe_state_count > 0);
}

TEST(DfaTest, NoProbeDfaForSimplePattern) {
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(!prog.has_lookaround_probes);
}

// ===========================================================================
// 11. Possessive probe construction
// ===========================================================================

TEST(DfaTest, NoPossessiveProbesForSimplePattern) {
  constexpr auto& prog = detail::DfaHolder<"abc", kNone>::dfaProg_;
  static_assert(!prog.has_possessive_probes);
}

TEST(DfaTest, PossessiveProbeForAtomicGroup) {
  // Atomic groups may or may not generate possessive probes depending on
  // whether the optimizer unwraps them. Verify the DFA builds and the
  // field is well-formed.
  constexpr auto& prog = detail::DfaHolder<"(?>a+)b", kNone>::dfaProg_;
  static_assert(prog.valid);
  EXPECT_EQ(prog.has_possessive_probes, prog.probe_count > 0);
}
