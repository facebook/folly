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

#include <folly/regex/Flags.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstOptimizer.h>
#include <folly/regex/detail/AstSerializer.h>
#include <folly/regex/detail/BacktrackExecutor.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Dfa.h>
#include <folly/regex/detail/DfaExecutor.h>
#include <folly/regex/detail/Direction.h>
#include <folly/regex/detail/Executor.h>
#include <folly/regex/detail/Nfa.h>
#include <folly/regex/detail/NfaExecutor.h>
#include <folly/regex/detail/Parser.h>

namespace folly::regex {

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

namespace detail {

// Constexpr-friendly map of named-capture-group name → group_id pairs.
// Entries are precisely sized via the template parameter; no runtime
// count field is needed. Names use byte-exact (case-sensitive)
// comparison per Perl/PCRE2 semantics — the CaseInsensitive flag
// affects input matching, not pattern-level identifiers.
template <int MaxEntries>
struct NamedGroupMap {
  NamedGroupEntry entries[MaxEntries > 0 ? MaxEntries : 1] = {};

  // Returns the group_id for `name`, or -1 if not found. Uses linear
  // search for small maps and binary search (entries assumed sorted by
  // name) for larger ones. The threshold is conservative — at <=4
  // entries the linear scan typically beats branchier binary search.
  constexpr int find(std::string_view name) const noexcept {
    if constexpr (MaxEntries <= 4) {
      for (int i = 0; i < MaxEntries; ++i) {
        if (entries[i].nameView() == name) {
          return entries[i].group_id;
        }
      }
      return -1;
    } else {
      int lo = 0;
      int hi = MaxEntries - 1;
      while (lo <= hi) {
        int mid = (lo + hi) / 2;
        auto midName = entries[mid].nameView();
        if (midName == name) {
          return entries[mid].group_id;
        }
        if (midName < name) {
          lo = mid + 1;
        } else {
          hi = mid - 1;
        }
      }
      return -1;
    }
  }
};

// Default (empty) name map used by MatchResult when the pattern has no
// named groups. find() always returns -1; operator[](string_view) on a
// MatchResult with this map always returns an empty string_view.
inline constexpr NamedGroupMap<0> kEmptyNameMap{};

} // namespace detail

template <int NumGroups, const auto& NameMap = detail::kEmptyNameMap>
struct MatchResult {
  bool matched_ = false;
  std::array<std::string_view, NumGroups + 1> groups_ = {};

  explicit operator bool() const noexcept { return matched_; }

  std::string_view operator[](std::size_t i) const noexcept {
    return groups_[i];
  }

  // Named-group access. Returns an empty string_view if the name is not
  // found. Byte-exact (case-sensitive) lookup. Unambiguous with the
  // size_t overload because integer literals don't implicitly convert
  // to std::string_view.
  std::string_view operator[](std::string_view name) const noexcept {
    int idx = NameMap.find(name);
    if (idx < 0 || static_cast<std::size_t>(idx) > NumGroups) {
      return {};
    }
    return groups_[idx];
  }

  static constexpr std::size_t size() noexcept { return NumGroups + 1; }

  template <std::size_t I>
  std::string_view get() const noexcept {
    static_assert(I <= static_cast<std::size_t>(NumGroups));
    return groups_[I];
  }
};

} // namespace folly::regex

template <int NumGroups, const auto& NameMap>
struct std::tuple_size<folly::regex::MatchResult<NumGroups, NameMap>>
    : std::integral_constant<std::size_t, NumGroups + 1> {};

template <std::size_t I, int NumGroups, const auto& NameMap>
struct std::tuple_element<I, folly::regex::MatchResult<NumGroups, NameMap>> {
  using type = std::string_view;
};

namespace folly::regex {

template <std::size_t I, int NumGroups, const auto& NameMap>
std::string_view get(const MatchResult<NumGroups, NameMap>& m) noexcept {
  static_assert(I <= static_cast<std::size_t>(NumGroups));
  return m.groups_[I];
}

namespace detail {

template <
    const auto& Ast,
    const auto& NfaProg,
    const auto& DfaProg,
    const auto& DfaProgUnanchored,
    Flags F,
    int NumGroups,
    int PrefixLen = 0,
    int PrefixStripLen = 0,
    Direction Dir = Direction::Forward,
    const auto& ForwardAst = Ast,
    const auto& ProbeDfas = DfaProg,
    const auto& NameMap = kEmptyNameMap>
struct HybridMatcher {
  static_assert(ReadOnlyAst<std::remove_cvref_t<decltype(Ast)>>);
  // Strip execution-only flags so that
  // NfaRunner/NfaPositionSearcher instantiations are deduped across modes.
  // Strip execution-only flags so that
  // executor instantiations are deduped across engine modes
  // (ForceNFA/ForceDFA/ForceBacktracking).
  static constexpr Flags kCompFlags = normalizedCompilationFlags(F);
  // The exact match-result type returned by all match/search functions
  // in this matcher. Wraps the runtime captures together with the
  // pattern's name→id map so the public-facing API can do `m["name"]`
  // lookup at runtime via byte-exact string_view comparison.
  using ResultT = MatchResult<NumGroups, NameMap>;
  static ResultT doMatch(std::string_view input) noexcept {
    // Fast-reject via minimum input length
    if constexpr (Ast.min_width > 0) {
      if (input.size() < static_cast<std::size_t>(Ast.min_width)) {
        return ResultT{};
      }
    }

    if constexpr (Dir == Direction::Reverse) {
      return doMatchReverse(input);
    } else {
      return doMatchForward(input);
    }
  }

