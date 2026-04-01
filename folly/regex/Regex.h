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

#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstOptimizer.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Dfa.h>
#include <folly/regex/detail/DfaExecutor.h>
#include <folly/regex/detail/Executor.h>
#include <folly/regex/detail/Nfa.h>
#include <folly/regex/detail/NfaExecutor.h>
#include <folly/regex/detail/Parser.h>

namespace folly {
namespace regex {

enum class Flags : unsigned {
  None = 0,
  ForceBacktracking = 1u << 0,
  ForceNFA = 1u << 1,
  ForceDFA = 1u << 2,
  Multiline = 1u << 3,
  DotAll = 1u << 4,
};

constexpr Flags operator|(Flags a, Flags b) noexcept {
  return static_cast<Flags>(
      static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr Flags operator&(Flags a, Flags b) noexcept {
  return static_cast<Flags>(
      static_cast<unsigned>(a) & static_cast<unsigned>(b));
}

constexpr Flags operator~(Flags a) noexcept {
  return static_cast<Flags>(~static_cast<unsigned>(a));
}

constexpr bool hasFlag(Flags flags, Flags flag) noexcept {
  return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}

template <std::size_t N>
struct FixedPattern {
  char data[N + 1] = {};

  consteval FixedPattern(const char (&str)[N + 1]) {
    for (std::size_t i = 0; i <= N; ++i) {
      data[i] = str[i];
    }
  }

  consteval std::size_t length() const noexcept { return N; }
  consteval char operator[](std::size_t i) const noexcept { return data[i]; }
};

template <std::size_t N>
FixedPattern(const char (&)[N]) -> FixedPattern<N - 1>;

template <int NumGroups>
struct MatchResult {
  bool matched_ = false;
  std::array<std::string_view, NumGroups + 1> groups_ = {};

  explicit operator bool() const noexcept { return matched_; }

  std::string_view operator[](std::size_t i) const noexcept {
    return groups_[i];
  }

  static constexpr std::size_t size() noexcept { return NumGroups + 1; }

  template <std::size_t I>
  std::string_view get() const noexcept {
    static_assert(I <= static_cast<std::size_t>(NumGroups));
    return groups_[I];
  }
};

} // namespace regex
} // namespace folly

template <int NumGroups>
struct std::tuple_size<folly::regex::MatchResult<NumGroups>>
    : std::integral_constant<std::size_t, NumGroups + 1> {};

template <std::size_t I, int NumGroups>
struct std::tuple_element<I, folly::regex::MatchResult<NumGroups>> {
  using type = std::string_view;
};

namespace folly {
namespace regex {

template <std::size_t I, int NumGroups>
std::string_view get(const MatchResult<NumGroups>& m) noexcept {
  static_assert(I <= static_cast<std::size_t>(NumGroups));
  return m.groups_[I];
}

namespace detail {

// Forward declaration
template <
    const auto& Ast,
    const auto& NfaProg,
    const auto& DfaProg,
    Flags F,
    int NumGroups>
struct NfaColdFallback;

template <
    const auto& Ast,
    const auto& NfaProg,
    const auto& DfaProg,
    Flags F,
    int NumGroups,
    int PrefixLen = 0,
    int SuffixLen = 0>
struct HybridMatcher {
  friend struct NfaColdFallback<Ast, NfaProg, DfaProg, F, NumGroups>;

