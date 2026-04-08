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

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

// ===== Cross-Engine Test Helpers =====
//
// These helpers run each regex operation (match, search, test) against all
// compatible engines (Backtracker, NFA, DFA) and verify they produce
// identical results. The NFA engine is skipped for patterns with features
// it doesn't support (backreferences, lookaround, word boundaries). The DFA
// engine is skipped when the pattern's DFA exceeds the compile-time state
// limit.

namespace folly::regex::testing {

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectMatchAllEngines(std::string_view input, bool expected) {
  auto bt = Regex<Pat, BaseFlags | Flags::ForceBacktracking>::match(input);
  EXPECT_EQ(bool(bt), expected)
      << "Backtracker: pattern=\"" << Pat.data << "\" input=\"" << input
      << "\"";

  if constexpr (Regex<Pat, BaseFlags>::parsed_.nfa_compatible) {
    auto nfa = Regex<Pat, BaseFlags | Flags::ForceNFA>::match(input);
    EXPECT_EQ(bool(nfa), expected)
        << "NFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
    EXPECT_EQ(bool(nfa), bool(bt))
        << "NFA/BT disagree: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\"";
  }

  if constexpr (Regex<Pat, BaseFlags>::dfaProg_.valid) {
    auto dfa = Regex<Pat, BaseFlags | Flags::ForceDFA>::match(input);
    EXPECT_EQ(bool(dfa), expected)
        << "DFA: pattern=\"" << Pat.data << "\" input=\"" << input << "\"";
    EXPECT_EQ(bool(dfa), bool(bt))
        << "DFA/BT disagree: pattern=\"" << Pat.data << "\" input=\"" << input
        << "\"";
  }
}

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectSearchAllEngines(
    std::string_view input,
    bool expectedFound,
    std::string_view expectedContent = "") {
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
}

template <FixedPattern Pat, Flags BaseFlags = Flags::None>
void expectTestAllEngines(std::string_view input, bool expected) {
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
}

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

  return bt;
}

// Same as expectMatchCapturesAgree but for search().
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

  return bt;
}

} // namespace folly::regex::testing