  static ResultT doMatchForward(std::string_view input) noexcept {
    // Fast-reject via literal prefix byte comparison
    if constexpr (PrefixLen > 0) {
      if (input.substr(0, PrefixLen) != Ast.literal_prefix()) {
        return ResultT{};
      }
    }

    // When prefix was stripped from AST, match on trimmed input
    if constexpr (PrefixStripLen > 0) {
      auto outcome = matchEngine(
          InputView<Dir>{
              input, Ast.literal_prefix().substr(0, PrefixStripLen)});
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
      return ResultT{};
    } else {
      return fromOutcome(matchEngine(InputView<Dir>{input}), input);
    }
  }

  static ResultT doMatchReverse(std::string_view input) noexcept {
    // For reverse: the reversed AST's "prefix" (un-reversed) is the original
    // pattern's trailing literal — check at END of input.
    if constexpr (PrefixLen > 0) {
      if (input.substr(input.size() - PrefixLen) != Ast.literal_prefix()) {
        return ResultT{};
      }
    }

    if constexpr (PrefixStripLen > 0) {
      // Reverse: right strip = PrefixStripLen.
      auto outcome = matchEngine(
          InputView<Dir>{
              input, Ast.literal_prefix().substr(0, PrefixStripLen)});
      if (outcome.status == MatchStatus::Matched) {
        outcome.state.groups[0] = {0, input.size()};

        // Fix up groups within the reversed AST's prefix (at the END of
        // input). prefix_offset is relative to the reversed prefix start;
        // after un-reversal, position mapping is:
        //   input_offset = input.size() - prefix_offset - chars_stripped
        //
        // The adjustment's chars_stripped records chars in the FULL prefix,
        // but only the first PrefixStripLen chars are actually stripped.
        // Compute the overlap between the group's prefix range and the
        // stripped range to get the actual chars needing adjustment.
        for (int i = 0; i < Ast.prefix_group_adjustment_count; ++i) {
          const auto& adj = Ast.prefix_group_adjustments[i];
          auto& g = outcome.state.groups[adj.group_id];

          int groupEnd = adj.prefix_offset + adj.chars_stripped;
          int overlapEnd =
              groupEnd < PrefixStripLen ? groupEnd : PrefixStripLen;
          int actualStripped = overlapEnd > adj.prefix_offset
              ? overlapEnd - adj.prefix_offset
              : 0;

          if (actualStripped <= 0) {
            // Group is entirely outside the stripped range — the engine
            // already handled it; no adjustment needed.
            continue;
          }

          if (g.offset != std::string_view::npos && g.length > 0) {
            // Group straddles prefix boundary — extend rightward into prefix.
            g.length += actualStripped;
          } else {
            // Group entirely within the stripped prefix.
            g.offset = input.size() - adj.prefix_offset - actualStripped;
            g.length = actualStripped;
          }
        }

        return fromOutcome(outcome, input);
      }
      return ResultT{};
    } else {
      return fromOutcome(matchEngine(InputView<Dir>{input}), input);
    }
  }

  // Reverse-direction search with stripped prefix. The engine operates
  // on the stripped reversed AST, so each candidate match must be verified
  // against the stripped prefix (at the match's right end).
  static ResultT reverseStrippedSearch(std::string_view input) noexcept {
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

          // Extend group 0 to include stripped prefix.
          outcome.state.groups[0].offset = matchStart;
          outcome.state.groups[0].length = matchLen + PrefixStripLen;
          return outcome;
        });

    if (scanResult.result) {
      return scanResult.result;
    }
    if (scanResult.budgetExhausted) {
      // Fall back to NFA search without prefix verification.
      // TODO: add prefix verification to NFA fallback path.
      return searchImpl(input);
    }
    return ResultT{};
  }