  static MatchResult<NumGroups> doMatch(std::string_view input) noexcept {
    // Fast-reject via literal prefix/suffix
    if constexpr (PrefixLen > 0 || SuffixLen > 0) {
      constexpr int minLen = PrefixLen + SuffixLen;
      if (input.size() < static_cast<std::size_t>(minLen)) {
        return MatchResult<NumGroups>{};
      }
      if constexpr (PrefixLen > 0) {
        for (int i = 0; i < PrefixLen; ++i) {
          if (input[i] != Ast.stripped_prefix[i]) {
            return MatchResult<NumGroups>{};
          }
        }
      }
      if constexpr (SuffixLen > 0) {
        constexpr auto kSuffix = extractLiteralSuffix(Ast, Ast.root);
        for (int i = 0; i < SuffixLen; ++i) {
          if (input[input.size() - SuffixLen + i] != kSuffix.chars[i]) {
            return MatchResult<NumGroups>{};
          }
        }
      }
    }

    // When prefix was stripped from AST, match on trimmed input
    if constexpr (PrefixLen > 0) {
      auto trimmed = input.substr(PrefixLen);
      auto outcome = matchEngine(trimmed);
      if (outcome.status == MatchStatus::Matched) {
        for (int i = 0; i <= NumGroups; ++i) {
          auto& g = outcome.state.groups[i];
          if (g.offset != std::string_view::npos) {
            g.offset += PrefixLen;
          }
        }
        outcome.state.groups[0] = {0, input.size()};
        return fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    } else {
      return fromOutcome(matchEngine(input), input);
    }
  }

  static MatchResult<NumGroups> doSearch(std::string_view input) noexcept {
    if constexpr (PrefixLen > 0) {
      return prefixSearch(input);
    } else {
      return searchImpl(input);
    }
  }

  static bool doTest(std::string_view input) noexcept {
    if constexpr (PrefixLen > 0) {
      return prefixTest(input);
    } else {
      return testImpl(input);
    }
  }

 private:
  static MatchResult<NumGroups> prefixSearch(std::string_view input) noexcept {
    constexpr auto prefixLen = static_cast<std::size_t>(PrefixLen);
    const char firstChar = Ast.stripped_prefix[0];
    std::size_t pos = 0;

    while (pos + prefixLen <= input.size()) {
      auto* p = static_cast<const char*>(std::memchr(
          input.data() + pos, firstChar, input.size() - pos - prefixLen + 1));
      if (!p) {
        break;
      }
      pos = static_cast<std::size_t>(p - input.data());
      if (pos + prefixLen > input.size()) {
        break;
      }

      bool ok = true;
      for (int i = 1; i < PrefixLen; ++i) {
        if (input[pos + i] != Ast.stripped_prefix[i]) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        ++pos;
        continue;
      }

      auto remaining = input.substr(pos + prefixLen);
      auto result = searchImpl(remaining);
      if (result && result[0].data() == remaining.data()) {
        MatchResult<NumGroups> adjusted;
        adjusted.matched_ = true;
        adjusted.groups_[0] =
            std::string_view{input.data() + pos, prefixLen + result[0].size()};
        for (int i = 1; i <= NumGroups; ++i) {
          adjusted.groups_[i] = result[i];
        }
        return adjusted;
      }
      ++pos;
    }
    return MatchResult<NumGroups>{};
  }

  static bool prefixTest(std::string_view input) noexcept {
    constexpr auto prefixLen = static_cast<std::size_t>(PrefixLen);
    const char firstChar = Ast.stripped_prefix[0];
    std::size_t pos = 0;

    while (pos + prefixLen <= input.size()) {
      auto* p = static_cast<const char*>(std::memchr(
          input.data() + pos, firstChar, input.size() - pos - prefixLen + 1));
      if (!p) {
        break;
      }
      pos = static_cast<std::size_t>(p - input.data());
      if (pos + prefixLen > input.size()) {
        break;
      }

      bool ok = true;
      for (int i = 1; i < PrefixLen; ++i) {
        if (input[pos + i] != Ast.stripped_prefix[i]) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        ++pos;
        continue;
      }

      auto remaining = input.substr(pos + prefixLen);
      auto result = searchImpl(remaining);
      if (result && result[0].data() == remaining.data()) {
        return true;
      }
      ++pos;
    }
    return false;
  }

  static MatchResult<NumGroups> searchImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      static_assert(
          DfaProg.valid,
          "ForceDFA requires a pattern whose DFA fits within the state limit");
      return fromOutcome(
          DfaRunner<DfaProg, Ast, F, true, NumGroups>::search(input), input);
    } else if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      auto pos = NfaPositionSearcher<NfaProg, Ast, F>::findFirst(input);
      if (!pos.found) {
        return MatchResult<NumGroups>{};
      }
      auto matchSub = input.substr(pos.start, pos.end - pos.start);
      auto outcome =
          NfaRunner<NfaProg, Ast, F, true, NumGroups>::matchAnchored(matchSub);
      if (outcome.status == MatchStatus::Matched) {
        for (int i = 0; i <= NumGroups; ++i) {
          auto& g = outcome.state.groups[i];
          if (g.offset != std::string_view::npos) {
            g.offset += pos.start;
          }
        }
        outcome.state.groups[0] = {pos.start, pos.end - pos.start};
        return fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    } else if constexpr (hasFlag(F, Flags::ForceBacktracking)) {
      return fromOutcome(
          BacktrackRunner<Ast, F, true, NumGroups, false>::searchCapturing(
              input),
          input);
    } else if constexpr (!Ast.nfa_compatible) {
      return fromOutcome(
          BacktrackRunner<Ast, F, true, NumGroups, false>::searchCapturing(
              input),
          input);
    } else if constexpr (Ast.backtrack_safe) {
      return fromOutcome(
          BacktrackRunner<Ast, F, true, NumGroups, false>::searchCapturing(
              input),
          input);
    } else {
      auto outcome =
          BacktrackRunner<Ast, F, true, NumGroups, true>::searchCapturing(
              input);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        return NfaColdFallback<Ast, NfaProg, DfaProg, F, NumGroups>::
            searchWithCaptures(input);
      }
      return fromOutcome(outcome, input);
    }
  }

