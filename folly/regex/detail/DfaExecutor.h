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
#include <string_view>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/Dfa.h>
#include <folly/regex/detail/Executor.h>

namespace folly {
namespace regex {

enum class Flags : unsigned;

namespace detail {

struct DfaCapture {
  std::size_t start = std::string_view::npos;
  std::size_t end = 0;
};

template <
    const auto& DfaProg,
    const auto& DfaProgUnanchored,
    const auto& Ast,
    Flags F,
    bool TrackCaptures,
    int NumGroups>
struct DfaRunner {
  static constexpr int16_t kHasTagsBit = 0x4000;
  static constexpr int16_t kStateMask = 0x3FFF;
  static constexpr bool kAnchored = hasAnchorBegin(Ast, Ast.root);
  static constexpr bool kHasLineOrEndAnchors =
      hasLineOrEndAnchors(Ast, Ast.root);
  static constexpr auto kFilter = extractFirstCharFilter(Ast, Ast.root);
  static constexpr auto kReqLit = extractRequiredLiteral(Ast, Ast.root);
  static constexpr bool kUseMemchr = kReqLit.found && !kAnchored &&
      (kFilter.accepts_all ||
       (!kFilter.accepts_all && !kFilter.test(kReqLit.ch)));
  static constexpr bool kUseRangeClassifier = DfaProg.use_range_classifier;

  // Classify a character into its equivalence class.  For patterns with
  // ≤2 boundary transitions (e.g. \d, [a-z]), replaces the char_to_class[]
  // table load with 1-2 immediate comparisons — pure register ops.
  static int classifyChar(unsigned char ch) noexcept {
    if constexpr (kUseRangeClassifier) {
      if constexpr (DfaProg.boundary_count >= 1) {
        if (ch < DfaProg.boundaries[0].threshold) {
          return DfaProg.boundaries[0].class_id;
        }
      }
      if constexpr (DfaProg.boundary_count >= 2) {
        if (ch < DfaProg.boundaries[1].threshold) {
          return DfaProg.boundaries[1].class_id;
        }
      }
      return DfaProg.last_class;
    } else {
      return DfaProg.char_to_class[ch];
    }
  }

  static int classifyCharUnanchored(unsigned char ch) noexcept {
    if constexpr (DfaProgUnanchored.use_range_classifier) {
      if constexpr (DfaProgUnanchored.boundary_count >= 1) {
        if (ch < DfaProgUnanchored.boundaries[0].threshold) {
          return DfaProgUnanchored.boundaries[0].class_id;
        }
      }
      if constexpr (DfaProgUnanchored.boundary_count >= 2) {
        if (ch < DfaProgUnanchored.boundaries[1].threshold) {
          return DfaProgUnanchored.boundaries[1].class_id;
        }
      }
      return DfaProgUnanchored.last_class;
    } else {
      return DfaProgUnanchored.char_to_class[ch];
    }
  }

  static void executeTagOps(
      const DfaTagEntry& entry,
      std::array<DfaCapture, NumGroups + 1>& captures,
      std::size_t pos) {
    if constexpr (TrackCaptures) {
      for (int i = 0; i < entry.count; ++i) {
        auto& op = entry.ops[i];
        if (op.is_end) {
          captures[op.group_id].end = pos;
        } else {
          captures[op.group_id].start = pos;
        }
      }
    }
  }

  static MatchOutcome<NumGroups> matchAnchored(std::string_view input) {
    MatchOutcome<NumGroups> result;

    int state = DfaProg.start_anchored;
    if (state < 0) {
      return result;
    }

    std::array<DfaCapture, NumGroups + 1> captures = {};

    if constexpr (TrackCaptures) {
      executeTagOps(
          DfaProg.tag_entries[DfaProg.start_anchored_tags], captures, 0);
    }

    for (std::size_t pos = 0; pos < input.size(); ++pos) {
      auto cls = classifyChar(static_cast<unsigned char>(input[pos]));
      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        return result;
      }
      if constexpr (TrackCaptures) {
        if (trans & kHasTagsBit) {
          auto tagIdx = DfaProg.tag_actions[cls][state];
          executeTagOps(DfaProg.tag_entries[tagIdx], captures, pos + 1);
        }
      }
      state = trans & kStateMask;
    }

    bool accepted =
        DfaProg.accepting.test(state) || DfaProg.accepting_at_end.test(state);
    if (!accepted) {
      return result;
    }

    result.status = MatchStatus::Matched;
    if constexpr (TrackCaptures) {
      result.state.groups[0] = {0, input.size()};
      for (int g = 1; g <= NumGroups; ++g) {
        if (captures[g].start != std::string_view::npos) {
          result.state.groups[g] = {
              captures[g].start, captures[g].end - captures[g].start};
        }
      }
    }
    return result;
  }