  static ResultT doSearch(std::string_view input) noexcept {
    if constexpr (Ast.min_width > 0) {
      if (input.size() < static_cast<std::size_t>(Ast.min_width)) {
        return ResultT{};
      }
    }
    if constexpr (Dir == Direction::Reverse && PrefixStripLen > 0) {
      return reverseStrippedSearch(input);
    } else if constexpr (PrefixStripLen > 0) {
      return prefixSearch(input);
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
    if constexpr (Dir == Direction::Reverse && PrefixStripLen > 0) {
      return static_cast<bool>(doSearch(input));
    } else if constexpr (PrefixStripLen > 0) {
      return prefixTest(input);
    } else {
      return testImpl(input);
    }
  }

 private:
  template <bool TC, int NG, Direction D>
  using Dfa = DfaRunner<
      DfaProg,
      DfaProgUnanchored,
      Ast,
      kCompFlags,
      TC,
      NG,
      D,
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
    ResultT result;
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
        return {true, {ResultT{}, true}};
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
      return BacktrackRunner<
          Ast,
          ForwardAst,
          kCompFlags,
          true,
          NumGroups,
          false,
          Dir>::tryMatchAt(iv, start, budget);
    } else {
      return BacktrackRunner<
          Ast,
          ForwardAst,
          kCompFlags,
          true,
          NumGroups,
          true,
          Dir>::tryMatchAt(iv, start, budget);
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
      return BacktrackRunner<
          Ast,
          ForwardAst,
          kCompFlags,
          false,
          0,
          false,
          Dir>::tryMatchAt(iv, start, budget);
    } else {
      return BacktrackRunner<Ast, ForwardAst, kCompFlags, false, 0, true, Dir>::
          tryMatchAt(iv, start, budget);
    }
  }

  static ResultT prefixSearch(std::string_view input) noexcept {
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
          ResultT adjusted;
          adjusted.matched_ = true;
          adjusted.groups_[0] = std::string_view{
              input.data() + pos, prefixLen + result[0].size()};
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
    return ResultT{};
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
          return true;
        }
      }
      ++pos;
    }
    return false;
  }

  static ResultT searchImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      auto pos =
          NfaPositionSearcher<NfaProg, Ast, kCompFlags, Dir, ForwardAst>::
              findFirst(InputView<Dir>{input});
      if (!pos.found) {
        return ResultT{};
      }
      auto iv = InputView<Dir>{input}.narrowTo(pos.start, pos.end);
      auto outcome = NfaRunner<
          NfaProg,
          Ast,
          kCompFlags,
          true,
          NumGroups,
          Dir,
          ForwardAst>::matchAnchored(iv);
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
      return ResultT{};
    } else if constexpr (hasFlag(F, Flags::ForceDFA)) {
      static_assert(DfaProg.valid, "ForceDFA requires a valid DFA program");
      auto scanResult =
          positionScanSearch(input, [](std::string_view in, std::size_t start) {
            return Dfa<true, NumGroups, Dir>::tryMatchFrom(
                InputView<Dir>{in}, start);
          });
      return scanResult.result;
    } else {
      std::size_t budget = computeBudget(input);
      auto scanResult = positionScanSearch(
          input, [&budget](std::string_view in, std::size_t start) {
            return tryMatchAtImpl(in, start, budget);
          });
      if (scanResult.budgetExhausted) {
        // Budget exhausted — try DFA position scan before NFA fallback
        if constexpr (DfaProg.valid && !hasFlag(F, Flags::ForceBacktracking)) {
          auto dfaScan = positionScanSearch(
              input, [](std::string_view in, std::size_t start) {
                return Dfa<true, NumGroups, Dir>::tryMatchFrom(
                    InputView<Dir>{in}, start);
              });
          if (dfaScan.result) {
            return dfaScan.result;
          }
        }
        // Fall back to NFA findFirst + matchAnchored
        if constexpr (
            Ast.nfa_compatible && !hasFlag(F, Flags::ForceBacktracking)) {
          auto pos =
              NfaPositionSearcher<NfaProg, Ast, kCompFlags, Dir, ForwardAst>::
                  findFirst(InputView<Dir>{input});
          if (!pos.found) {
            return ResultT{};
          }
          auto iv = InputView<Dir>{input}.narrowTo(pos.start, pos.end);
          auto outcome = NfaRunner<
              NfaProg,
              Ast,
              kCompFlags,
              true,
              NumGroups,
              Dir,
              ForwardAst>::matchAnchored(iv);
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
        } else {
          // This shouldn't be possible, since the backtracker should have
          // run without a budget if it wasn't NFA compatible.
          folly::throw_exception<std::logic_error>(
              "Budget exhausted on non-NFA compatible pattern");
        }
        return ResultT{};
      }
      return scanResult.result;
    }
  }

  static bool testImpl(std::string_view input) noexcept {
    if constexpr (hasFlag(F, Flags::ForceNFA)) {
      static_assert(
          Ast.nfa_compatible, "ForceNFA requires an NFA-compatible pattern");
      return NfaRunner<
                 NfaProg,
                 Ast,
                 kCompFlags,
                 false,
                 NumGroups,
                 Dir,
                 ForwardAst>::search(InputView<Dir>{input})
                 .status == MatchStatus::Matched;
    } else if constexpr (hasFlag(F, Flags::ForceDFA)) {
      static_assert(
          DfaProg.valid, "ForceDFA requires a DFA-compatible pattern");
      return Dfa<false, 0, Dir>::testUnanchored(InputView<Dir>{input});
    } else {
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
        if constexpr (!Ast.backtrack_safe) {
          if (outcome.status == MatchStatus::BudgetExhausted) {
            // Try unanchored DFA single-pass first (forward only, fastest path)
            if constexpr (
                DfaProgUnanchored.valid &&
                !hasFlag(F, Flags::ForceBacktracking)) {
              return Dfa<false, 0, Dir>::testUnanchored(InputView<Dir>{input});
            } else if constexpr (
                Ast.nfa_compatible && !hasFlag(F, Flags::ForceBacktracking)) {
              return NfaPositionSearcher<
                  NfaProg,
                  Ast,
                  kCompFlags,
                  Dir,
                  ForwardAst>::testMatch(InputView<Dir>{input});
            } else {
              // This shouldn't be possible, since the backtracker should have
              // run without a budget if it wasn't NFA compatible.
              folly::throw_exception<std::logic_error>(
                  "Budget exhausted on non-NFA compatible pattern");
            }
            return false;
          }
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
      return NfaRunner<
          NfaProg,
          Ast,
          kCompFlags,
          true,
          NumGroups,
          D,
          ForwardAst>::matchAnchored(input);
    } else if constexpr (
        hasFlag(F, Flags::ForceBacktracking) || !Ast.nfa_compatible ||
        Ast.backtrack_safe) {
      return BacktrackRunner<
          Ast,
          ForwardAst,
          kCompFlags,
          true,
          NumGroups,
          false,
          D>::matchAnchored(input);
    } else {
      auto outcome = BacktrackRunner<
          Ast,
          ForwardAst,
          kCompFlags,
          true,
          NumGroups,
          true,
          D>::matchAnchored(input);
      if (outcome.status == MatchStatus::BudgetExhausted) {
        if constexpr (DfaProg.valid) {
          return Dfa<true, NumGroups, D>::matchAnchored(input);
        } else {
          return NfaRunner<
              NfaProg,
              Ast,
              kCompFlags,
              true,
              NumGroups,
              D,
              ForwardAst>::matchAnchored(input);
        }
      }
      return outcome;
    }
  }

  static ResultT fromOutcome(
      const MatchOutcome<NumGroups>& outcome, std::string_view input) noexcept {
    ResultT result;
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
// AstHolderOptimized: re-parses the optimized regex string, produces parsed_
// and nameMap_. Top-level template so semantically equivalent patterns
// that produce the same OptPat share the same instantiation.
// ---------------------------------------------------------------------------
template <auto OptPat, Flags CompFlags, auto SC>
struct AstHolderOptimized {
  static constexpr bool kReverse =
      hasFlag(CompFlags, Flags::ForceReverseExecution);

  static constexpr void buildAstCommonSecondPass_(AstBuilder& builder) {
    // Pre-set group_count so backref validation succeeds during parsing.
    builder.group_count = SC.groupCount;
    builder.valid = true;
    Parser parser(std::string_view{OptPat.data, OptPat.length()}, builder);
    // Optimized re-parse uses SyntaxPreset_Serialized — all metadata is
    // encoded in (?~...) annotations by the serializer.
    builder.root = parser.parseRegex(Flags::SyntaxPreset_Serialized);
    if (builder.valid && !parser.atEnd()) {
      parser.error("Unexpected character after pattern");
    }
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }

    // With SyntaxPreset_Serialized:
    // - Group IDs are correct from (?~gN:...) parsing — no reassignGroupIds
    // - Probe IDs come from annotations for all directions:
    //   (?~pN:...) / (?~PN:...) for possessive, (?~=N:...) etc. for
    //   lookaround — no assignProbeIds or reassignProbeIds
    // - PossessiveProbed mode is set by (?~pN:...) parsing — no
    //   Possessive→PossessiveProbed conversion loop

    // possessivePass runs here only (not in outer, not in
    // optimizeAnalyses).
    if constexpr (kReverse) {
      // Demotion FIRST: demote PossessiveProbed → Possessive where
      // forward follow is disjoint (eliminates unnecessary probes).
      possessivePass(
          builder,
          builder.root,
          FirstCharFilter{.accepts_all = false},
          PossessivePassMode::Demote);
    }
    // Promotion: promote remaining Greedy/Lazy → Possessive where
    // follow set is disjoint.
    possessivePass(
        builder,
        builder.root,
        FirstCharFilter{.accepts_all = false},
        PossessivePassMode::Promote);

    // Copy side-channel data from outer onto builder BEFORE analyses.
    builder.prefix_len = SC.prefixLen;
    builder.prefix_strip_len = SC.prefixStripLen;
    for (int i = 0; i < SC.prefixLen; ++i) {
      builder.literal_buf[i] = SC.prefixBuf[i];
    }
    builder.prefix_group_adjustment_count = SC.prefixGroupAdjCount;
    if (SC.prefixGroupAdjCount > 0) {
      builder.ensurePrefixGroupAdjustments();
      for (int i = 0; i < SC.prefixGroupAdjCount; ++i) {
        builder.prefix_group_adjustments[i] = SC.prefixGroupAdj[i];
      }
    }
    builder.trailing_dot_star_min = SC.trailingDotStarMin;
    builder.trailing_dot_star_dot_all = SC.trailingDotStarDotAll;
    builder.trailing_dot_star_anchor = SC.trailingDotStarAnchor;
    builder.leading_dot_star_min = SC.leadingDotStarMin;
    builder.leading_dot_star_dot_all = SC.leadingDotStarDotAll;
    builder.leading_dot_star_anchor = SC.leadingDotStarAnchor;

    // Simplified inner analyses — discriminator offsets are known from
    // (?~DN:...) annotations, backtrackSafe/minWidth from side channel.
    // materializeDiscriminatorCharClasses handles both the outer pipeline
    // (where valid_discriminators exists) and the inner pipeline (where
    // it falls back to already-materialized char_class_index via
    // getBranchCharSet).
    precomputeFixedWidths(builder, builder.root);
    materializeDiscriminatorCharClasses(builder, builder.root);
    builder.backtrack_safe = SC.backtrackSafe;
    builder.min_width = SC.minWidth;
  }

  // Single pass: re-parse + analyze + compact. No counts_ needed.
  static constexpr auto& parsed_ = ConstexprHolder<[] {
    AstBuilder builder(32, 32, 8, static_cast<int>(SC.sizes.literal_buf_size));
    buildAstCommonSecondPass_(builder);
    markUnreachableNodes(builder);
    return compact<SC.sizes>(builder);
  }>::value;

  static constexpr int NumGroups = parsed_.group_count;

  static constexpr auto& nameMap_ = ConstexprHolder<[] {
    constexpr int N = parsed_.named_group_count;
    NamedGroupMap<N> map;
    for (int i = 0; i < N; ++i) {
      map.entries[i] = parsed_.named_groups[i];
    }
    return map;
  }>::value;

  // Probe analysis — depends only on parsed_, so lives here for dedup.
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

  static constexpr bool kNeedsReverseProbes = [] {
    for (int i = 0; i < parsed_.node_count; ++i) {
      if (parsed_.nodes[i].probe_id >= 0 &&
          survivingProbeIds_.test(parsed_.nodes[i].probe_id) &&
          (parsed_.nodes[i].kind == NodeKind::Lookbehind ||
           parsed_.nodes[i].kind == NodeKind::NegLookbehind) &&
          parsed_.nodes[i].min_repeat < 0) {
        return true;
      }
    }
    return false;
  }();

  static constexpr auto fwdSurvivingProbeIds_ = ConstexprHolder<[] {
    FixedBitset<parsed_.probe_count + 1> fwd;
    for (int i = 0; i < parsed_.node_count; ++i) {
      int pid = parsed_.nodes[i].probe_id;
      if (pid >= 0 && survivingProbeIds_.test(pid)) {
        auto kind = parsed_.nodes[i].kind;
        if (kind != NodeKind::Lookbehind && kind != NodeKind::NegLookbehind) {
          fwd.set(pid);
        } else if (parsed_.nodes[i].min_repeat >= 0) {
          fwd.set(pid);
        }
      }
    }
    return fwd;
  }>::value;

  static constexpr auto revSurvivingProbeIds_ = ConstexprHolder<[] {
    FixedBitset<parsed_.probe_count + 1> rev;
    for (int i = 0; i < parsed_.node_count; ++i) {
      int pid = parsed_.nodes[i].probe_id;
      if (pid >= 0 && survivingProbeIds_.test(pid)) {
        auto kind = parsed_.nodes[i].kind;
        if ((kind == NodeKind::Lookbehind || kind == NodeKind::NegLookbehind) &&
            parsed_.nodes[i].min_repeat < 0) {
          rev.set(pid);
        }
      }
    }
    return rev;
  }>::value;

  // Probe store — extracted directly from parsed_ (no re-parsing needed).
};

// ---------------------------------------------------------------------------
// AstHolder: owns the parsed AST and probe store. Keyed on (Pat, CompFlags)
// so that different execution-flag variants share the same AST.
//
// Runs the full outer pipeline once (parse + transform + analyze + serialize)
// to produce the optimized regex, then delegates parsed_ to AstHolderOptimized.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct AstHolder {
  static constexpr bool kReverse =
      hasFlag(CompFlags, Flags::ForceReverseExecution);
  static constexpr std::size_t kUpperBound =
      UpperBoundSerializedLen(Pat.length());

  struct OuterResult {
    SerializedPattern<kUpperBound> optimizedPat;
    ParseSizes sizes;
    int groupCount = 0;
    // Prefix side-channel.
    char prefixBuf[Pat.length() > 0 ? Pat.length() : 1] = {};
    int prefixLen = 0;
    int prefixStripLen = 0;
    PrefixGroupAdjustment prefixGroupAdj[Pat.length() > 0 ? Pat.length() : 1] =
        {};
    int prefixGroupAdjCount = 0;
    // Dot-star side-channel.
    int trailingDotStarMin = -1;
    bool trailingDotStarDotAll = false;
    int trailingDotStarAnchor = -1;
    int leadingDotStarMin = -1;
    bool leadingDotStarDotAll = false;
    int leadingDotStarAnchor = -1;
    bool backtrackSafe = false;
    int minWidth = 0;
    constexpr bool operator==(const OuterResult&) const = default;
  };

  // Outer pipeline: parse + transform + analyze + serialize + count.
  static constexpr auto outerResult_ = ConstexprHolder<[] {
    AstBuilder builder(32, 32, 8, Pat.length());
    builder.valid = true;
    Parser parser(std::string_view{Pat.data, Pat.length()}, builder);
    builder.root = parser.parseRegex(compilationFlags(CompFlags));
    if (builder.valid && !parser.atEnd()) {
      parser.error("Unexpected character after pattern");
    }
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
    assignProbeIds(builder, builder.root);
    if constexpr (kReverse) {
      reverseAst(builder, builder.root);
    }
    // Outer does NOT call possessivePass — it runs only in the
    // inner, AFTER Possessive→PossessiveProbed conversion.
    optimizeTransformations(builder, builder.root, kReverse);
    optimizeAnalyses(builder, builder.root, kReverse);

    // Snapshot side-channel data.
    OuterResult result;
    result.groupCount = builder.group_count;
    result.prefixLen = builder.prefix_len;
    result.prefixStripLen = builder.prefix_strip_len;
    for (int i = 0; i < builder.prefix_len; ++i) {
      result.prefixBuf[i] = builder.literal_buf[i];
    }
    // Un-reverse prefix for reverse execution.
    if constexpr (kReverse) {
      for (int i = 0; i < result.prefixLen / 2; ++i) {
        char tmp = result.prefixBuf[i];
        result.prefixBuf[i] = result.prefixBuf[result.prefixLen - 1 - i];
        result.prefixBuf[result.prefixLen - 1 - i] = tmp;
      }
    }
    result.prefixGroupAdjCount = builder.prefix_group_adjustment_count;
    for (int i = 0; i < builder.prefix_group_adjustment_count; ++i) {
      result.prefixGroupAdj[i] = builder.prefix_group_adjustments[i];
    }
    result.trailingDotStarMin = builder.trailing_dot_star_min;
    result.trailingDotStarDotAll = builder.trailing_dot_star_dot_all;
    result.trailingDotStarAnchor = builder.trailing_dot_star_anchor;
    result.leadingDotStarMin = builder.leading_dot_star_min;
    result.leadingDotStarDotAll = builder.leading_dot_star_dot_all;
    result.leadingDotStarAnchor = builder.leading_dot_star_anchor;
    result.backtrackSafe = builder.backtrack_safe;
    result.minWidth = builder.min_width;

    // Serialize post-analysis, post-strip AST with serialized mode enabled
    // to emit (?~...) annotation constructs for the inner re-parse.
    markUnreachableNodes(builder);
    result.optimizedPat = serializeAstToRegex<kUpperBound>(builder, true);
    result.sizes = countLiveEntries(builder);
    // literal_buf_size must include prefix length.
    result.sizes.literal_buf_size += result.prefixLen;
    return result;
  }>::value;

  // Alias into the sibling AstHolderOptimized (shared across equivalent
  // patterns).
  using Optimized =
      AstHolderOptimized<outerResult_.optimizedPat, CompFlags, outerResult_>;

  // Forward parsed_ and nameMap_ from Optimized.
  static constexpr auto& parsed_ = Optimized::parsed_;
  static constexpr int NumGroups = Optimized::NumGroups;
  static constexpr auto& nameMap_ = Optimized::nameMap_;

  // Forward probe analysis from Optimized.
  static constexpr auto& survivingProbeIds_ = Optimized::survivingProbeIds_;
  static constexpr bool kNeedsProbes = Optimized::kNeedsProbes;
  static constexpr bool kNeedsReverseProbes = Optimized::kNeedsReverseProbes;
  static constexpr auto& fwdSurvivingProbeIds_ =
      Optimized::fwdSurvivingProbeIds_;
  static constexpr auto& revSurvivingProbeIds_ =
      Optimized::revSurvivingProbeIds_;

  // Probe store: forward probes come from the forward parsed_, reverse
  // probes (variable-width lookbehind) come from the reverse parsed_.
  // Both reference their respective AstHolderOptimized instances.
  static constexpr Flags kFwdCompFlags = static_cast<Flags>(
      static_cast<unsigned>(CompFlags) &
      ~static_cast<unsigned>(Flags::ForceReverseExecution));
  static constexpr Flags kRevCompFlags = static_cast<Flags>(
      static_cast<unsigned>(CompFlags) |
      static_cast<unsigned>(Flags::ForceReverseExecution));

  using FwdOptimized = AstHolderOptimized<
      AstHolder<Pat, kFwdCompFlags>::outerResult_.optimizedPat,
      kFwdCompFlags,
      AstHolder<Pat, kFwdCompFlags>::outerResult_>;

  static constexpr auto& fwdParsed_ = FwdOptimized::parsed_;
  static constexpr auto& fwdSurvivingProbeIds__ =
      FwdOptimized::fwdSurvivingProbeIds_;

  static constexpr auto probeSizes_ = ConstexprHolder<[] {
    if constexpr (kNeedsProbes) {
      auto fwdSizes = extractAndCountProbes(fwdParsed_, fwdSurvivingProbeIds__);
      if constexpr (kNeedsReverseProbes) {
        using RevOptimized = AstHolderOptimized<
            AstHolder<Pat, kRevCompFlags>::outerResult_.optimizedPat,
            kRevCompFlags,
            AstHolder<Pat, kRevCompFlags>::outerResult_>;
        auto revSizes =
            extractAndCountProbes(RevOptimized::parsed_, revSurvivingProbeIds_);
        return ProbeStoreSizes{
            fwdSizes.max_nodes + revSizes.max_nodes,
            fwdSizes.max_ranges + revSizes.max_ranges,
            fwdSizes.max_classes + revSizes.max_classes,
            fwdSizes.literal_chars_size + revSizes.literal_chars_size,
            fwdSizes.max_probes > revSizes.max_probes
                ? fwdSizes.max_probes
                : revSizes.max_probes};
      } else {
        return fwdSizes;
      }
    } else {
      return ProbeStoreSizes{1, 1, 1, 1, 1};
    }
  }>::value;

  static constexpr auto& probeStore_ = ConstexprHolder<[] {
    if constexpr (kNeedsProbes) {
      if constexpr (kNeedsReverseProbes) {
        // Extract forward probes from forward parsed_
        auto store =
            compactProbeStore<probeSizes_>(fwdParsed_, fwdSurvivingProbeIds__);
        // Extract reverse probes from reverse parsed_ and merge
        using RevOptimized = AstHolderOptimized<
            AstHolder<Pat, kRevCompFlags>::outerResult_.optimizedPat,
            kRevCompFlags,
            AstHolder<Pat, kRevCompFlags>::outerResult_>;
        auto revStore = compactProbeStore<probeSizes_>(
            RevOptimized::parsed_, revSurvivingProbeIds_);
        // Merge reverse probe nodes into forward store
        int nodeBase = store.node_count;
        int ccBase = store.char_class_count;
        int rangeBase = store.total_ranges;
        int litBase = store.literal_chars_count;
        for (int n = 0; n < revStore.node_count; ++n) {
          auto node = revStore.nodes[n];
          if (node.child_first >= 0)
            node.child_first += nodeBase;
          if (node.child_last >= 0)
            node.child_last += nodeBase;
          if (node.next_sibling >= 0)
            node.next_sibling += nodeBase;
          if (node.prev_sibling >= 0)
            node.prev_sibling += nodeBase;
          if (node.char_class_index >= 0)
            node.char_class_index += ccBase;
          if (node.kind == NodeKind::Literal && !node.literal.empty()) {
            int len = static_cast<int>(node.literal.size());
            for (int c = 0; c < len; ++c) {
              store.literal_chars[litBase + c] = node.literal[c];
            }
            node.literal = std::string_view(
                store.literal_chars + litBase, node.literal.size());
            litBase += len;
          }
          store.nodes[store.node_count++] = node;
        }
        store.literal_chars_count = litBase;
        for (int c = 0; c < revStore.char_class_count; ++c) {
          auto cc = revStore.char_classes[c];
          cc.range_offset += rangeBase;
          store.char_classes[store.char_class_count++] = cc;
        }
        for (int r = 0; r < revStore.total_ranges; ++r) {
          store.ranges[store.total_ranges++] = revStore.ranges[r];
        }
        // Merge reverse probe entries
        for (int pid = 0; pid < revStore.probe_count; ++pid) {
          if (revStore.hasProbe(pid)) {
            if (pid >= store.probe_count) {
              store.probe_count = pid + 1;
            }
            store.probes[pid].root = revStore.probes[pid].root >= 0
                ? revStore.probes[pid].root + nodeBase
                : -1;
            store.probes[pid].dir = revStore.probes[pid].dir;
            store.probes[pid].negated = revStore.probes[pid].negated;
            store.probes[pid].lookbehind_width =
                revStore.probes[pid].lookbehind_width;
          }
        }
        return store;
      } else {
        return compactProbeStore<probeSizes_>(
            fwdParsed_, fwdSurvivingProbeIds__);
      }
    } else {
      return ProbeStore<ProbeStoreSizes{1, 1, 1, 1, 1}>{};
    }
  }>::value;

  static constexpr auto kDir = hasFlag(CompFlags, Flags::ForceReverseExecution)
      ? Direction::Reverse
      : Direction::Forward;
};