  static bool testImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      static_assert(
          DfaProg.valid,
          "ForceDFA requires a pattern whose DFA fits within the state limit");
      return DfaRunner<DfaProg, Ast, F, false, NumGroups>::testMatch(input);
    } else if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      return NfaRunner<NfaProg, Ast, F, false, NumGroups>::search(input)
                 .status == MatchStatus::Matched;
    } else if constexpr (hasFlag(F, Flags::ForceBacktracking)) {
      return BacktrackRunner<Ast, F, false, NumGroups, false>::template search<
                 true>(input)
          .matched;
    } else if constexpr (!Ast.nfa_compatible) {
      return BacktrackRunner<Ast, F, false, NumGroups, false>::template search<
                 true>(input)
          .matched;
    } else {
      if constexpr (DfaProg.valid) {
        return DfaRunner<DfaProg, Ast, F, false, NumGroups>::testMatch(input);
      } else {
        auto result =
            BacktrackRunner<Ast, F, false, NumGroups, true>::template search<
                true>(input);
        if (result.budgetExhausted) {
          return NfaColdFallback<Ast, NfaProg, DfaProg, F, NumGroups>::test(
              input);
        }
        return result.matched;
      }
    }
  }

 private:
  static MatchOutcome<NumGroups> matchEngine(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      return DfaRunner<DfaProg, Ast, F, true, NumGroups>::matchAnchored(input);
    } else if constexpr (hasFlag(F, Flags::ForceNFA)) {
      return NfaRunner<NfaProg, Ast, F, true, NumGroups>::matchAnchored(input);
    } else if constexpr (hasFlag(F, Flags::ForceBacktracking)) {
      return BacktrackRunner<Ast, F, true, NumGroups, false>::matchAnchored(
          input);
    } else if constexpr (!Ast.nfa_compatible) {
      return BacktrackRunner<Ast, F, true, NumGroups, false>::matchAnchored(
          input);
    } else if constexpr (Ast.backtrack_safe) {
      return BacktrackRunner<Ast, F, true, NumGroups, false>::matchAnchored(
          input);
    } else {
      auto outcome =
          BacktrackRunner<Ast, F, true, NumGroups, true>::matchAnchored(input);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        if constexpr (DfaProg.valid) {
          return DfaRunner<DfaProg, Ast, F, true, NumGroups>::matchAnchored(
              input);
        } else {
          return NfaRunner<NfaProg, Ast, F, true, NumGroups>::matchAnchored(
              input);
        }
      }
      return outcome;
    }
  }

  static MatchResult<NumGroups> fromOutcome(
      const MatchOutcome<NumGroups>& outcome, std::string_view input) noexcept {
    MatchResult<NumGroups> result;
    if (outcome.status == MatchStatus::Matched) {
      result.matched_ = true;
      for (int i = 0; i <= NumGroups; ++i) {
        const auto& g = outcome.state.groups[i];
        if (g.offset != std::string_view::npos && g.offset <= input.size()) {
          result.groups_[i] = input.substr(g.offset, g.length);
        }
      }
    }
    return result;
  }
};

