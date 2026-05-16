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

#pragma once

#include <string>

#if __has_include(<re2/re2.h>)
#include <iostream> // NOLINT(facebook-hte-ConditionalBadInclude-iostream)
#include <memory>
#include <re2/re2.h>
#define FOLLY_REGEX_TEST_HAS_RE2 1
#endif

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

// ===== Cross-Engine Test Helpers =====
//
// These helpers run each regex operation (match, search, test) against all
// compatible engines (Backtracker, NFA, DFA) in both forward and reverse
// directions and verify they produce identical results. The NFA engine is
// skipped for patterns with features it doesn't support (backreferences,
// lookaround, word boundaries). The DFA engine is skipped when the
// pattern's DFA exceeds the compile-time state limit.
//
// Each helper also cross-checks against RE2. If RE2 cannot compile the
// pattern (e.g. backreferences, lookaround) the skip is logged to stderr
// once per pattern/flags combination.

namespace folly::regex::testing {

#ifdef FOLLY_REGEX_TEST_HAS_RE2
// Build and cache an RE2 for the given compile-time pattern and flags.
// Returns nullptr (and logs to stderr once) if RE2 can't handle the pattern.
template <FixedPattern Pat, Flags BaseFlags>
re2::RE2* re2ForPattern() {
  static const auto re = [] {
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    if constexpr (hasFlag(BaseFlags, Flags::DotAll)) {
      opts.set_dot_nl(true);
    }
    std::string pat;
    if constexpr (hasFlag(BaseFlags, Flags::Multiline)) {
      pat = "(?m)";
    }
    pat += Pat.data;
    auto compiled = std::make_unique<re2::RE2>(pat, opts);
    if (!compiled->ok()) {
      std::cerr << "[RE2 skip] Pattern \"" << Pat.data
                << "\" not supported by RE2: " << compiled->error() << "\n";
      return std::unique_ptr<re2::RE2>{};
    }
    return compiled;
  }();
  return re.get();
}
#endif // FOLLY_REGEX_TEST_HAS_RE2

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectMatchAllEngines(std::string_view input, bool expected) {
  // Forward engines
  auto bt = Regex<Pat, BaseFlags | Flags::ForceBacktracking>::match(input);
  EXPECT_EQ(bool(bt), expected)
      << "Forward BT: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\" expected=" << expected << " got=" << bool(bt);

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto nfa = Regex<Pat, BaseFlags | Flags::ForceNFA>::match(input);
    EXPECT_EQ(bool(nfa), expected)
        << "Forward NFA: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected << " got=" << bool(nfa);
    EXPECT_EQ(bool(nfa), bool(bt))
        << "Forward NFA/BT disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto dfa = Regex<Pat, BaseFlags | Flags::ForceDFA>::match(input);
    EXPECT_EQ(bool(dfa), expected)
        << "Forward DFA: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected << " got=" << bool(dfa);
    EXPECT_EQ(bool(dfa), bool(bt))
        << "DFA/BT disagree: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\"";
  }

  // Reverse engines
  constexpr auto Rev = Flags::ForceReverseExecution;

  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::match(input);
  EXPECT_EQ(bool(revBt), expected)
      << "Reverse BT: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\" expected=" << expected << " got=" << bool(revBt);

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto revNfa = Regex<Pat, BaseFlags | Flags::ForceNFA | Rev>::match(input);
    EXPECT_EQ(bool(revNfa), expected)
        << "Reverse NFA: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected << " got=" << bool(revNfa);
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto revDfa = Regex<Pat, BaseFlags | Flags::ForceDFA | Rev>::match(input);
    EXPECT_EQ(bool(revDfa), expected)
        << "Reverse DFA: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected << " got=" << bool(revDfa);
  }

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (anchored match)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::FullMatch(input, *re), expected)
        << "RE2: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected;
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2
}

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectSearchAllEngines(
    std::string_view input,
    bool expectedFound,
    std::string_view expectedContent = "") {
  // Forward engines
  auto bt = Regex<Pat, BaseFlags | Flags::ForceBacktracking>::search(input);
  EXPECT_EQ(bool(bt), expectedFound)
      << "Backtracker: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\"";
  if (bt && !expectedContent.empty()) {
    EXPECT_EQ(bt[0], expectedContent)
        << "Backtracker content: pattern=\"" << Pat.data << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto nfa = Regex<Pat, BaseFlags | Flags::ForceNFA>::search(input);
    EXPECT_EQ(bool(nfa), expectedFound)
        << "NFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
    if (nfa && bt) {
      EXPECT_EQ(std::string(nfa[0]), std::string(bt[0]))
          << "NFA/BT content disagree: pattern=\"" << Pat.data << "\"";
    }
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto dfa = Regex<Pat, BaseFlags | Flags::ForceDFA>::search(input);
    EXPECT_EQ(bool(dfa), expectedFound)
        << "DFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
    if (dfa && bt) {
      EXPECT_EQ(std::string(dfa[0]), std::string(bt[0]))
          << "DFA/BT content disagree: pattern=\"" << Pat.data << "\"";
    }
  }

  // Reverse engines (boolean result only — match position may differ)
  constexpr auto Rev = Flags::ForceReverseExecution;

  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::search(input);
  EXPECT_EQ(bool(revBt), expectedFound)
      << "Reverse BT search: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\"";

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto revNfa = Regex<Pat, BaseFlags | Flags::ForceNFA | Rev>::search(input);
    EXPECT_EQ(bool(revNfa), expectedFound)
        << "Reverse NFA search: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto revDfa = Regex<Pat, BaseFlags | Flags::ForceDFA | Rev>::search(input);
    EXPECT_EQ(bool(revDfa), expectedFound)
        << "Reverse DFA search: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (unanchored search)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::PartialMatch(input, *re), expectedFound)
        << "RE2: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2
}

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectTestAllEngines(std::string_view input, bool expected) {
  // Forward engines
  using BT = Regex<Pat, BaseFlags | Flags::ForceBacktracking>;
  bool btResult = BT::test(input);
  EXPECT_EQ(btResult, expected)
      << "Backtracker: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\"";

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    using NFA = Regex<Pat, BaseFlags | Flags::ForceNFA>;
    bool nfaResult = NFA::test(input);
    EXPECT_EQ(nfaResult, expected)
        << "NFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    using DFA = Regex<Pat, BaseFlags | Flags::ForceDFA>;
    bool dfaResult = DFA::test(input);
    EXPECT_EQ(dfaResult, expected)
        << "DFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }

  // Reverse engines
  constexpr auto Rev = Flags::ForceReverseExecution;

  using RevBT = Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>;
  EXPECT_EQ(RevBT::test(input), expected)
      << "Reverse BT test: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\"";

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    using RevNFA = Regex<Pat, BaseFlags | Flags::ForceNFA | Rev>;
    EXPECT_EQ(RevNFA::test(input), expected)
        << "Reverse NFA test: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    using RevDFA = Regex<Pat, BaseFlags | Flags::ForceDFA | Rev>;
    EXPECT_EQ(RevDFA::test(input), expected)
        << "Reverse DFA test: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\"";
  }

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (unanchored search)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::PartialMatch(input, *re), expected)
        << "RE2: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2
}

