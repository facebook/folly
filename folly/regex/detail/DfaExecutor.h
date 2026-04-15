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
#include <folly/regex/detail/Direction.h>
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
    int NumGroups,
    Direction Dir = Direction::Forward,
    const auto& NfaProg = DfaProg,
    const auto& ForwardAst = Ast,
    const auto& ProbeDfas = DfaProg>
struct DfaRunner {
  static constexpr int16_t kHasTagsBit = 0x4000;
  static constexpr int16_t kStateMask = 0x3FFF;
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

  // Evaluate lookaround probes attached to a DFA state.
  // Returns true if ALL probe conditions are satisfied.
  // Implementation is in Regex.h where NfaExecutor is available.
  static bool evaluateLookaroundProbes(
      int dfaState,
      InputView<Dir> input,
      std::size_t pos) noexcept;

  static MatchOutcome<NumGroups> matchAnchored(InputView<Dir> input) {
    MatchOutcome<NumGroups> result;

    int state = DfaProg.start_anchored;
    if (state < 0) {
      return result;
    }

    std::array<DfaCapture, NumGroups + 1> captures = {};

    if constexpr (TrackCaptures) {
      executeTagOps(
          DfaProg.tag_entries[DfaProg.start_anchored_tags],
          captures,
          input.startPos());
    }

    for (std::size_t pos = input.startPos(); input.canConsume(pos);
         pos = InputView<Dir>::advance(pos)) {
      auto cls = classifyChar(static_cast<unsigned char>(input.charAt(pos)));
      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        return result;
      }
      if constexpr (TrackCaptures) {
        if (trans & kHasTagsBit) {
          auto tagIdx = DfaProg.tag_actions[cls][state];
          executeTagOps(
              DfaProg.tag_entries[tagIdx],
              captures,
              InputView<Dir>::advance(pos));
        }
      }
      state = trans & kStateMask;
    }

    bool accepted =
        (DfaProg.accepting.test(state) || DfaProg.accepting_at_end.test(state))
        && evaluateLookaroundProbes(state, input, input.size());
    if (!accepted) {
      return result;
    }