  static MatchOutcome<NumGroups> tryMatchFrom(
      std::string_view input, std::size_t startPos) {
    MatchOutcome<NumGroups> bestResult;

    int state;
    if (startPos == 0) {
      state = DfaProg.start_anchored;
    } else if (
        startPos > 0 && DfaProg.start_after_newline >= 0 &&
        input[startPos - 1] == '\n') {
      state = DfaProg.start_after_newline;
    } else {
      state = DfaProg.start_unanchored;
    }
    if (state < 0) {
      return bestResult;
    }

    std::array<DfaCapture, NumGroups + 1> captures = {};
    if constexpr (TrackCaptures) {
      uint16_t startTags;
      if (startPos == 0) {
        startTags = DfaProg.start_anchored_tags;
      } else if (
          startPos > 0 && DfaProg.start_after_newline >= 0 &&
          input[startPos - 1] == '\n') {
        startTags = DfaProg.start_after_newline_tags;
      } else {
        startTags = DfaProg.start_unanchored_tags;
      }
      executeTagOps(DfaProg.tag_entries[startTags], captures, startPos);
    }

    if (DfaProg.accepting.test(state) ||
        (startPos == input.size() && DfaProg.accepting_at_end.test(state))) {
      bestResult.status = MatchStatus::Matched;
      if constexpr (TrackCaptures) {
        bestResult.state.groups[0] = {startPos, 0};
        for (int g = 1; g <= NumGroups; ++g) {
          if (captures[g].start != std::string_view::npos) {
            bestResult.state.groups[g] = {
                captures[g].start, captures[g].end - captures[g].start};
          }
        }
      }
      if constexpr (DfaProg.match_preference == MatchPreference::AllLazy) {
        return bestResult;
      } else if constexpr (DfaProg.match_preference == MatchPreference::Mixed) {
        if (DfaProg.accept_early.test(state)) {
          return bestResult;
        }
      }
    }

    for (std::size_t pos = startPos; pos < input.size(); ++pos) {
      auto cls = classifyChar(static_cast<unsigned char>(input[pos]));

      // Multiline $: accept before '\n' without consuming it
      if (input[pos] == '\n' && DfaProg.accepting_before_newline.test(state)) {
        bestResult.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          bestResult.state.groups[0] = {startPos, pos - startPos};
          for (int g = 1; g <= NumGroups; ++g) {
            if (captures[g].start != std::string_view::npos) {
              bestResult.state.groups[g] = {
                  captures[g].start, captures[g].end - captures[g].start};
            }
          }
        }
      }

      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        // \Z: accept before trailing newline
        if (pos + 1 == input.size() && input[pos] == '\n' &&
            DfaProg.accepting_before_trailing_newline.test(state)) {
          bestResult.status = MatchStatus::Matched;
          if constexpr (TrackCaptures) {
            bestResult.state.groups[0] = {startPos, pos - startPos};
            for (int g = 1; g <= NumGroups; ++g) {
              if (captures[g].start != std::string_view::npos) {
                bestResult.state.groups[g] = {
                    captures[g].start, captures[g].end - captures[g].start};
              }
            }
          }
        }
        break;
      }
      if constexpr (TrackCaptures) {
        if (trans & kHasTagsBit) {
          auto tagIdx = DfaProg.tag_actions[cls][state];
          executeTagOps(DfaProg.tag_entries[tagIdx], captures, pos + 1);
        }
      }
      state = trans & kStateMask;

      if (DfaProg.accepting.test(state) ||
          (pos + 1 == input.size() && DfaProg.accepting_at_end.test(state))) {
        bestResult.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          bestResult.state.groups[0] = {startPos, pos + 1 - startPos};
          for (int g = 1; g <= NumGroups; ++g) {
            if (captures[g].start != std::string_view::npos) {
              bestResult.state.groups[g] = {
                  captures[g].start, captures[g].end - captures[g].start};
            }
          }
        }
        if constexpr (DfaProg.match_preference == MatchPreference::AllLazy) {
          return bestResult;
        } else if constexpr (
            DfaProg.match_preference == MatchPreference::Mixed) {
          if (DfaProg.accept_early.test(state)) {
            return bestResult;
          }
        }
      }
    }

