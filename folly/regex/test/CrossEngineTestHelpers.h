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

#if __has_include(<pcre.h>)
#include <pcre.h>
#include <iostream> // NOLINT(facebook-hte-ConditionalBadInclude-iostream)
#include <memory>
#define FOLLY_REGEX_TEST_HAS_PCRE 1
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
// Each helper also cross-checks against RE2 and PCRE. If RE2/PCRE cannot
// compile the pattern (e.g. backreferences, lookaround) the skip is logged
// to stderr once per pattern/flags combination.

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
    if constexpr (hasFlag(BaseFlags, Flags::CaseInsensitive)) {
      opts.set_case_sensitive(false);
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
      compiled.reset();
    }
    return compiled;
  }();
  return re.get();
}
#endif // FOLLY_REGEX_TEST_HAS_RE2

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
struct PcrePatternCtx {
  pcre* searchRe = nullptr;
  pcre_extra* searchExtra = nullptr;
  pcre* matchRe = nullptr;
  pcre_extra* matchExtra = nullptr;

  ~PcrePatternCtx() {
    if (searchExtra) {
      pcre_free_study(searchExtra);
    }
    if (searchRe) {
      pcre_free(searchRe);
    }
    if (matchExtra) {
      pcre_free_study(matchExtra);
    }
    if (matchRe) {
      pcre_free(matchRe);
    }
  }

  bool search(std::string_view input) const {
    int ov[30];
    return pcre_exec(
               searchRe,
               searchExtra,
               input.data(),
               static_cast<int>(input.size()),
               0,
               0,
               ov,
               30) >= 0;
  }

  bool match(std::string_view input) const {
    int ov[30];
    // matchRe is compiled with \A(?:...)\z anchors so PCRE backtracks
    // through alternatives until one consumes the full input.
    return pcre_exec(
               matchRe,
               matchExtra,
               input.data(),
               static_cast<int>(input.size()),
               0,
               0,
               ov,
               30) >= 0;
  }
};

// Build and cache a PCRE for the given compile-time pattern and flags.
// Returns nullptr (and logs to stderr once) if PCRE can't handle the pattern.
// Compiles two variants: one for unanchored search and one wrapped with
// \A(?:...)\z for correct full-match semantics.
template <FixedPattern Pat, Flags BaseFlags>
PcrePatternCtx* pcreForPattern() {
  static const auto ctx = [] {
    auto compiled = std::make_unique<PcrePatternCtx>();
    const char* error;
    int erroffset;
    int options = 0;
    if constexpr (hasFlag(BaseFlags, Flags::DotAll)) {
      options |= PCRE_DOTALL;
    }
    if constexpr (hasFlag(BaseFlags, Flags::CaseInsensitive)) {
      options |= PCRE_CASELESS;
    }
    if constexpr (hasFlag(BaseFlags, Flags::Multiline)) {
      options |= PCRE_MULTILINE;
    }
    compiled->searchRe =
        pcre_compile(Pat.data, options, &error, &erroffset, nullptr);
    if (!compiled->searchRe) {
      std::cerr << "[PCRE skip] Pattern \"" << Pat.data
                << "\" not supported by PCRE: " << error << "\n";
      return std::unique_ptr<PcrePatternCtx>{};
    }
    compiled->searchExtra =
        pcre_study(compiled->searchRe, PCRE_STUDY_JIT_COMPILE, &error);
    // Wrap in \A(?:...)\z for full-match: forces PCRE to backtrack through
    // alternatives until one consumes the entire input.
    std::string matchPat = "\\A(?:";
    matchPat += Pat.data;
    matchPat += ")\\z";
    compiled->matchRe =
        pcre_compile(matchPat.c_str(), options, &error, &erroffset, nullptr);
    if (!compiled->matchRe) {
      std::cerr << "[PCRE skip] Pattern \"" << Pat.data
                << "\" match wrapper not supported by PCRE: " << error << "\n";
      return std::unique_ptr<PcrePatternCtx>{};
    }
    compiled->matchExtra =
        pcre_study(compiled->matchRe, PCRE_STUDY_JIT_COMPILE, &error);
    return compiled;
  }();
  return ctx.get();
}
#endif // FOLLY_REGEX_TEST_HAS_PCRE

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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
  // PCRE cross-check (anchored match)
  if (auto* ctx = pcreForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(ctx->match(input), expected)
        << "PCRE: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\" expected=" << expected;
  }