// ---------------------------------------------------------------------------
// NfaHolderOptimized: owns the NFA program. Keyed on deduplicated references
// so that semantically equivalent patterns share the same NFA.
// ---------------------------------------------------------------------------
template <
    const auto& Parsed,
    const auto& ProbeStore,
    bool NeedsProbes,
    Direction Dir>
struct NfaHolderOptimized {
  static constexpr auto& nfaProg_ = ConstexprHolder<[] {
    if constexpr (Parsed.nfa_compatible) {
      if constexpr (NeedsProbes) {
        return buildNfa<Parsed.node_count * 4 + 16, Dir>(Parsed, &ProbeStore);
      } else {
        return buildNfa<Parsed.node_count * 4 + 16, Dir>(Parsed);
      }
    } else {
      return NfaProgram<1>{};
    }
  }>::value;
};

// ---------------------------------------------------------------------------
// NfaHolder: owns the NFA program. Keyed on (Pat, CompFlags).
// Aliases through NfaHolderOptimized for dedup.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct NfaHolder {
  static constexpr auto& parsed_ = AstHolder<Pat, CompFlags>::parsed_;
  static constexpr auto& probeStore_ = AstHolder<Pat, CompFlags>::probeStore_;
  static constexpr bool kNeedsProbes = AstHolder<Pat, CompFlags>::kNeedsProbes;
  static constexpr auto kDir = AstHolder<Pat, CompFlags>::kDir;

  static constexpr auto& nfaProg_ =
      NfaHolderOptimized<parsed_, probeStore_, kNeedsProbes, kDir>::nfaProg_;
};