    // In reverse: the DFA's subset construction doesn't preserve possessive
    // constraints. Verify using pre-compiled probe DFAs: each probe is a
    // linear chain of interval masks. Walk backward through the input
    // checking each character — pure register ops, no NFA simulation.
    //
    // When a prefix has been stripped, the probe must see the stripped
    // prefix chars extending beyond the trimmed input.
    if constexpr (Dir == Direction::Reverse && DfaProg.has_possessive_probes) {
      constexpr auto kStrippedPrefix =
          Ast.literal_prefix().substr(Ast.prefix_len - Ast.prefix_strip_len);
      constexpr auto kSPLen = static_cast<std::size_t>(kStrippedPrefix.size());

      for (int pi = 0; pi < DfaProg.probe_count; ++pi) {
        const auto& probe = DfaProg.probes[pi];
        if (!probe.valid || probe.step_count == 0) {
          continue;
        }
        if (input.size() + kSPLen <
            static_cast<std::size_t>(probe.step_count)) {
          continue;
        }
        // Check if the probe pattern matches at the end of the
        // (conceptually extended) input.
        bool probeMatched = true;
        for (int s = 0; s < probe.step_count; ++s) {
          char ch;
          if (static_cast<std::size_t>(s) < kSPLen) {
            // Reading from the stripped prefix (rightmost chars first).
            ch = kStrippedPrefix[kSPLen - 1 - s];
          } else {
            ch = input[input.size() - 1 - (s - kSPLen)];
          }
          int iv = DfaProg.probe_partition.charToInterval(
              static_cast<unsigned char>(ch));
          if (!((probe.masks[probe.step_count - 1 - s] >> iv) & 1)) {
            probeMatched = false;
            break;
          }
        }
        if (!probeMatched) {
          continue;
        }
        if (probe.max_repeat < 0) {
          // Unbounded possessive — should consume everything, reject
          return result;
        }
        // Bounded: reject only if possessive consumed fewer than max
        std::size_t probeWidth = static_cast<std::size_t>(probe.step_count);
        std::size_t consumed = input.size() + kSPLen - probeWidth;
        std::size_t iters = consumed / probeWidth;
        if (iters < static_cast<std::size_t>(probe.max_repeat)) {
          return result;
        }
      }
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
      InputView<Dir> input, std::size_t startPos) {
    MatchOutcome<NumGroups> bestResult;

    int state;
    if constexpr (Dir == Direction::Forward) {
      if (startPos == 0) {
        state = DfaProg.start_anchored;
      } else if (
          DfaProg.start_after_newline >= 0 && input[startPos - 1] == '\n') {
        state = DfaProg.start_after_newline;
      } else {
        state = DfaProg.start_unanchored;
      }
    } else {
      if (startPos == input.size()) {
        state = DfaProg.start_anchored;
      } else if (
          DfaProg.start_after_newline >= 0 && startPos < input.size() &&
          input[startPos] == '\n') {
        state = DfaProg.start_after_newline;
      } else {
        state = DfaProg.start_unanchored;
      }
    }
    if (state < 0) {
      return bestResult;
    }

    std::array<DfaCapture, NumGroups + 1> captures = {};
    if constexpr (TrackCaptures) {
      uint16_t startTags;
      if constexpr (Dir == Direction::Forward) {
        if (startPos == 0) {
          startTags = DfaProg.start_anchored_tags;
        } else if (
            DfaProg.start_after_newline >= 0 && input[startPos - 1] == '\n') {
          startTags = DfaProg.start_after_newline_tags;
        } else {
          startTags = DfaProg.start_unanchored_tags;
        }
      } else {
        if (startPos == input.size()) {
          startTags = DfaProg.start_anchored_tags;
        } else if (
            DfaProg.start_after_newline >= 0 && startPos < input.size() &&
            input[startPos] == '\n') {
          startTags = DfaProg.start_after_newline_tags;
        } else {
          startTags = DfaProg.start_unanchored_tags;
        }
      }
      executeTagOps(DfaProg.tag_entries[startTags], captures, startPos);
    }

    if ((DfaProg.accepting.test(state) ||
         (input.atEnd(startPos) && DfaProg.accepting_at_end.test(state))) &&
        evaluateLookaroundProbes(state, input, startPos)) {
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

    for (std::size_t pos = startPos; input.canConsume(pos);
         pos = InputView<Dir>::advance(pos)) {
      char ch = input.charAt(pos);
      auto cls = classifyChar(static_cast<unsigned char>(ch));

      // Multiline anchor: accept at newline boundary without consuming
      if constexpr (Dir == Direction::Forward) {
        // Multiline $: accept before '\n'
        if (ch == '\n' && DfaProg.accepting_before_newline.test(state)) {
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
      } else {
        // Reverse multiline ^: accept after '\n' (going left past newline)
        if (ch == '\n' && DfaProg.accepting_before_newline.test(state)) {
          bestResult.status = MatchStatus::Matched;
          if constexpr (TrackCaptures) {
            bestResult.state.groups[0] = {pos, startPos - pos};
            for (int g = 1; g <= NumGroups; ++g) {
              if (captures[g].start != std::string_view::npos) {
                bestResult.state.groups[g] = {
                    captures[g].start, captures[g].end - captures[g].start};
              }
            }
          }
        }
      }

      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        if constexpr (Dir == Direction::Forward) {
          // \Z: accept before trailing newline
          if (pos + 1 == input.size() && ch == '\n' &&
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
        } else {
          // Reverse \Z equivalent: accept after leading newline
          if (pos == 1 && ch == '\n' &&
              DfaProg.accepting_before_trailing_newline.test(state)) {
            bestResult.status = MatchStatus::Matched;
            if constexpr (TrackCaptures) {
              bestResult.state.groups[0] = {pos - 1, startPos - (pos - 1)};
              for (int g = 1; g <= NumGroups; ++g) {
                if (captures[g].start != std::string_view::npos) {
                  bestResult.state.groups[g] = {
                      captures[g].start, captures[g].end - captures[g].start};
                }
              }
            }
          }
        }
        break;
      }
      if constexpr (TrackCaptures) {
        if (trans & kHasTagsBit) {
          auto tagIdx = DfaProg.tag_actions[cls][state];
          executeTagOps(
              DfaProg.tag_entries[tagIdx],
              captures,
              InputView<Dir>::advance(pos));
        }
      }
      state = trans & kStateMask;

      std::size_t nextPos = InputView<Dir>::advance(pos);
      if ((DfaProg.accepting.test(state) ||
           (input.atEnd(nextPos) && DfaProg.accepting_at_end.test(state))) &&
          evaluateLookaroundProbes(state, input, nextPos)) {
        bestResult.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          if constexpr (Dir == Direction::Forward) {
            bestResult.state.groups[0] = {startPos, nextPos - startPos};
          } else {
            bestResult.state.groups[0] = {nextPos, startPos - nextPos};
          }
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

  // Single-pass unanchored DFA test:
  // self-loops to determine if a match exists anywhere in the input without
  // per-position retries. Returns true if any match is found.
  static bool testUnanchored(InputView<Dir> input) noexcept {
    if constexpr (!DfaProgUnanchored.valid || Dir != Direction::Forward) {
      return false; // Caller should use positionScanSearch fallback
    } else {
      int state = DfaProgUnanchored.start_unanchored;
      if (state < 0) {
        return false;
      }
      if (DfaProgUnanchored.accepting.test(state)) {
        return true;
      }

      auto classifyUnanchored = [](unsigned char ch) {
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
      };

      for (std::size_t pos = 0; pos < input.size(); ++pos) {
        auto cls = classifyUnanchored(static_cast<unsigned char>(input[pos]));
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
    }
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