    return bestResult;
  }

  static MatchOutcome<NumGroups> search(std::string_view input) {
    std::size_t maxStart = kAnchored ? 0 : input.size();

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
          auto result = tryMatchFrom(input, start);
          if (result.status == MatchStatus::Matched) {
            return result;
          }
        }
        scan = static_cast<const char*>(found) + 1;
      }

      return MatchOutcome<NumGroups>{};
    } else if constexpr (
        !kAnchored && DfaProgUnanchored.valid && !kHasLineOrEndAnchors) {
      // Single-pass search using unanchored DFA with restart self-loops.
      // The unanchored DFA includes the restart closure in every transition,
      // so it never goes dead for characters matching the restart path.
      int state = DfaProgUnanchored.start_unanchored;
      if (state < 0) {
        return MatchOutcome<NumGroups>{};
      }
      std::size_t matchStart = 0;
      bool haveMatch = false;
      std::size_t bestStart = 0;

      if (DfaProgUnanchored.accepting.test(state)) {
        haveMatch = true;
        bestStart = 0;
      }

      for (std::size_t pos = 0; pos < input.size(); ++pos) {
        auto cls =
            classifyCharUnanchored(static_cast<unsigned char>(input[pos]));
        auto trans = DfaProgUnanchored.transitions[cls][state];

        if (trans < 0) {
          if (haveMatch) {
            if constexpr (TrackCaptures) {
              // Scan forward from bestStart to find the leftmost match.
              // matchStart may be too early when skip characters overlap
              // with the pattern's first character class.
              for (std::size_t s = bestStart; s <= input.size(); ++s) {
                if constexpr (!kFilter.accepts_all) {
                  if (s < input.size() && !kFilter.test(input[s])) {
                    continue;
                  }
                }
                auto result = tryMatchFrom(input, s);
                if (result.status == MatchStatus::Matched) {
                  return result;
                }
              }
            } else {
              MatchOutcome<NumGroups> result;
              result.status = MatchStatus::Matched;
              return result;
            }
          }
          return MatchOutcome<NumGroups>{};
        }

        state = trans & kStateMask;

        // When we return to the restart-only state, no partial match
        // is in progress — advance matchStart past this position.
        if (state == DfaProgUnanchored.start_unanchored) {
          matchStart = pos + 1;
        }

        if (DfaProgUnanchored.accepting.test(state) ||
            (pos + 1 == input.size() &&
             DfaProgUnanchored.accepting_at_end.test(state))) {
          if (!haveMatch) {
            bestStart = matchStart;
          }
          haveMatch = true;
        }
      }

      if (!haveMatch && DfaProgUnanchored.accepting_at_end.test(state)) {
        haveMatch = true;
        bestStart = matchStart;
      }

      if (haveMatch) {
        if constexpr (TrackCaptures) {
          for (std::size_t s = bestStart; s <= input.size(); ++s) {
            if constexpr (!kFilter.accepts_all) {
              if (s < input.size() && !kFilter.test(input[s])) {
                continue;
              }
            }
            auto result = tryMatchFrom(input, s);
            if (result.status == MatchStatus::Matched) {
              return result;
            }
          }
        } else {
          MatchOutcome<NumGroups> result;
          result.status = MatchStatus::Matched;
          return result;
        }
      }
      return MatchOutcome<NumGroups>{};
    } else if constexpr (!kAnchored && kHasLineOrEndAnchors) {
      // Per-position search for patterns with line/end anchors.
      // These patterns need start_after_newline for positions after '\n',
      // which tryMatchFrom already handles.
      for (std::size_t start = 0; start <= input.size(); ++start) {
        if constexpr (!kFilter.accepts_all) {
          if (start < input.size() && !kFilter.test(input[start])) {
            continue;
          }
        }
        auto result = tryMatchFrom(input, start);
        if (result.status == MatchStatus::Matched) {
          return result;
        }
      }
      return MatchOutcome<NumGroups>{};
    } else if constexpr (!kAnchored) {
      // Fallback single-pass search using anchored DFA with dead-state reset.
      // Used when the unanchored DFA could not be built (state limit exceeded).
      int state = DfaProg.start_anchored;
      if (state < 0) {
        return MatchOutcome<NumGroups>{};
      }
      std::size_t matchStart = 0;
      bool haveMatch = false;
      std::size_t bestStart = 0;

      // Check if start state itself is accepting (empty pattern)
      if (DfaProg.accepting.test(state)) {
        haveMatch = true;
        bestStart = 0;
      }

      for (std::size_t pos = 0; pos < input.size(); ++pos) {
        auto cls = classifyChar(static_cast<unsigned char>(input[pos]));
        auto trans = DfaProg.transitions[cls][state];

        if (trans < 0) {
          // Dead state — if we recorded a match, return it
          if (haveMatch) {
            if constexpr (TrackCaptures) {
              return tryMatchFrom(input, bestStart);
            } else {
              MatchOutcome<NumGroups> result;
              result.status = MatchStatus::Matched;
              return result;
            }
          }
          // Reset to unanchored start state for next position
          int uState = DfaProg.start_unanchored;
          if (uState < 0) {
            return MatchOutcome<NumGroups>{};
          }
          // Try the current character from the unanchored start state
          auto trans2 = DfaProg.transitions[cls][uState];
          if (trans2 < 0) {
            state = uState;
            matchStart = pos + 1;
          } else {
            state = trans2 & kStateMask;
            matchStart = pos;
            if (DfaProg.accepting.test(state)) {
              haveMatch = true;
              bestStart = matchStart;
            }
          }
          continue;
        }

        state = trans & kStateMask;

        if (DfaProg.accepting.test(state) ||
            (pos + 1 == input.size() && DfaProg.accepting_at_end.test(state))) {
          haveMatch = true;
          bestStart = matchStart;
        }
      }

      // Check end-of-input acceptance
      if (!haveMatch && DfaProg.accepting_at_end.test(state)) {
        haveMatch = true;
        bestStart = matchStart;
      }

      if (haveMatch) {
        if constexpr (TrackCaptures) {
          return tryMatchFrom(input, bestStart);
        } else {
          MatchOutcome<NumGroups> result;
          result.status = MatchStatus::Matched;
          return result;
        }
      }
      return MatchOutcome<NumGroups>{};
    } else {
      constexpr auto kSingleFirstChar = extractSingleFirstChar(kFilter);

      if constexpr (kSingleFirstChar.found) {
        const char* data = input.data();
        const char* end = data + input.size();
        const char* scan = data;

        while (scan < end) {
          const void* found =
              std::memchr(scan, kSingleFirstChar.ch, end - scan);
          if (!found) {
            break;
          }
          std::size_t start = static_cast<const char*>(found) - data;
          if (start > maxStart) {
            break;
          }

          auto result = tryMatchFrom(input, start);
          if (result.status == MatchStatus::Matched) {
            return result;
          }
          scan = static_cast<const char*>(found) + 1;
        }
        return MatchOutcome<NumGroups>{};
      } else {
        for (std::size_t start = 0; start <= maxStart; ++start) {
          if constexpr (!kFilter.accepts_all) {
            if (start < input.size() && !kFilter.test(input[start])) {
              continue;
            }
          }

          auto result = tryMatchFrom(input, start);
          if (result.status == MatchStatus::Matched) {
            return result;
          }
        }
        return MatchOutcome<NumGroups>{};
      }
    }
  }

  static bool tryTestFrom(std::string_view input, std::size_t startPos) {
    int state;
    if (startPos == 0) {
      state = DfaProg.start_anchored;
    } else if (
        startPos > 0 && DfaProg.start_after_newline >= 0 &&
        input[startPos - 1] == '\n') {
      state = DfaProg.start_after_newline;
    } else {
      state = DfaProg.start_unanchored;
    }
    if (state < 0) {
      return false;
    }

    if (DfaProg.accepting.test(state) ||
        (startPos == input.size() && DfaProg.accepting_at_end.test(state))) {
      return true;
    }

    for (std::size_t pos = startPos; pos < input.size(); ++pos) {
      auto cls = classifyChar(static_cast<unsigned char>(input[pos]));
      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        return false;
      }
      state = trans & kStateMask;
      if (DfaProg.accepting.test(state) ||
          (pos + 1 == input.size() && DfaProg.accepting_at_end.test(state))) {
        return true;
      }
    }
    return false;
  }

  static bool testMatch(std::string_view input) {
    std::size_t maxStart = kAnchored ? 0 : input.size();

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
          if (tryTestFrom(input, start)) {
            return true;
          }
        }
        scan = static_cast<const char*>(found) + 1;
      }
      return false;
    } else if constexpr (
        !kAnchored && DfaProgUnanchored.valid && !kHasLineOrEndAnchors) {
      int state = DfaProgUnanchored.start_unanchored;
      if (state < 0) {
        return false;
      }
      if (DfaProgUnanchored.accepting.test(state)) {
        return true;
      }
      for (std::size_t pos = 0; pos < input.size(); ++pos) {
        auto cls =
            classifyCharUnanchored(static_cast<unsigned char>(input[pos]));
        auto trans = DfaProgUnanchored.transitions[cls][state];
        if (trans < 0) {
          return false;
        }
        state = trans & kStateMask;
        if (DfaProgUnanchored.accepting.test(state) ||
            (pos + 1 == input.size() &&
             DfaProgUnanchored.accepting_at_end.test(state))) {
          return true;
        }
      }
      return DfaProgUnanchored.accepting_at_end.test(state);
    } else {
      for (std::size_t start = 0; start <= maxStart; ++start) {
        if constexpr (!kFilter.accepts_all) {
          if (start < input.size() && !kFilter.test(input[start])) {
            continue;
          }
        }
        if (tryTestFrom(input, start)) {
          return true;
        }
      }
      return false;
    }
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