// ---------------------------------------------------------------------------
// DfaHolderOptimized: owns the DFA build chain. Keyed on deduplicated
// references so that semantically equivalent patterns share the same DFA
// artifacts.
// ---------------------------------------------------------------------------
template <
    const auto& NfaProg,
    const auto& Parsed,
    const auto& ProbeStore,
    bool NeedsProbes,
    bool HasNfa,
    Direction Dir>
struct DfaHolderOptimized {
  static constexpr bool kHasNfa = HasNfa;

  static constexpr int kMaxUnrolledNfaStates =
      !kHasNfa ? 1 : (NfaProg.state_count + NfaProg.num_counters * 36 + 16);

  static constexpr auto& nfaProgDfa_ = ConstexprHolder<[] {
    if constexpr (kHasNfa) {
      return unrollCountedRepeats<kMaxUnrolledNfaStates>(NfaProg);
    } else {
      return NfaProgram<1>{};
    }
  }>::value;

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
      return buildDfaCore<kMaxDfaStates, false, Dir>(
          nfaProgDfa_, Parsed, dfaIntervalSets_, dfaReachMap_);
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
      if (nfaProgDfa_.num_counters > 0) {
        prog.valid = false;
      }
      if constexpr (NfaProg.has_possessive && NfaProg.partition.valid) {
        prog.probe_partition = NfaProg.partition;
        for (int si = 0; si < NfaProg.state_count; ++si) {
          const auto& ns = NfaProg.states[si];
          if (ns.possessive_probe_idx < 0 ||
              ns.possessive_probe_idx >= NfaProg.probe_count) {
            continue;
          }
          if (prog.probe_count >= decltype(prog)::kMaxProbes) {
            break;
          }
          DfaPossessiveProbe probe;
          probe.max_repeat =
              (ns.min_repeat == 1 && ns.max_repeat == 1) ? -1 : ns.max_repeat;
          int pst = NfaProg.probe_start[ns.possessive_probe_idx];
          DynamicBitset probeVisited;
          probeVisited.reserve(NfaProg.state_count);
          while (pst >= 0 && pst < NfaProg.state_count &&
                 probe.step_count < DfaPossessiveProbe::kMaxSteps) {
            if (probeVisited.test(pst)) {
              probe.valid = (probe.step_count > 0);
              break;
            }
            probeVisited.set(pst);
            const auto& ps = NfaProg.states[pst];
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

  static constexpr bool kAnchored = hasAnchorBegin(Parsed, Parsed.root);
  static constexpr bool kHasAnyAnchor = hasAnyAnchor(Parsed, Parsed.root);

  static constexpr int kMaxProbeDfas = 8;

  static constexpr auto& probeDfas_ = ConstexprHolder<[] {
    return buildProbeDfas<kMaxProbeDfas>(ProbeStore, NeedsProbes, kHasNfa);
  }>::value;
};

// ---------------------------------------------------------------------------
// DfaHolder: owns the DFA programs. Keyed on (Pat, CompFlags).
// Aliases through DfaHolderOptimized for dedup.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct DfaHolder {
  static constexpr auto& parsed_ = AstHolder<Pat, CompFlags>::parsed_;
  static constexpr auto& nfaProg_ = NfaHolder<Pat, CompFlags>::nfaProg_;
  static constexpr auto& probeStore_ = AstHolder<Pat, CompFlags>::probeStore_;
  static constexpr bool kNeedsProbes = AstHolder<Pat, CompFlags>::kNeedsProbes;
  static constexpr auto kDir = AstHolder<Pat, CompFlags>::kDir;

  static constexpr bool kHasNfa = nfaProg_.start_state >= 0;

  using Optimized = DfaHolderOptimized<
      nfaProg_,
      parsed_,
      probeStore_,
      kNeedsProbes,
      kHasNfa,
      kDir>;

  // kHasNfa aliased above, before Optimized.
  static constexpr int kMaxDfaStates = Optimized::kMaxDfaStates;
  static constexpr auto& nfaProgDfa_ = Optimized::nfaProgDfa_;
  static constexpr auto& dfaIntervalSets_ = Optimized::dfaIntervalSets_;
  static constexpr auto& dfaReachMap_ = Optimized::dfaReachMap_;
  static constexpr auto& dfaCore_ = Optimized::dfaCore_;
  static constexpr auto& dfaProg_ = Optimized::dfaProg_;
  static constexpr bool kAnchored = Optimized::kAnchored;
  static constexpr bool kHasAnyAnchor = Optimized::kHasAnyAnchor;
  static constexpr auto& probeDfas_ = Optimized::probeDfas_;
};

// ---------------------------------------------------------------------------
// UnanchoredDfaHolder: owns the unanchored DFA program. Keyed on
// (Pat, CompFlags). Built separately from DfaHolder so that accessing
// the anchored DFA (for match()) does not trigger the unanchored DFA
// build. Only instantiated when search() or test() is called.
// ---------------------------------------------------------------------------
template <FixedPattern Pat, Flags CompFlags>
struct UnanchoredDfaHolder {
  static constexpr auto& parsed_ = AstHolder<Pat, CompFlags>::parsed_;
  static constexpr auto& nfaProg_ = NfaHolder<Pat, CompFlags>::nfaProg_;

  using DfaI = DfaHolder<Pat, CompFlags>::Optimized;
  static constexpr bool kHasNfa = DfaI::kHasNfa;
  static constexpr int kMaxDfaStates = DfaI::kMaxDfaStates;
  static constexpr auto& nfaProgDfa_ = DfaI::nfaProgDfa_;
  static constexpr auto& dfaIntervalSets_ = DfaI::dfaIntervalSets_;
  static constexpr auto& dfaReachMap_ = DfaI::dfaReachMap_;
  static constexpr bool kHasAnyAnchor = DfaI::kHasAnyAnchor;
  static constexpr auto kDir = AstHolder<Pat, CompFlags>::kDir;

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

  static constexpr Flags kCompFlags = normalizedCompilationFlags(F);

  // Shared compilation artifacts, keyed on (Pat, kCompFlags).
  // Multiple Regex instantiations with the same pattern and compilation
  // flags but different execution flags (ForceBacktracking, ForceNFA,
  // ForceDFA) share the same AstHolder/NfaHolder/DfaHolder, avoiding
  // redundant constexpr evaluation.
  using Ast = detail::AstHolder<Pat, kCompFlags>;

  static constexpr auto& parsed_ = Ast::parsed_;
  static constexpr int NumGroups = Ast::NumGroups;
  static constexpr auto kDir = Ast::kDir;

  // Whether this instantiation needs NFA/DFA engines. Backtrack-safe
  // patterns in auto mode use only the backtracker, so we skip building
  // NFA/DFA to save constexpr evaluation budget.
  static constexpr bool kNeedsAutomata = hasFlag(F, Flags::ForceNFA) ||
      hasFlag(F, Flags::ForceDFA) ||
      (!hasFlag(F, Flags::ForceBacktracking) && parsed_.nfa_compatible &&
       !parsed_.backtrack_safe);

  static constexpr auto kInvalidNFA = detail::NfaProgram<1>{};
  static constexpr auto kInvalidDFA = detail::DfaProgram<1, 1, 1>{};

  static constexpr auto& nfaProg_ = []() -> const auto& {
    if constexpr (kNeedsAutomata) {
      return detail::NfaHolder<Pat, kCompFlags>::nfaProg_;
    } else {
      return kInvalidNFA;
    }
  }();

  static constexpr auto& dfaProg_ = []() -> const auto& {
    if constexpr (kNeedsAutomata) {
      return detail::DfaHolder<Pat, kCompFlags>::dfaProg_;
    } else {
      return kInvalidDFA;
    }
  }();

  static constexpr auto& dfaProgUnanchored_ = kInvalidDFA;

  static constexpr auto& probeStore_ = Ast::probeStore_;
  static constexpr bool kNeedsProbes = Ast::kNeedsProbes;

  static constexpr auto& nameMap_ = Ast::nameMap_;

  static constexpr auto& probeDfas_ = []() -> const auto& {
    if constexpr (kNeedsAutomata) {
      return detail::DfaHolder<Pat, kCompFlags>::probeDfas_;
    } else {
      return kInvalidDFA;
    }
  }();

  using Matcher = detail::HybridMatcher<
      parsed_,
      nfaProg_,
      dfaProg_,
      dfaProgUnanchored_,
      F,
      NumGroups,
      parsed_.prefix_len,
      parsed_.prefix_strip_len,
      kDir,
      probeStore_,
      probeDfas_,
      nameMap_>;

  static MatchResult<NumGroups, nameMap_> match(
      std::string_view input) noexcept {
    return Matcher::doMatch(input);
  }

  static MatchResult<NumGroups, nameMap_> search(
      std::string_view input) noexcept {
    return Matcher::doSearch(input);
  }

  static bool test(std::string_view input) noexcept {
    if constexpr (kNeedsAutomata || hasFlag(F, Flags::ForceDFA)) {
      constexpr auto& uDfa =
          detail::UnanchoredDfaHolder<Pat, kCompFlags>::dfaProgUnanchored_;
      using SearchMatcher = detail::HybridMatcher<
          parsed_,
          nfaProg_,
          dfaProg_,
          uDfa,
          F,
          NumGroups,
          parsed_.prefix_len,
          parsed_.prefix_strip_len,
          kDir,
          probeStore_,
          probeDfas_,
          nameMap_>;
      return SearchMatcher::doTest(input);
    } else {
      return Matcher::doTest(input);
    }
  }

  struct MatchIterator {
    using value_type = MatchResult<NumGroups, nameMap_>;
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

} // namespace folly::regex
