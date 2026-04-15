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
#include <folly/regex/detail/Direction.h>
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
  ForceReverseExecution = 1u << 5,
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

// Flags that affect AST/NFA/DFA compilation. Execution-only flags
// (ForceBacktracking, ForceNFA, ForceDFA) do not change compiled
// artifacts and are excluded so that different execution modes can
// share the same backing storage.
constexpr Flags kCompilationFlagMask =
    Flags::Multiline | Flags::DotAll | Flags::ForceReverseExecution;

constexpr Flags compilationFlags(Flags f) noexcept {
  return f & kCompilationFlagMask;
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

// Implementation of DfaRunner::evaluateLookaroundProbes.
// Defined here (after both NfaExecutor.h and DfaExecutor.h are included)
// to avoid circular include dependencies between DfaExecutor and NfaExecutor.
template <
    const auto& DfaProg_,
    const auto& DfaProgUnanchored_,
    const auto& Ast_,
    Flags F_,
    bool TrackCaptures_,
    int NumGroups_,
    Direction Dir_,
    const auto& NfaProg_,
    const auto& ForwardAst_,
    const auto& ProbeDfas_>
bool DfaRunner<
    DfaProg_,
    DfaProgUnanchored_,
    Ast_,
    F_,
    TrackCaptures_,
    NumGroups_,
    Dir_,
    NfaProg_,
    ForwardAst_,
    ProbeDfas_>::
    evaluateLookaroundProbes(
        int dfaState,
        InputView<Dir_> input,
        std::size_t pos) noexcept {
  if constexpr (!DfaProg_.has_lookaround_probes) {
    return true;
  } else {
    const auto& entry = DfaProg_.lookaround_probes[dfaState];
    if (entry.count == 0) {
      return true;
    }
    for (int i = 0; i < entry.count; ++i) {
      const auto& p = entry.probes[i];
      bool matched = false;

      if (p.lookbehind_width >= 0) {
        // Fixed-width lookbehind: inline comparison using ProbeStore AST
        if (pos >= static_cast<std::size_t>(p.lookbehind_width)) {
          using ProbeExecutor = NfaExecutorBase<
              NfaPositionSearcherPolicy<NfaProg_>,
              NfaProg_,
              Ast_,
              F_,
              Dir_,
              ForwardAst_>;
          matched = ProbeExecutor::evaluateFixedLookbehind(
              p.probe_id, input, pos, p.lookbehind_width);
        }
      } else if (
          p.probe_id >= 0 && p.probe_id < ProbeDfas_.count &&
          ProbeDfas_.entries[p.probe_id].valid) {
        // Run the pre-compiled probe sub-DFA from pos forward
        const auto& probeDfa = ProbeDfas_.entries[p.probe_id];
        int state = probeDfa.start_anchored;
        if (state >= 0) {
          // Check if start state itself is accepting (empty match)
          if (probeDfa.accepting.test(state)) {
            matched = true;
          } else {
            // Run the sub-DFA forward from pos
            for (std::size_t j = pos; j < input.size() && !matched; ++j) {
              auto cls = probeDfa.char_to_class
                  [static_cast<unsigned char>(input[j])];
              auto trans = probeDfa.transitions[cls][state];
              if (trans < 0) {
                break;
              }
              state = trans & 0x3FFF;
              if (probeDfa.accepting.test(state) ||
                  (j + 1 == input.size() &&
                   probeDfa.accepting_at_end.test(state))) {
                matched = true;
              }
            }
            // Check accepting_at_end at final position
            if (!matched && probeDfa.accepting_at_end.test(state)) {
              matched = true;
            }
          }
        }
      }

      if (p.negated) {
        matched = !matched;
      }
      if (!matched) {
        return false;
      }
    }
    return true;
  }
}

// Forward declaration
template <
    const auto& Ast,
    const auto& NfaProg,
    const auto& DfaProg,
    const auto& DfaProgUnanchored,
    Flags F,
    int NumGroups,
    int PrefixLen = 0,
    int PrefixStripLen = 0,
    int SuffixLen = 0,
    int SuffixStripLen = 0,
    Direction Dir = Direction::Forward,
    const auto& ForwardAst = Ast,
    const auto& ProbeDfas = DfaProg>
struct HybridMatcher {
  static MatchResult<NumGroups> doMatch(std::string_view input) noexcept {
    // Fast-reject via minimum input length
    if constexpr (Ast.min_width > 0) {
      if (input.size() < static_cast<std::size_t>(Ast.min_width)) {
        return MatchResult<NumGroups>{};
      }
    }

    if constexpr (Dir == Direction::Reverse) {
      return doMatchReverse(input);
    } else {
      return doMatchForward(input);
    }
  }

  static MatchResult<NumGroups> doMatchForward(
      std::string_view input) noexcept {
    // Fast-reject via literal prefix/suffix byte comparison
    if constexpr (PrefixLen > 0) {
      if (input.substr(0, PrefixLen) != Ast.literal_prefix()) {
        return MatchResult<NumGroups>{};
      }
    }
    if constexpr (SuffixLen > 0) {
      if (input.substr(input.size() - SuffixLen) != Ast.literal_suffix()) {
        return MatchResult<NumGroups>{};
      }
    }

    // When prefix/suffix was stripped from AST, match on trimmed input
    if constexpr (PrefixStripLen > 0 || SuffixStripLen > 0) {
      auto outcome = matchEngine(
          InputView<Dir>{
              input,
              input.substr(0, PrefixStripLen),
              input.substr(input.size() - SuffixStripLen)});
      if (outcome.status == MatchStatus::Matched) {
        for (int i = 0; i <= NumGroups; ++i) {
          auto& g = outcome.state.groups[i];
          if (g.offset != std::string_view::npos) {
            g.offset += PrefixStripLen;
          }
        }
        outcome.state.groups[0] = {0, input.size()};

        for (int i = 0; i < Ast.prefix_group_adjustment_count; ++i) {
          const auto& adj = Ast.prefix_group_adjustments[i];
          auto& g = outcome.state.groups[adj.group_id];
          if (g.offset != std::string_view::npos && g.length > 0) {
            g.offset -= adj.chars_stripped;
            g.length += adj.chars_stripped;
          } else {
            g.offset = adj.prefix_offset;
            g.length = adj.chars_stripped;
          }
        }

        return fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    } else {
      return fromOutcome(matchEngine(InputView<Dir>{input}), input);
    }
  }

  static MatchResult<NumGroups> doMatchReverse(
      std::string_view input) noexcept {
    // For reverse: the reversed AST's "prefix" (un-reversed) is the original
    // pattern's trailing literal — check at END of input. The "suffix"
    // (un-reversed) is the original pattern's leading literal — check at START.
    if constexpr (PrefixLen > 0) {
      if (input.substr(input.size() - PrefixLen) != Ast.literal_prefix()) {
        return MatchResult<NumGroups>{};
      }
    }
    if constexpr (SuffixLen > 0) {
      if (input.substr(0, SuffixLen) != Ast.literal_suffix()) {
        return MatchResult<NumGroups>{};
      }
    }

    if constexpr (PrefixStripLen > 0 || SuffixStripLen > 0) {
      // Reverse: left strip = SuffixStripLen, right strip = PrefixStripLen.
      auto outcome = matchEngine(
          InputView<Dir>{
              input,
              input.substr(0, SuffixStripLen),
              input.substr(input.size() - PrefixStripLen)});
      if (outcome.status == MatchStatus::Matched) {
        // Shift all group offsets by the left trim amount.
        for (int i = 0; i <= NumGroups; ++i) {
          auto& g = outcome.state.groups[i];
          if (g.offset != std::string_view::npos) {
            g.offset += SuffixStripLen;
          }
        }
        outcome.state.groups[0] = {0, input.size()};

        // Fix up groups within the reversed AST's prefix (at the END of
        // input). prefix_offset is relative to the reversed prefix start;
        // after un-reversal, position mapping is:
        //   input_offset = input.size() - prefix_offset - chars_stripped
        for (int i = 0; i < Ast.prefix_group_adjustment_count; ++i) {
          const auto& adj = Ast.prefix_group_adjustments[i];
          auto& g = outcome.state.groups[adj.group_id];
          if (g.offset != std::string_view::npos && g.length > 0) {
            // Group straddles prefix boundary — extend rightward into prefix.
            g.length += adj.chars_stripped;
          } else {
            // Group entirely within the prefix.
            g.offset = input.size() - adj.prefix_offset - adj.chars_stripped;
            g.length = adj.chars_stripped;
          }
        }

        return fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    } else {
      return fromOutcome(matchEngine(InputView<Dir>{input}), input);
    }
  }

  static MatchResult<NumGroups> suffixVerifiedSearch(
      std::string_view input) noexcept {
    std::size_t searchStart = 0;
    while (searchStart <= input.size()) {
      auto remaining = input.substr(searchStart);
      auto result = searchImpl(remaining);
      if (!result) {
        break;
      }

      auto matchStartAbs =
          static_cast<std::size_t>(result[0].data() - input.data());
      auto matchEndAbs = matchStartAbs + result[0].size();

      if (matchEndAbs + SuffixStripLen <= input.size()) {
        if (input.substr(matchEndAbs, SuffixStripLen) ==
            Ast.literal_suffix().substr(SuffixLen - SuffixStripLen)) {
          MatchResult<NumGroups> adjusted;
          adjusted.matched_ = true;
          adjusted.groups_[0] = std::string_view{
              input.data() + matchStartAbs, result[0].size() + SuffixStripLen};
          for (int i = 1; i <= NumGroups; ++i) {
            adjusted.groups_[i] = result[i];
          }
          return adjusted;
        }
      }

      searchStart = matchStartAbs + 1;
    }
    return MatchResult<NumGroups>{};
  }

  // Reverse-direction search with stripped prefix/suffix. The engine operates
  // on the stripped reversed AST, so each candidate match must be verified
  // against the stripped prefix (at the match's right end) and stripped
  // suffix (at the match's left end).
  static MatchResult<NumGroups> reverseStrippedSearch(
      std::string_view input) noexcept {
    std::size_t budget = computeBudget(input);
    auto scanResult = positionScanSearch(
        input,
        [&budget, &input](
            std::string_view in, std::size_t start) -> MatchOutcome<NumGroups> {
          auto outcome = tryMatchAtImpl(in, start, budget);
          if (outcome.status != MatchStatus::Matched) {
            return outcome;
          }
          auto matchStart = outcome.state.groups[0].offset;
          auto matchLen = outcome.state.groups[0].length;
          auto matchEnd = matchStart + matchLen;

          // Verify stripped prefix at the right end of match.
          if constexpr (PrefixStripLen > 0) {
            if (matchEnd + PrefixStripLen > input.size()) {
              outcome.status = MatchStatus::NoMatch;
              return outcome;
            }
            constexpr auto strippedPrefix =
                Ast.literal_prefix().substr(PrefixLen - PrefixStripLen);
            if (input.substr(matchEnd, PrefixStripLen) != strippedPrefix) {
              outcome.status = MatchStatus::NoMatch;
              return outcome;
            }
          }

          // Verify stripped suffix at the left end of match.
          if constexpr (SuffixStripLen > 0) {
            constexpr auto strippedSuffix =
                Ast.literal_suffix().substr(SuffixLen - SuffixStripLen);
            if (matchStart < SuffixStripLen) {
              outcome.status = MatchStatus::NoMatch;
              return outcome;
            }
            if (input.substr(matchStart - SuffixStripLen, SuffixStripLen) !=
                strippedSuffix) {
              outcome.status = MatchStatus::NoMatch;
              return outcome;
            }
          }

          // Extend group 0 to include stripped prefix and suffix.
          outcome.state.groups[0].offset =
              matchStart - static_cast<std::size_t>(SuffixStripLen);
          outcome.state.groups[0].length =
              matchLen + PrefixStripLen + SuffixStripLen;
          return outcome;
        });

    if (scanResult.result) {
      return scanResult.result;
    }
    if (scanResult.budgetExhausted) {
      // Fall back to NFA search without prefix/suffix verification.
      // TODO: add prefix/suffix verification to NFA fallback path.
      return searchImpl(input);
    }
    return MatchResult<NumGroups>{};
  }

  static MatchResult<NumGroups> doSearch(std::string_view input) noexcept {
    if constexpr (Ast.min_width > 0) {
      if (input.size() < static_cast<std::size_t>(Ast.min_width)) {
        return MatchResult<NumGroups>{};
      }
    }
    if constexpr (
        Dir == Direction::Reverse &&
        (PrefixStripLen > 0 || SuffixStripLen > 0)) {
      return reverseStrippedSearch(input);
    } else if constexpr (PrefixStripLen > 0) {
      return prefixSearch(input);
    } else if constexpr (SuffixStripLen > 0) {
      return suffixVerifiedSearch(input);
    } else {
      return searchImpl(input);
    }
  }

  static bool doTest(std::string_view input) noexcept {
    if constexpr (Ast.min_width > 0) {
      if (input.size() < static_cast<std::size_t>(Ast.min_width)) {
        return false;
      }
    }
    if constexpr (
        Dir == Direction::Reverse &&
        (PrefixStripLen > 0 || SuffixStripLen > 0)) {
      return static_cast<bool>(doSearch(input));
    } else if constexpr (SuffixStripLen > 0) {
      return static_cast<bool>(doSearch(input));
    } else if constexpr (PrefixStripLen > 0) {
      return prefixTest(input);
    } else {
      return testImpl(input);
    }
  }

 private:
  // DfaRunner alias with NFA program and ForwardAst for probe evaluation.
  template <bool TC, int NG, Direction D>
  using Dfa = DfaRunner<
      DfaProg,
      DfaProgUnanchored,
      Ast,
      F,
      TC,
      NG,
      D,
      NfaProg,
      ForwardAst,
      ProbeDfas>;

  // Acceleration constexprs for search position scanning.
  static constexpr bool kAnchored = hasAnchorBegin(Ast, Ast.root);
  static constexpr auto kFilter = extractFirstCharFilter(Ast, Ast.root);
  static constexpr auto kReqLit = extractRequiredLiteral(Ast, Ast.root);
  static constexpr bool kUseMemchr = kReqLit.found && !kAnchored &&
      Dir == Direction::Forward &&
      (kFilter.accepts_all ||
       (!kFilter.accepts_all && !kFilter.test(kReqLit.ch)));
  static constexpr auto kSingleFirstChar = extractSingleFirstChar(kFilter);

  // Result from positionScanSearch — either a match result or budget
  // exhaustion.
  struct SearchScanResult {
    MatchResult<NumGroups> result;
    bool budgetExhausted = false;
  };

  // Position-scanning search: iterates candidate start positions and calls
  // tryMatch at each. The scanning is always left-to-right (for leftmost
  // match semantics) regardless of executor direction. Acceleration
  // (memchr, firstCharFilter) is used when available.
  // The budget is shared across all positions — if it runs out, the scan
  // stops and returns budgetExhausted=true.
  template <typename TryMatchFn>
  static SearchScanResult positionScanSearch(
      std::string_view input, TryMatchFn&& tryMatch) noexcept {
    std::size_t maxStart = kAnchored ? 0 : input.size();

    auto handleOutcome = [&](const MatchOutcome<NumGroups>& outcome)
        -> std::pair<bool, SearchScanResult> {
      if (outcome.status == MatchStatus::Matched) {
        return {true, {fromOutcome(outcome, input), false}};
      }
      if (outcome.status == MatchStatus::BudgetExhausted) {
        return {true, {MatchResult<NumGroups>{}, true}};
      }
      return {false, {}};
    };

    if constexpr (kUseMemchr) {
      const char* data = input.data();
      const char* end = data + input.size();
      const char* scan = data;

      while (scan < end) {
        const void* found = std::memchr(scan, kReqLit.ch, end - scan);
        if (!found) {
          break;
        }
        std::size_t litPos = static_cast<const char*>(found) - data;

        std::size_t tryStart = litPos;
        if constexpr (!kFilter.accepts_all) {
          while (tryStart > 0 && kFilter.test(input[tryStart - 1])) {
            --tryStart;
          }
        } else {
          tryStart = 0;
        }

        for (std::size_t start = tryStart; start <= litPos && start <= maxStart;
             ++start) {
          if constexpr (!kFilter.accepts_all) {
            if (!kFilter.test(input[start])) {
              continue;
            }
          }
          auto [done, r] = handleOutcome(tryMatch(input, start));
          if (done) {
            return r;
          }
        }
        scan = static_cast<const char*>(found) + 1;
      }
      return {};
    } else if constexpr (kSingleFirstChar.found && Dir == Direction::Forward) {
      const char* data = input.data();
      const char* end = data + input.size();
      const char* scan = data;

      while (scan < end) {
        const void* found = std::memchr(scan, kSingleFirstChar.ch, end - scan);
        if (!found) {
          break;
        }
        std::size_t start = static_cast<const char*>(found) - data;
        if (start > maxStart) {
          break;
        }
        auto [done, r] = handleOutcome(tryMatch(input, start));
        if (done) {
          return r;
        }
        scan = static_cast<const char*>(found) + 1;
      }
      return {};
    } else {
      for (std::size_t start = InputView<Dir>{input}.scanStart();
           !InputView<Dir>{input}.scanDone(start);
           start = InputView<Dir>::advance(start)) {
        if constexpr (!kFilter.accepts_all && Dir == Direction::Forward) {
          if (start < input.size() && !kFilter.test(input[start])) {
            continue;
          }
        }
        auto [done, r] = handleOutcome(tryMatch(input, start));
        if (done) {
          return r;
        }
      }
      return {};
    }
  }

  // Try matching at a single position using the current engine.
  // For budget-limited backtracking, shares the budget across calls.
  static MatchOutcome<NumGroups> tryMatchAtImpl(
      std::string_view input, std::size_t start, std::size_t& budget) noexcept {
    InputView<Dir> iv{input};
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      return Dfa<true, NumGroups, Dir>::tryMatchFrom(iv, start);
    } else if constexpr (
        hasFlag(F, Flags::ForceBacktracking) || !Ast.nfa_compatible ||
        Ast.backtrack_safe) {
      return BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, false, Dir>::
          tryMatchAt(iv, start, budget);
    } else {
      return BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, true, Dir>::
          tryMatchAt(iv, start, budget);
    }
  }

  static constexpr std::size_t computeBudget(std::string_view input) noexcept {
    return input.size() * static_cast<std::size_t>(Ast.node_count) * 8 + 1024;
  }

  // Like tryMatchAtImpl but with TrackCaptures=false for test-only queries.
  static MatchOutcome<0> tryTestAtImpl(
      std::string_view input, std::size_t start, std::size_t& budget) noexcept {
    InputView<Dir> iv{input};
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      return Dfa<false, 0, Dir>::tryMatchFrom(iv, start);
    } else if constexpr (
        hasFlag(F, Flags::ForceBacktracking) || !Ast.nfa_compatible ||
        Ast.backtrack_safe) {
      return BacktrackRunner<Ast, ForwardAst, F, false, 0, false, Dir>::
          tryMatchAt(iv, start, budget);
    } else {
      return BacktrackRunner<Ast, ForwardAst, F, false, 0, true, Dir>::
          tryMatchAt(iv, start, budget);
    }
  }

  static MatchResult<NumGroups> prefixSearch(std::string_view input) noexcept {
    constexpr auto prefixLen = static_cast<std::size_t>(PrefixLen);
    const char firstChar = Ast.literal_prefix()[0];
    std::size_t pos = 0;
    std::size_t budget = computeBudget(input);

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

      if (input.substr(pos, PrefixLen) != Ast.literal_prefix()) {
        ++pos;
        continue;
      }

      auto remaining = input.substr(pos + prefixLen);
      auto outcome = tryMatchAtImpl(remaining, 0, budget);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        break;
      }
      if (outcome.status == MatchStatus::Matched) {
        auto result = fromOutcome(outcome, remaining);
        if (result && result[0].data() == remaining.data()) {
          if constexpr (SuffixStripLen > 0) {
            auto matchEnd = static_cast<std::size_t>(
                result[0].data() + result[0].size() - input.data());
            if (matchEnd + SuffixStripLen > input.size()) {
              ++pos;
              continue;
            }
            constexpr auto strippedSuffix =
                Ast.literal_suffix().substr(SuffixLen - SuffixStripLen);
            if (input.substr(matchEnd, SuffixStripLen) != strippedSuffix) {
              ++pos;
              continue;
            }
          }

          MatchResult<NumGroups> adjusted;
          adjusted.matched_ = true;
          adjusted.groups_[0] = std::string_view{
              input.data() + pos,
              prefixLen + result[0].size() + SuffixStripLen};
          for (int i = 1; i <= NumGroups; ++i) {
            adjusted.groups_[i] = result[i];
          }

          for (int i = 0; i < Ast.prefix_group_adjustment_count; ++i) {
            const auto& adj = Ast.prefix_group_adjustments[i];
            auto& g = adjusted.groups_[adj.group_id];
            if (!g.empty()) {
              g = std::string_view(
                  input.data() + pos + adj.prefix_offset,
                  adj.chars_stripped + g.size());
            } else {
              g = std::string_view(
                  input.data() + pos + adj.prefix_offset, adj.chars_stripped);
            }
          }

          return adjusted;
        }
      }
      ++pos;
    }
    return MatchResult<NumGroups>{};
  }

  static bool prefixTest(std::string_view input) noexcept {
    constexpr auto prefixLen = static_cast<std::size_t>(PrefixLen);
    const char firstChar = Ast.literal_prefix()[0];
    std::size_t pos = 0;
    std::size_t budget = computeBudget(input);

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

      if (input.substr(pos, PrefixLen) != Ast.literal_prefix()) {
        ++pos;
        continue;
      }

      auto remaining = input.substr(pos + prefixLen);
      auto outcome = tryMatchAtImpl(remaining, 0, budget);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        break;
      }
      if (outcome.status == MatchStatus::Matched) {
        auto result = fromOutcome(outcome, remaining);
        if (result && result[0].data() == remaining.data()) {
          if constexpr (SuffixStripLen > 0) {
            auto matchEnd = static_cast<std::size_t>(
                result[0].data() + result[0].size() - input.data());
            if (matchEnd + SuffixStripLen > input.size()) {
              ++pos;
              continue;
            }
            constexpr auto strippedSuffix =
                Ast.literal_suffix().substr(SuffixLen - SuffixStripLen);
            if (input.substr(matchEnd, SuffixStripLen) != strippedSuffix) {
              ++pos;
              continue;
            }
          }
          return true;
        }
      }
      ++pos;
    }
    return false;
  }

  static MatchResult<NumGroups> searchImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      auto outcome =
          NfaRunner<NfaProg, Ast, F, true, NumGroups, Dir, ForwardAst>::
              search(InputView<Dir>{input});
      if (outcome.status == MatchStatus::Matched) {
        return fromOutcome(outcome, input);
      }
      return MatchResult<NumGroups>{};
    } else {
      std::size_t budget = computeBudget(input);
      auto scanResult = positionScanSearch(
          input, [&budget](std::string_view in, std::size_t start) {
            return tryMatchAtImpl(in, start, budget);
          });
      if (scanResult.budgetExhausted) {
        // Budget exhausted — fall back to NFA search on full input
        if constexpr (Ast.nfa_compatible) {
          auto outcome =
              NfaRunner<NfaProg, Ast, F, true, NumGroups, Dir, ForwardAst>::
                  search(InputView<Dir>{input});
          if (outcome.status == MatchStatus::Matched) {
            return fromOutcome(outcome, input);
          }
        }
        return MatchResult<NumGroups>{};
      }
      return scanResult.result;
    }
  }

  static bool testImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      return NfaRunner<NfaProg, Ast, F, false, NumGroups, Dir, ForwardAst>::
                 search(InputView<Dir>{input})
                     .status == MatchStatus::Matched;
    } else {
      // Try unanchored DFA single-pass first (forward only, fastest path)
      if constexpr (DfaProg.valid && !hasFlag(F, Flags::ForceBacktracking)) {
        if (Dfa<false, 0, Dir>::testUnanchored(InputView<Dir>{input})) {
          return true;
        }
      }
      // Position scan with no captures
      InputView<Dir> iv{input};
      std::size_t budget = computeBudget(input);
      for (std::size_t start = iv.scanStart(); !iv.scanDone(start);
           start = InputView<Dir>::advance(start)) {
        if constexpr (!kFilter.accepts_all && Dir == Direction::Forward) {
          if (start < input.size() && !kFilter.test(input[start])) {
            continue;
          }
        }
        auto outcome = tryTestAtImpl(input, start, budget);
        if (outcome.status == MatchStatus::Matched) {
          return true;
        }
        if (outcome.status == MatchStatus::BudgetExhausted) {
          if constexpr (Ast.nfa_compatible) {
            return NfaPositionSearcher<NfaProg, Ast, F, Dir, ForwardAst>::
                testMatch(InputView<Dir>{input});
          }
          return false;
        }
      }
      return false;
    }
  }

 private:
  static MatchOutcome<NumGroups> matchEngine(InputView<Dir> input) noexcept {
    return matchEngineImpl<Dir>(input);
  }

  template <Direction D>
  static MatchOutcome<NumGroups> matchEngineImpl(InputView<D> input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceDFA)) {
      return Dfa<true, NumGroups, D>::matchAnchored(input);
    } else if constexpr (hasFlag(F, Flags::ForceNFA)) {
      return NfaRunner<NfaProg, Ast, F, true, NumGroups, D, ForwardAst>::
          matchAnchored(input);
    } else if constexpr (hasFlag(F, Flags::ForceBacktracking)) {
      return BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, false, D>::
          matchAnchored(input);
    } else if constexpr (!Ast.nfa_compatible) {
      return BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, false, D>::
          matchAnchored(input);
    } else if constexpr (Ast.backtrack_safe) {
      return BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, false, D>::
          matchAnchored(input);
    } else {
      auto outcome =
          BacktrackRunner<Ast, ForwardAst, F, true, NumGroups, true, D>::
              matchAnchored(input);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        if constexpr (DfaProg.valid) {
          return Dfa<true, NumGroups, D>::matchAnchored(input);
        } else {
          return NfaRunner<NfaProg, Ast, F, true, NumGroups, D, ForwardAst>::
              matchAnchored(input);
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

} // namespace detail

// Each static constexpr member of a templated struct gets its own
// constexpr evaluation step limit. ConstexprHolder exploits this:
// wrapping a computation in ConstexprHolder<lambda> gives it an
// independent step budget, allowing complex patterns to compile that
// would otherwise exceed the limit when all stages share one context.
template <auto Fn>
struct ConstexprHolder {
  static constexpr auto value = Fn();
};

namespace detail {

// ---------------------------------------------------------------------------
// AstHolder: owns the parsed AST and probe store. Keyed on (Pat, CompFlags)
// so that different execution-flag variants share the same AST.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct AstHolder {
  // Helper: parse, apply flags, and optimize into an AstBuilder.
  static constexpr void buildAst_(AstBuilder& builder) {
    builder.valid = true;
    Parser parser(std::string_view{Pat.data, Pat.length()}, builder);
    builder.root = parser.parseRegex();
    if (builder.valid && !parser.atEnd()) {
      parser.error("Unexpected character after pattern");
    }
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
    if (hasFlag(CompFlags, Flags::Multiline)) {
      applyMultilineFlag(builder, builder.root);
    }
    applyDotAllFlag(builder, builder.root, hasFlag(CompFlags, Flags::DotAll));
    assignProbeIds(builder, builder.root);
    if constexpr (hasFlag(CompFlags, Flags::ForceReverseExecution)) {
      reverseAst(builder, builder.root);
    }
    optimizeAst(
        builder,
        builder.root,
        hasFlag(CompFlags, Flags::ForceReverseExecution));

    // For reverse execution, the optimizer extracted prefix/suffix from the
    // reversed AST, so the stored characters are in reversed order. Un-reverse
    // them so that HybridMatcher can use simple memcmp-based string_view
    // comparisons against the original input.
    if constexpr (hasFlag(CompFlags, Flags::ForceReverseExecution)) {
      for (int i = 0; i < builder.prefix_len / 2; ++i) {
        char tmp = builder.literal_buf[i];
        builder.literal_buf[i] =
            builder.literal_buf[builder.prefix_len - 1 - i];
        builder.literal_buf[builder.prefix_len - 1 - i] = tmp;
      }
      int suffStart = builder.literal_buf_size - builder.suffix_len;
      for (int i = 0; i < builder.suffix_len / 2; ++i) {
        char tmp = builder.literal_buf[suffStart + i];
        builder.literal_buf[suffStart + i] =
            builder.literal_buf[suffStart + builder.suffix_len - 1 - i];
        builder.literal_buf[suffStart + builder.suffix_len - 1 - i] = tmp;
      }
    }
  }

  // Phase 1: Parse + optimize, compute precise output sizes.
  static constexpr auto counts_ = ConstexprHolder<[] {
    AstBuilder builder(32, 32, 8, Pat.length());
    buildAst_(builder);
    markUnreachableNodes(builder);
    return countLiveEntries(builder);
  }>::value;

  // Phase 2: Parse + optimize again, compact into precisely-sized output.
  static constexpr auto& parsed_ = ConstexprHolder<[] {
    AstBuilder builder(32, 32, 8, Pat.length());
    buildAst_(builder);
    markUnreachableNodes(builder);
    return compact<counts_>(builder);
  }>::value;

  static constexpr int NumGroups = parsed_.group_count;

  // Compute which probe_ids survived optimization in the main AST.
  // After optimization, some possessive repeats may have been eliminated
  // (e.g., (?>non-backtracking) is unwrapped). Only surviving probe_ids
  // need forward AST subtrees in the ProbeStore.
  static constexpr auto survivingProbeIds_ = ConstexprHolder<[] {
    FixedBitset<parsed_.probe_count + 1> surviving;
    for (int i = 0; i < parsed_.node_count; ++i) {
      if (parsed_.nodes[i].probe_id >= 0) {
        surviving.set(parsed_.nodes[i].probe_id);
      }
    }
    return surviving;
  }>::value;

  static constexpr bool kNeedsProbes = [] {
    for (int i = 0; i < parsed_.probe_count + 1; ++i) {
      if (survivingProbeIds_.test(i)) {
        return true;
      }
    }
    return false;
  }();

  // Helper: parse, apply flags, and optimize into a forward AstBuilder.
  // Used only for probe extraction — the full forward-optimized AST is
  // built but only surviving probe subtrees are kept.
  static constexpr void buildForwardAst_(AstBuilder& builder) {
    builder.valid = true;
    Parser parser(std::string_view{Pat.data, Pat.length()}, builder);
    builder.root = parser.parseRegex();
    if (builder.valid && !parser.atEnd()) {
      parser.error("Unexpected character after pattern");
    }
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
    if (hasFlag(CompFlags, Flags::Multiline)) {
      applyMultilineFlag(builder, builder.root);
    }
    applyDotAllFlag(builder, builder.root, hasFlag(CompFlags, Flags::DotAll));
    assignProbeIds(builder, builder.root);
    // NO reversal — always forward
    optimizeAst(builder, builder.root, false);
  }

  // ProbeStore: compact storage of surviving probe subtrees.
  // Replaces forwardParsed_ — stores only the nodes/ranges/char_classes
  // that probes actually reference.
  static constexpr auto probeSizes_ = ConstexprHolder<[] {
    if constexpr (kNeedsProbes) {
      AstBuilder builder(32, 32, 8, Pat.length());
      buildForwardAst_(builder);
      return extractAndCountProbes(builder, survivingProbeIds_);
    } else {
      return ProbeStoreSizes{1, 1, 1, 1, 1};
    }
  }>::value;

  static constexpr auto& probeStore_ = ConstexprHolder<[] {
    if constexpr (kNeedsProbes) {
      AstBuilder builder(32, 32, 8, Pat.length());
      buildForwardAst_(builder);
      return compactProbeStore<probeSizes_>(builder, survivingProbeIds_);
    } else {
      return ProbeStore<ProbeStoreSizes{1, 1, 1, 1, 1}>{};
    }
  }>::value;

  static constexpr auto kDir = hasFlag(CompFlags, Flags::ForceReverseExecution)
      ? Direction::Reverse
      : Direction::Forward;
};

// ---------------------------------------------------------------------------
// NfaHolder: owns the NFA program. Keyed on (Pat, CompFlags).
// Only builds the NFA when the pattern is NFA-compatible.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct NfaHolder {
  static constexpr auto& parsed_ = AstHolder<Pat, CompFlags>::parsed_;
  static constexpr auto& probeStore_ = AstHolder<Pat, CompFlags>::probeStore_;
  static constexpr bool kNeedsProbes = AstHolder<Pat, CompFlags>::kNeedsProbes;
  static constexpr auto kDir = AstHolder<Pat, CompFlags>::kDir;

  static constexpr auto& nfaProg_ = ConstexprHolder<[] {
    if constexpr (parsed_.nfa_compatible) {
      if constexpr (kNeedsProbes) {
        return buildNfa<parsed_.node_count * 4 + 16, kDir>(
            parsed_, &probeStore_);
      } else {
        return buildNfa<parsed_.node_count * 4 + 16, kDir>(parsed_);
      }
    } else {
      return NfaProgram<1>{};
    }
  }>::value;
};