// Cold fallback paths — uses DFA when available, NFA otherwise
template <
    const auto& Ast,
    const auto& NfaProg,
    const auto& DfaProg,
    Flags F,
    int NumGroups>
struct NfaColdFallback {
  using Matcher = HybridMatcher<Ast, NfaProg, DfaProg, F, NumGroups>;

  FOLLY_NOINLINE static MatchResult<NumGroups> matchAnchored(
      std::string_view input) noexcept {
    if constexpr (DfaProg.valid) {
      return Matcher::fromOutcome(
          DfaRunner<DfaProg, Ast, F, true, NumGroups>::matchAnchored(input),
          input);
    } else {
      return Matcher::fromOutcome(
          NfaRunner<NfaProg, Ast, F, true, NumGroups>::matchAnchored(input),
          input);
    }
  }

  FOLLY_NOINLINE static MatchResult<NumGroups> searchWithCaptures(
      std::string_view input) noexcept {
    if constexpr (DfaProg.valid) {
      return Matcher::fromOutcome(
          DfaRunner<DfaProg, Ast, F, true, NumGroups>::search(input), input);
    } else {
      auto pos = NfaPositionSearcher<NfaProg, Ast, F>::findFirst(input);
      if (!pos.found) {
        return MatchResult<NumGroups>{};
      }
      auto matchSub = input.substr(pos.start, pos.end - pos.start);
      auto outcome =
          NfaRunner<NfaProg, Ast, F, true, NumGroups>::matchAnchored(matchSub);
      if (outcome.status == MatchStatus::Matched) {
        for (int i = 0; i <= NumGroups; ++i) {
          auto& g = outcome.state.groups[i];
          if (g.offset != std::string_view::npos) {
            g.offset += pos.start;
          }
        }
        outcome.state.groups[0] = {pos.start, pos.end - pos.start};
        return Matcher::fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    }
  }

  FOLLY_NOINLINE static bool test(std::string_view input) noexcept {
    if constexpr (DfaProg.valid) {
      return DfaRunner<DfaProg, Ast, F, false, NumGroups>::testMatch(input);
    } else {
      return NfaPositionSearcher<NfaProg, Ast, F>::testMatch(input);
    }
  }
};

} // namespace detail

template <FixedPattern Pat, Flags F = Flags::None>
struct Regex {
  static_assert(
      !(hasFlag(F, Flags::ForceBacktracking) && hasFlag(F, Flags::ForceNFA)),
      "ForceBacktracking and ForceNFA are mutually exclusive");
  static_assert(
      !(hasFlag(F, Flags::ForceDFA) && hasFlag(F, Flags::ForceBacktracking)),
      "ForceDFA and ForceBacktracking are mutually exclusive");
  static_assert(
      !(hasFlag(F, Flags::ForceDFA) && hasFlag(F, Flags::ForceNFA)),
      "ForceDFA and ForceNFA are mutually exclusive");

  static constexpr auto parsed_ = [] {
    auto ast = detail::parse<detail::ParseErrorMode::CompileTime>(
        std::string_view{Pat.data, Pat.length()});
    if (ast.valid) {
      if (hasFlag(F, Flags::Multiline)) {
        detail::applyMultilineFlag(ast, ast.root);
      }
      if (hasFlag(F, Flags::DotAll)) {
        detail::applyDotAllFlag(ast, ast.root);
      }
      ast.root = detail::optimizeAst(ast, ast.root);
    }
    return ast;
  }();

  static constexpr int NumGroups = parsed_.group_count;

  static constexpr auto nfaProg_ = [] {
    if constexpr (parsed_.nfa_compatible) {
      return detail::buildNfa<parsed_.node_count * 4 + 16>(parsed_);
    } else {
      return detail::NfaProgram<1>{};
    }
  }();

  // With interval-based subset construction and CountedRepeat loops,
  // the constexpr cost is dramatically reduced. The dominant costs are:
  //   1. Zero-init of iv_trans/iv_tags: O(MaxDfaStates * intervals)
  //   2. Subset construction:          O(dfa_states * intervals * nfa_states)
  //   3. Equivalence classes:          O(256) (direct from partition)
  // CountedRepeat reduces NFA state count by 25-64% for counted repetitions.
  // The constexpr budget limit for MaxDfaStates is ~3856; we use 3584
  // to leave headroom for patterns with higher subset construction cost.
  static constexpr int kMaxDfaStates = !parsed_.nfa_compatible ? 1 : 3584;