// Returns the backtracker result

// Returns the backtracker result for additional assertions. Also verifies
// that NFA and DFA (when compatible) agree on all capture groups.
template <FixedPattern Pat, Flags BaseFlags = Flags::None>
auto expectMatchCapturesAgree(std::string_view input) {
  auto bt = Regex<Pat, BaseFlags | Flags::ForceBacktracking>::match(input);

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto nfa = Regex<Pat, BaseFlags | Flags::ForceNFA>::match(input);
    EXPECT_EQ(bool(nfa), bool(bt))
        << "NFA/BT match disagree: pattern=\"" << Pat.data << "\"";
    if (nfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(nfa[i]), std::string(bt[i]))
            << "NFA/BT group " << i << " disagree: pattern=\"" << Pat.data
            << "\"";
      }
    }
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto dfa = Regex<Pat, BaseFlags | Flags::ForceDFA>::match(input);
    EXPECT_EQ(bool(dfa), bool(bt))
        << "DFA/BT match disagree: pattern=\"" << Pat.data << "\"";
    if (dfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(dfa[i]), std::string(bt[i]))
            << "DFA/BT group " << i << " disagree: pattern=\"" << Pat.data
            << "\"";
      }
    }
  }

  // Reverse backtracker — verify boolean result agrees
  constexpr auto Rev = Flags::ForceReverseExecution;
  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::match(input);
  EXPECT_EQ(bool(revBt), bool(bt))
      << "Reverse BT/BT match disagree: pattern=\"" << Pat.data << "\"";

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (anchored match, boolean only)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::FullMatch(input, *re), bool(bt))
        << "RE2/BT match disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2

  return bt;
}

// Same as expectMatchCapturesAgree
template <FixedPattern Pat, Flags BaseFlags = Flags::None>
auto expectSearchCapturesAgree(std::string_view input) {
  auto bt = Regex<Pat, BaseFlags | Flags::ForceBacktracking>::search(input);

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto nfa = Regex<Pat, BaseFlags | Flags::ForceNFA>::search(input);
    EXPECT_EQ(bool(nfa), bool(bt))
        << "NFA/BT search disagree: pattern=\"" << Pat.data << "\"";
    if (nfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(nfa[i]), std::string(bt[i]))
            << "NFA/BT group " << i << " disagree: pattern=\"" << Pat.data
            << "\"";
      }
    }
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto dfa = Regex<Pat, BaseFlags | Flags::ForceDFA>::search(input);
    EXPECT_EQ(bool(dfa), bool(bt))
        << "DFA/BT search disagree: pattern=\"" << Pat.data << "\"";
    if (dfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(dfa[i]), std::string(bt[i]))
            << "DFA/BT group " << i << " disagree: pattern=\"" << Pat.data
            << "\"";
      }
    }
  }

  // Reverse backtracker — verify boolean result agrees
  constexpr auto Rev = Flags::ForceReverseExecution;
  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::search(input);
  EXPECT_EQ(bool(revBt), bool(bt))
      << "Reverse BT/BT search disagree: pattern=\"" << Pat.data << "\"";

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (unanchored search, boolean only)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::PartialMatch(input, *re), bool(bt))
        << "RE2/BT search disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2

  return bt;
}

} // namespace folly::regex::testing