// ---------------------------------------------------------------------------
// DfaHolder: owns the DFA programs (anchored + unanchored). Keyed on
// (Pat, CompFlags). Only builds when a viable NFA exists.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct DfaHolder {
  static constexpr auto& parsed_ = AstHolder<Pat, CompFlags>::parsed_;
  static constexpr auto& nfaProg_ = NfaHolder<Pat, CompFlags>::nfaProg_;
  static constexpr auto kDir = AstHolder<Pat, CompFlags>::kDir;

  static constexpr bool kHasNfa = nfaProg_.start_state >= 0;

  // Unrolled NFA for DFA construction: small CountedRepeat states are
  // expanded into explicit copies, eliminating them from the NFA so the
  // DFA's subset construction handles them correctly. The compact
  // nfaProg_ (with CountedRepeats) continues serving the NFA runner.
  static constexpr int kMaxUnrolledNfaStates =
      !kHasNfa ? 1 : (nfaProg_.state_count + nfaProg_.num_counters * 36 + 16);

  static constexpr auto& nfaProgDfa_ = ConstexprHolder<[] {
    if constexpr (kHasNfa) {
      return unrollCountedRepeats<kMaxUnrolledNfaStates>(nfaProg_);
    } else {
      return NfaProgram<1>{};
    }
  }>::value;

  // This limit is set from experimental testing. Changes to various things
  // may increase or lower this limit.
  static constexpr int kMaxDfaStates = !kHasNfa ? 0 : 3584;

  static constexpr auto& dfaIntervalSets_ = ConstexprHolder<[] {
    if constexpr (kHasNfa && kMaxDfaStates > 0) {
      return buildDfaIntervalSets(nfaProgDfa_);
    } else {
      return DfaIntervalSets<1>{};
    }
  }>::value;

  static constexpr auto& dfaReachMap_ = ConstexprHolder<[] {
    if constexpr (kHasNfa && kMaxDfaStates > 0) {
      return buildDfaReachMap(nfaProgDfa_);
    } else {
      return DfaReachMap<1>{};
    }
  }>::value;

  static constexpr auto& dfaCore_ = ConstexprHolder<[] {
    if constexpr (kHasNfa && kMaxDfaStates > 0) {
      return buildDfaCore<kMaxDfaStates, false, kDir>(
          nfaProgDfa_, parsed_, dfaIntervalSets_, dfaReachMap_);
    } else {
      return DfaCoreResult<1>{};
    }
  }>::value;

  static constexpr auto& dfaProg_ = ConstexprHolder<[] {
    if constexpr (dfaCore_.valid) {
      constexpr int kStates = dfaCore_.state_count;
      constexpr int kClasses = computeDfaClassCount(dfaCore_, nfaProgDfa_);
      constexpr int kTagEntries = dfaCore_.tag_entry_count;
      auto prog = buildDfaFinalize<kStates, kClasses, kTagEntries>(
          dfaCore_, nfaProgDfa_);
      // Remaining CountedRepeats after unrolling cannot be handled
      // correctly by the DFA without runtime counters. Mark invalid
      // so the system falls back to NFA/backtracking.
      if (nfaProgDfa_.num_counters > 0) {
        prog.valid = false;
      }
      // Build pre-compiled possessive probes from the NFA's probe states.
      // Each probe is linearized into a sequence of interval masks for
      // fast table-driven reverse verification at runtime.
      if constexpr (nfaProg_.has_possessive && nfaProg_.partition.valid) {
        prog.probe_partition = nfaProg_.partition;
        for (int si = 0; si < nfaProg_.state_count; ++si) {
          const auto& ns = nfaProg_.states[si];
          if (ns.possessive_probe_idx < 0 ||
              ns.possessive_probe_idx >= nfaProg_.probe_count) {
            continue;
          }
          if (prog.probe_count >= decltype(prog)::kMaxProbes) {
            break;
          }
          DfaPossessiveProbe probe;
          // Atomic groups (possessive {1,1}) wrap an inner expression
          // that can consume any number of characters. Use unbounded
          // semantics so the probe rejects whenever the inner can match.
          probe.max_repeat =
              (ns.min_repeat == 1 && ns.max_repeat == 1) ? -1 : ns.max_repeat;
          int pst = nfaProg_.probe_start[ns.possessive_probe_idx];
          // Track visited states to detect loops in the probe NFA.
          DynamicBitset probeVisited;
          probeVisited.reserve(nfaProg_.state_count);
          while (pst >= 0 && pst < nfaProg_.state_count &&
                 probe.step_count < DfaPossessiveProbe::kMaxSteps) {
            if (probeVisited.test(pst)) {
              // Loop detected — the inner expression can repeat.
              // The steps collected so far represent one iteration;
              // if we matched any consuming states the probe is valid.
              probe.valid = (probe.step_count > 0);
              break;
            }
            probeVisited.set(pst);
            const auto& ps = nfaProg_.states[pst];
            if (ps.kind == NfaStateKind::Match) {
              probe.valid = (probe.step_count > 0);
              break;
            }
            if (ps.kind == NfaStateKind::Split ||
                ps.kind == NfaStateKind::GroupStart ||
                ps.kind == NfaStateKind::GroupEnd) {
              pst = ps.next >= 0 ? ps.next : ps.alt;
              continue;
            }
            // Consuming state — grab its interval mask
            probe.masks[probe.step_count++] = ps.interval_mask;
            pst = ps.next;
          }
          prog.probes[prog.probe_count++] = probe;
        }
        prog.has_possessive_probes = (prog.probe_count > 0);
      }
      return prog;
    } else {
      return DfaProgram<1, 1, 1>{};
    }
  }>::value;

  static constexpr bool kAnchored = hasAnchorBegin(parsed_, parsed_.root);

  // Check for any anchor in the AST. When present, we skip building the
  // unanchored DFA entirely.
  static constexpr bool kHasAnyAnchor = hasAnyAnchor(parsed_, parsed_.root);

  // Unanchored DFA: built with restart self-loops so the DFA never goes
  // dead when a viable match restart exists. Skipped for anchored patterns
  // and patterns with large NFAs (the restart closure increases constexpr
  // evaluation cost beyond the step budget for complex patterns).
  static constexpr int kMaxNfaForUnanchored = 40;

  static constexpr auto& dfaCoreUnanchored_ = ConstexprHolder<[] {
    if constexpr (
        kHasNfa && kMaxDfaStates > 0 && !kHasAnyAnchor &&
        nfaProg_.state_count <= kMaxNfaForUnanchored &&
        nfaProgDfa_.num_counters == 0) {
      return buildDfaCore<kMaxDfaStates, true, kDir>(
          nfaProgDfa_, parsed_, dfaIntervalSets_, dfaReachMap_);
    } else {
      return DfaCoreResult<1>{};
    }
  }>::value;

  static constexpr auto& dfaProgUnanchored_ = ConstexprHolder<[] {
    if constexpr (dfaCoreUnanchored_.valid) {
      constexpr int kStates = dfaCoreUnanchored_.state_count;
      constexpr int kClasses =
          computeDfaClassCount(dfaCoreUnanchored_, nfaProgDfa_);
      constexpr int kTagEntries = dfaCoreUnanchored_.tag_entry_count;
      return buildDfaFinalize<kStates, kClasses, kTagEntries>(
          dfaCoreUnanchored_, nfaProgDfa_);
    } else {
      return DfaProgram<1, 1, 1>{};
    }
  }>::value;

  // Build probe sub-DFAs for lookaround runtime evaluation.
  // Each probe gets its own small DFA built from the ProbeStore AST.
  static constexpr bool kNeedsProbes = AstHolder<Pat, CompFlags>::kNeedsProbes;
  static constexpr auto& probeStore_ = AstHolder<Pat, CompFlags>::probeStore_;

  // Per-probe DFA: builds a standalone NFA from each ProbeStore entry,
  // then a DFA from it. Stored as a flat array of small DfaPrograms.
  struct ProbeDfaEntry {
    int start_anchored = -1;
    int state_count = 0;
    bool valid = false;
    // Simplified transition table for small probe DFAs
    static constexpr int kMaxStates = 64;
    static constexpr int kMaxClasses = 64;
    uint8_t char_to_class[256] = {};
    int num_classes = 0;
    int16_t transitions[kMaxClasses][kMaxStates] = {};
    FixedBitset<kMaxStates> accepting;
    FixedBitset<kMaxStates> accepting_at_end;
  };

  static constexpr int kMaxProbeDfas = 8;

  static constexpr auto& probeDfas_ = ConstexprHolder<[] {
    struct ProbeDfaSet {
      ProbeDfaEntry entries[kMaxProbeDfas] = {};
      int count = 0;
    };
    ProbeDfaSet result;
    if constexpr (kNeedsProbes && kHasNfa) {
      for (int pid = 0; pid < probeStore_.probe_count && pid < kMaxProbeDfas;
           ++pid) {
        if (!probeStore_.hasProbe(pid)) {
          continue;
        }
        // Build a standalone NFA from this probe's AST subtree
        auto probeNfa =
            buildNfa<ProbeDfaEntry::kMaxStates, Direction::Forward>(
                probeStore_);
        if (probeNfa.start_state < 0) {
          continue;
        }
        // Override start state to the probe's root
        // Actually, we need to build from the probe's specific root.
        // buildNfa uses ast.root, but probeStore_.root is kNoNode.
        // We need a custom build that starts from probes[pid].root.

        // Build NFA fragment for this probe's subtree
        constexpr int kProbeNfaMax = ProbeDfaEntry::kMaxStates;
        NfaProgram<kProbeNfaMax> pnfa;
        using PSType = std::remove_cvref_t<decltype(probeStore_)>;
        NfaBuilder<kProbeNfaMax, Direction::Forward, PSType, const void*>
            builder{pnfa, probeStore_, nullptr};
        auto frag = builder.build(probeStore_.probes[pid].root);
        NfaState matchState;
        matchState.kind = NfaStateKind::Match;
        int matchIdx = pnfa.addState(matchState);
        builder.patch(frag.end, matchIdx);
        pnfa.start_state = frag.start;

        // Compute alphabet partition
        AlphabetPartition& part = pnfa.partition;
        for (int si = 0; si < pnfa.state_count; ++si) {
          const auto& s = pnfa.states[si];
          if (s.kind == NfaStateKind::Literal) {
            auto uc = static_cast<unsigned char>(s.ch);
            part.addBoundary(uc);
            if (uc < 255) {
              part.addBoundary(uc + 1);
            }
          } else if (s.kind == NfaStateKind::CharClass) {
            const auto& cc =
                probeStore_.char_classes[s.char_class_index];
            for (int r = 0; r < cc.range_count; ++r) {
              part.addBoundary(
                  probeStore_.ranges[cc.range_offset + r].lo);
              auto hi = probeStore_.ranges[cc.range_offset + r].hi;
              if (hi < 255) {
                part.addBoundary(hi + 1);
              }
            }
          }
        }
        part.sort();
        if (part.intervalCount() <= 64) {
          part.valid = true;
          for (int si = 0; si < pnfa.state_count; ++si) {
            auto& s = pnfa.states[si];
            uint64_t mask = 0;
            if (s.kind == NfaStateKind::Literal) {
              int iv = part.charToInterval(
                  static_cast<unsigned char>(s.ch));
              mask = uint64_t{1} << iv;
            } else if (s.kind == NfaStateKind::AnyByte) {
              mask = (part.intervalCount() >= 64)
                  ? ~uint64_t{0}
                  : (uint64_t{1} << part.intervalCount()) - 1;
            } else if (s.kind == NfaStateKind::CharClass) {
              const auto& cc =
                  probeStore_.char_classes[s.char_class_index];
              for (int r = 0; r < cc.range_count; ++r) {
                auto lo = probeStore_.ranges[cc.range_offset + r].lo;
                auto hi = probeStore_.ranges[cc.range_offset + r].hi;
                for (int iv = part.charToInterval(lo);
                     iv < part.intervalCount(); ++iv) {
                  auto rep = part.representative(iv);
                  if (rep > hi) {
                    break;
                  }
                  mask |= uint64_t{1} << iv;
                }
              }
            }
            s.interval_mask = mask;
          }
        }

        // Build DFA from probe NFA
        auto ivSets = buildDfaIntervalSets(pnfa);
        auto reachMap = buildDfaReachMap(pnfa);
        auto core = buildDfaCore<ProbeDfaEntry::kMaxStates, false,
                                 Direction::Forward>(
            pnfa, probeStore_, ivSets, reachMap);
        if (!core.valid) {
          continue;
        }
        auto dfaProg = buildDfaFinalize<
            ProbeDfaEntry::kMaxStates,
            ProbeDfaEntry::kMaxClasses>(core, pnfa);
        if (!dfaProg.valid) {
          continue;
        }

        auto& entry = result.entries[pid];
        entry.start_anchored = dfaProg.start_anchored;
        entry.state_count = dfaProg.state_count;
        entry.valid = true;
        entry.num_classes = dfaProg.num_classes;
        for (int c = 0; c < 256; ++c) {
          entry.char_to_class[c] = dfaProg.char_to_class[c];
        }
        for (int cls = 0; cls < dfaProg.num_classes; ++cls) {
          for (int s = 0; s < dfaProg.state_count; ++s) {
            entry.transitions[cls][s] = dfaProg.transitions[cls][s];
          }
        }
        entry.accepting.copyFrom(dfaProg.accepting);
        entry.accepting_at_end.copyFrom(dfaProg.accepting_at_end);
        if (pid >= result.count) {
          result.count = pid + 1;
        }
      }
    }
    return result;
  }>::value;
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

  static constexpr Flags kCompFlags = compilationFlags(F);

  // Shared compilation artifacts, keyed on (Pat, kCompFlags).
  // Multiple Regex instantiations with the same pattern and compilation
  // flags but different execution flags (ForceBacktracking, ForceNFA,
  // ForceDFA) share the same AstHolder/NfaHolder/DfaHolder, avoiding
  // redundant constexpr evaluation.
  using Ast = detail::AstHolder<Pat, kCompFlags>;
  using Nfa = detail::NfaHolder<Pat, kCompFlags>;
  using Dfa = detail::DfaHolder<Pat, kCompFlags>;

  // Public aliases for backward compatibility — tests and internal code
  // access these directly on Regex<Pat, F>.
  static constexpr auto& parsed_ = Ast::parsed_;
  static constexpr int NumGroups = Ast::NumGroups;
  static constexpr auto& probeStore_ = Ast::probeStore_;
  static constexpr auto& nfaProg_ = Nfa::nfaProg_;
  static constexpr auto& dfaProg_ = Dfa::dfaProg_;
  static constexpr auto& dfaProgUnanchored_ = Dfa::dfaProgUnanchored_;
  static constexpr auto& probeDfas_ = Dfa::probeDfas_;
  static constexpr auto kDir = Ast::kDir;

  using Matcher = detail::HybridMatcher<
      parsed_,
      nfaProg_,
      dfaProg_,
      dfaProgUnanchored_,
      F,
      NumGroups,
      parsed_.prefix_len,
      parsed_.prefix_strip_len,
      parsed_.suffix_len,
      parsed_.suffix_strip_len,
      kDir,
      probeStore_,
      probeDfas_>;

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