  static constexpr auto dfaProg_ = [] {
    if constexpr (parsed_.nfa_compatible && kMaxDfaStates > 0) {
      return detail::buildDfa<kMaxDfaStates>(nfaProg_, parsed_);
    } else {
      return detail::DfaProgram<1>{};
    }
  }();

  static constexpr auto literalPrefix_ =
      detail::extractLiteralPrefix(parsed_, parsed_.root);
  static constexpr auto literalSuffix_ =
      detail::extractLiteralSuffix(parsed_, parsed_.root);

  using Matcher = detail::HybridMatcher<
      parsed_,
      nfaProg_,
      dfaProg_,
      F,
      NumGroups,
      parsed_.stripped_prefix_len,
      literalSuffix_.length>;

  static MatchResult<NumGroups> match(std::string_view input) noexcept {
    return Matcher::doMatch(input);
  }

  static MatchResult<NumGroups> search(std::string_view input) noexcept {
    return Matcher::doSearch(input);
  }

  static bool test(std::string_view input) noexcept {
    return Matcher::doTest(input);
  }

  struct MatchIterator {
    using value_type = MatchResult<NumGroups>;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = const value_type&;
    using iterator_category = std::input_iterator_tag;

    std::string_view input_;
    std::size_t pos_ = 0;
    value_type current_;
    bool done_ = false;

    MatchIterator() : done_(true) {}
    explicit MatchIterator(std::string_view input) : input_(input) {
      advance();
    }

    reference operator*() const noexcept { return current_; }
    pointer operator->() const noexcept { return &current_; }

    MatchIterator& operator++() {
      advance();
      return *this;
    }

    MatchIterator operator++(int) {
      auto tmp = *this;
      advance();
      return tmp;
    }

    bool operator==(const MatchIterator& other) const noexcept {
      return done_ == other.done_;
    }

    bool operator!=(const MatchIterator& other) const noexcept {
      return !(*this == other);
    }

   private:
    void advance() {
      if (done_ || pos_ > input_.size()) {
        done_ = true;
        return;
      }

      auto remaining = input_.substr(pos_);
      auto result = Matcher::doSearch(remaining);

      if (!result) {
        done_ = true;
        return;
      }

      // Adjust group offsets relative to original input
      for (auto& g : result.groups_) {
        if (!g.empty()) {
          auto offset = static_cast<std::size_t>(g.data() - input_.data());
          g = input_.substr(offset, g.size());
        }
      }

      current_ = result;
      auto matchEnd =
          static_cast<std::size_t>(result[0].data() - input_.data()) +
          result[0].size();

      if (matchEnd == pos_) {
        ++pos_;
      } else {
        pos_ = matchEnd;
      }
    }
  };

  struct MatchRange {
    std::string_view input_;

    explicit MatchRange(std::string_view input) : input_(input) {}

    MatchIterator begin() const { return MatchIterator(input_); }
    MatchIterator end() const { return MatchIterator(); }
  };

  static MatchRange matchAll(std::string_view input) noexcept {
    return MatchRange(input);
  }
};

template <FixedPattern Pat, Flags F = Flags::None>
consteval auto compile() {
  return Regex<Pat, F>{};
}

namespace literals {

template <FixedPattern Pat>
consteval auto operator""_re() {
  return Regex<Pat>{};
}

} // namespace literals

template <FixedPattern Pat, Flags F = Flags::None>
auto match(std::string_view input) {
  return Regex<Pat, F>::match(input);
}

template <FixedPattern Pat, Flags F = Flags::None>
auto search(std::string_view input) {
  return Regex<Pat, F>::search(input);
}

template <FixedPattern Pat, Flags F = Flags::None>
bool test(std::string_view input) {
  return Regex<Pat, F>::test(input);
}

template <FixedPattern Pat, Flags F = Flags::None>
auto matchAll(std::string_view input) {
  return Regex<Pat, F>::matchAll(input);
}

} // namespace regex
} // namespace folly