#endif // FOLLY_REGEX_TEST_HAS_PCRE
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
  // PCRE cross-check (unanchored search)
  if (auto* ctx = pcreForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(ctx->search(input), expectedFound)
        << "PCRE: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_PCRE
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
  // PCRE cross-check (unanchored search)
  if (auto* ctx = pcreForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(ctx->search(input), expected)
        << "PCRE: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_PCRE
}

// Returns the backtracker result for additional assertions.
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

  // Reverse backtracker — verify boolean result AND capture groups agree
  constexpr auto Rev = Flags::ForceReverseExecution;
  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::match(input);
  EXPECT_EQ(bool(revBt), bool(bt))
      << "Reverse BT/BT match disagree: pattern=\"" << Pat.data << "\"";
  if (revBt && bt) {
    for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
      EXPECT_EQ(std::string(revBt[i]), std::string(bt[i]))
          << "Reverse BT/Forward BT group " << i << " disagree: pattern=\""
          << Pat.data << "\" input=\"" << input << "\"";
    }
  }

  // Reverse NFA — verify captures agree with forward BT
  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto revNfa = Regex<Pat, BaseFlags | Flags::ForceNFA | Rev>::match(input);
    EXPECT_EQ(bool(revNfa), bool(bt))
        << "Reverse NFA/Forward BT match disagree: pattern=\"" << Pat.data
        << "\"";
    if (revNfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(revNfa[i]), std::string(bt[i]))
            << "Reverse NFA/Forward BT group " << i << " disagree: pattern=\""
            << Pat.data << "\" input=\"" << input << "\"";
      }
    }
  }

  // Reverse DFA — verify captures agree with forward BT
  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
    auto revDfa = Regex<Pat, BaseFlags | Flags::ForceDFA | Rev>::match(input);
    EXPECT_EQ(bool(revDfa), bool(bt))
        << "Reverse DFA/Forward BT match disagree: pattern=\"" << Pat.data
        << "\"";
    if (revDfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(revDfa[i]), std::string(bt[i]))
            << "Reverse DFA/Forward BT group " << i << " disagree: pattern=\""
            << Pat.data << "\" input=\"" << input << "\"";
      }
    }
  }

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (anchored match, boolean only)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::FullMatch(input, *re), bool(bt))
        << "RE2/BT match disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
  // PCRE cross-check (anchored match, boolean only)
  if (auto* ctx = pcreForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(ctx->match(input), bool(bt))
        << "PCRE/BT match disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_PCRE

  return bt;
}

// Same as expectMatchCapturesAgree but for unanchored search.
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

  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
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

  // Reverse backtracker — verify boolean result AND capture groups agree
  constexpr auto Rev = Flags::ForceReverseExecution;
  auto revBt =
      Regex<Pat, BaseFlags | Flags::ForceBacktracking | Rev>::search(input);
  EXPECT_EQ(bool(revBt), bool(bt))
      << "Reverse BT/BT search disagree: pattern=\"" << Pat.data << "\"";
  if (revBt && bt) {
    for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
      EXPECT_EQ(std::string(revBt[i]), std::string(bt[i]))
          << "Reverse BT/Forward BT search group " << i
          << " disagree: pattern=\"" << Pat.data << "\" input=\"" << input
          << "\"";
    }
  }

  // Reverse NFA — verify search captures agree with forward BT
  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto revNfa = Regex<Pat, BaseFlags | Flags::ForceNFA | Rev>::search(input);
    EXPECT_EQ(bool(revNfa), bool(bt))
        << "Reverse NFA/Forward BT search disagree: pattern=\"" << Pat.data
        << "\"";
    if (revNfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(revNfa[i]), std::string(bt[i]))
            << "Reverse NFA/Forward BT search group " << i
            << " disagree: pattern=\"" << Pat.data << "\" input=\"" << input
            << "\"";
      }
    }
  }

  // Reverse DFA — verify search captures agree with forward BT
  if constexpr (detail::DfaHolder<Pat, BaseFlags>::dfaProg_.valid) {
    auto revDfa = Regex<Pat, BaseFlags | Flags::ForceDFA | Rev>::search(input);
    EXPECT_EQ(bool(revDfa), bool(bt))
        << "Reverse DFA/Forward BT search disagree: pattern=\"" << Pat.data
        << "\"";
    if (revDfa && bt) {
      for (std::size_t i = 0; i < decltype(bt)::size(); ++i) {
        EXPECT_EQ(std::string(revDfa[i]), std::string(bt[i]))
            << "Reverse DFA/Forward BT search group " << i
            << " disagree: pattern=\"" << Pat.data << "\" input=\"" << input
            << "\"";
      }
    }
  }

#ifdef FOLLY_REGEX_TEST_HAS_RE2
  // RE2 cross-check (unanchored search, boolean only)
  if (auto* re = re2ForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(re2::RE2::PartialMatch(input, *re), bool(bt))
        << "RE2/BT search disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_RE2

#ifdef FOLLY_REGEX_TEST_HAS_PCRE
  // PCRE cross-check (unanchored search, boolean only)
  if (auto* ctx = pcreForPattern<Pat, BaseFlags>()) {
    EXPECT_EQ(ctx->search(input), bool(bt))
        << "PCRE/BT search disagree: pattern=\"" << Pat.data << "\" input=\""
        << input << "\"";
  }
#endif // FOLLY_REGEX_TEST_HAS_PCRE

  return bt;
}

} // namespace folly::regex::testing
