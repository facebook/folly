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

#include <folly/regex/Flags.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/Dfa.h>
#include <folly/regex/detail/Direction.h>
#include <folly/regex/detail/Executor.h>

namespace folly::regex::detail {

template <Direction Dir = Direction::Forward>
struct DfaCapture {
  std::size_t start = std::string_view::npos;
  std::size_t end = 0;

  constexpr GroupSpan toGroupSpan() const noexcept {
    if constexpr (Dir == Direction::Reverse) {
      return {end, start - end};
    } else {
      return {start, end - start};
    }
  }
};
template <
    const auto& DfaProg,
    const auto& DfaProgUnanchored,
    const auto& Ast,
    Flags F,
    bool TrackCaptures,
    int NumGroups,
    Direction Dir = Direction::Forward,
    const auto& ForwardAst = Ast,
    const auto& ProbeDfas = DfaProg>
struct DfaRunner {
  static_assert(ReadOnlyAst<std::remove_cvref_t<decltype(Ast)>>);
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
      std::array<DfaCapture<Dir>, NumGroups + 1>& captures,
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

  // Evaluate whether a probe-gated state should be accepted.
  // Returns true if the probe passes (or no probe exists for this state).
  // Uses pre-compiled probe DFAs for evaluation — no NFA dependency.
  static bool evaluateProbeForAcceptance(
      const DfaProbeStateInfo& probe,
      InputView<Dir> input,
      std::size_t pos) noexcept {
    bool matched = false;
    if (probe.lookbehind_width >= 0) {
      // Check for literal lookbehind fast path: single literal probe node.
      bool isLiteralProbe = false;
      if (probe.probe_id >= 0 && ForwardAst.hasProbe(probe.probe_id)) {
        int rootIdx = ForwardAst.probes[probe.probe_id].root;
        isLiteralProbe =
            rootIdx >= 0 && ForwardAst.nodes[rootIdx].kind == NodeKind::Literal;
      }
      if (isLiteralProbe) {
        std::size_t fullPos = input.posInFull(pos);
        if (fullPos >= static_cast<std::size_t>(probe.lookbehind_width)) {
          int rootIdx = ForwardAst.probes[probe.probe_id].root;
          auto lit = ForwardAst.nodes[rootIdx].literal;
          std::size_t start =
              fullPos - static_cast<std::size_t>(probe.lookbehind_width);
          matched =
              std::memcmp(
                  input.original_input.data() + start,
                  lit.data(),
                  lit.size()) == 0;
        }
      } else if (
          probe.probe_id >= 0 && probe.probe_id < ProbeDfas.count &&
          ProbeDfas.entries[probe.probe_id].valid) {
        // Variable-width lookbehind: run the probe sub-DFA in reverse.
        const auto& probeDfa = ProbeDfas.entries[probe.probe_id];
        int state = probeDfa.start_anchored;
        if (state >= 0) {
          auto fwdInput = input.template fullInput<Direction::Forward>();
          std::size_t absPos = input.posInFull(pos);
          if (probeDfa.accepting.test(state)) {
            matched = true;
          } else {
            for (std::size_t j = absPos; j > 0 && !matched; --j) {
              auto cls = probeDfa.char_to_class[static_cast<unsigned char>(
                  fwdInput[j - 1])];
              auto trans = probeDfa.transitions[cls][state];
              if (trans < 0) {
                break;
              }
              state = trans & 0x3FFF;
              if (probeDfa.accepting.test(state) ||
                  (j == 1 && probeDfa.accepting_at_end.test(state))) {
                matched = true;
              }
            }
            if (!matched && probeDfa.accepting_at_end.test(state)) {
              matched = true;
            }
          }
        }
      }
    } else if (
        probe.probe_id >= 0 && probe.probe_id < ProbeDfas.count &&
        ProbeDfas.entries[probe.probe_id].valid) {
      // Run the pre-compiled probe sub-DFA.
      const auto& probeDfa = ProbeDfas.entries[probe.probe_id];
      int state = probeDfa.start_anchored;
      if (state >= 0) {
        auto fwdInput = input.template fullInput<Direction::Forward>();
        std::size_t absPos = input.posInFull(pos);
        if (probeDfa.dir == Direction::Reverse) {
          // Lookbehind: scan leftward from absPos
          if (probeDfa.accepting.test(state)) {
            matched = true;
          } else {
            for (std::size_t j = absPos; j > 0 && !matched; --j) {
              auto cls = probeDfa.char_to_class[static_cast<unsigned char>(
                  fwdInput[j - 1])];
              auto trans = probeDfa.transitions[cls][state];
              if (trans < 0) {
                break;
              }
              state = trans & 0x3FFF;
              if (probeDfa.accepting.test(state) ||
                  (j == 1 && probeDfa.accepting_at_end.test(state))) {
                matched = true;
              }
            }
            if (!matched && probeDfa.accepting_at_end.test(state)) {
              matched = true;
            }
          }
        } else {
          // Lookahead: scan rightward from absPos
          if (probeDfa.accepting.test(state)) {
            matched = true;
          } else {
            for (std::size_t j = absPos; j < fwdInput.size() && !matched; ++j) {
              auto cls = probeDfa.char_to_class[static_cast<unsigned char>(
                  fwdInput[j])];
              auto trans = probeDfa.transitions[cls][state];
              if (trans < 0) {
                break;
              }
              state = trans & 0x3FFF;
              if (probeDfa.accepting.test(state) ||
                  (j + 1 == fwdInput.size() &&
                   probeDfa.accepting_at_end.test(state))) {
                matched = true;
              }
            }
            if (!matched && probeDfa.accepting_at_end.test(state)) {
              matched = true;
            }
          }
        }
      }
    }
    if (probe.negated) {
      matched = !matched;
    }
    return matched;
  }

  // Check if a state is accepting, considering probe gates.
  static bool isAccepting(
      int state, InputView<Dir> input, std::size_t pos) noexcept {
    if (!DfaProg.accepting.test(state)) {
      return false;
    }
    if constexpr (DfaProg.has_lookaround_probes) {
      auto probeIdx = DfaProg.probe_for_state[state];
      if (probeIdx > 0) {
        return evaluateProbeForAcceptance(
            DfaProg.probe_state_info[probeIdx - 1], input, pos);
      }
    }
    return true;
  }

  static bool isAcceptingAtEnd(
      int state, InputView<Dir> input, std::size_t pos) noexcept {
    if (!DfaProg.accepting_at_end.test(state)) {
      return false;
    }
    if constexpr (DfaProg.has_lookaround_probes) {
      auto probeIdx = DfaProg.probe_for_state[state];
      if (probeIdx > 0) {
        return evaluateProbeForAcceptance(
            DfaProg.probe_state_info[probeIdx - 1], input, pos);
      }
    }
    return true;
  }

  static MatchOutcome<NumGroups> matchAnchored(InputView<Dir> input) {
    if constexpr (Ast.leading_dot_star_min >= 0) {
      // Leading dot-star pruned — search for body starting at min position.
      std::size_t minSkip = static_cast<std::size_t>(Ast.leading_dot_star_min);
      for (std::size_t s = input.startPos(); !input.scanDone(s);
           s = (Dir == Direction::Forward ? s + 1 : s - 1)) {
        if constexpr (Dir == Direction::Forward) {
          if (s < minSkip) {
            continue;
          }
        } else {
          if (input.size() - s < minSkip) {
            continue;
          }
        }
        auto result = tryMatchFrom(input, s);
        if (result.status == MatchStatus::Matched) {
          if constexpr (TrackCaptures) {
            result.state.groups[0] = {0, input.size()};
          }
          return result;
        }
      }
      return MatchOutcome<NumGroups>{};
    }

    MatchOutcome<NumGroups> result;

    int state = DfaProg.start_anchored;
    if (state < 0) {
      return result;
    }

    std::array<DfaCapture<Dir>, NumGroups + 1> captures = {};

    if constexpr (TrackCaptures) {
      executeTagOps(
          DfaProg.tag_entries[DfaProg.start_anchored_tags],
          captures,
          input.startPos());
    }

    // Gate non-accepting probe states at the start position.
    if constexpr (DfaProg.has_lookaround_probes) {
      auto probeIdx = DfaProg.probe_for_state[state];
      if (probeIdx > 0 && !DfaProg.accepting.test(state)) {
        if (!evaluateProbeForAcceptance(
                DfaProg.probe_state_info[probeIdx - 1],
                input,
                input.startPos())) {
          return result;
        }
      }
    }

    for (std::size_t pos = input.startPos(); input.canConsume(pos);
         pos = InputView<Dir>::advance(pos)) {
      int prevState = state;
      auto cls = classifyChar(static_cast<unsigned char>(input.charAt(pos)));
      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        // Trailing dot-star: if the pattern body already accepted, the
        // remaining input is covered by the pruned dot-star.
        if constexpr (Ast.trailing_dot_star_min >= 0) {
          if (isAccepting(state, input, pos) ||
              isAcceptingAtEnd(state, input, pos)) {
            auto extended = computeDotStarExtension<Dir>(
                input,
                pos,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended != std::string_view::npos) {
              result.status = MatchStatus::Matched;
              if constexpr (TrackCaptures) {
                result.state.groups[0] = {0, input.size()};
              }
              return result;
            }
          }
        }
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

      // Gate non-accepting probe states at transition time: check
      // when LEAVING rather than when entering.  This defers the
      // evaluation until after any self-loop iterations (e.g. a++)
      // have completed, so the probe sees the correct position.
      if constexpr (DfaProg.has_lookaround_probes) {
        if (state != prevState) {
          auto probeIdx = DfaProg.probe_for_state[prevState];
          if (probeIdx > 0 && !DfaProg.accepting.test(prevState)) {
            if (!evaluateProbeForAcceptance(
                    DfaProg.probe_state_info[probeIdx - 1], input, pos)) {
              return result;
            }
          }
        }
      }
    }

    bool accepted = isAccepting(state, input, input.size()) ||
        isAcceptingAtEnd(state, input, input.size());
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
          result.state.groups[g] = captures[g].toGroupSpan();
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

    std::array<DfaCapture<Dir>, NumGroups + 1> captures = {};
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

    // Gate non-accepting probe states at the start position.
    if constexpr (DfaProg.has_lookaround_probes) {
      auto probeIdx = DfaProg.probe_for_state[state];
      if (probeIdx > 0 && !DfaProg.accepting.test(state)) {
        if (!evaluateProbeForAcceptance(
                DfaProg.probe_state_info[probeIdx - 1], input, startPos)) {
          return bestResult;
        }
      }
    }

    if (isAccepting(state, input, startPos) ||
        (input.atEnd(startPos) && isAcceptingAtEnd(state, input, startPos))) {
      bestResult.status = MatchStatus::Matched;
      if constexpr (TrackCaptures) {
        bestResult.state.groups[0] = {startPos, 0};
        for (int g = 1; g <= NumGroups; ++g) {
          if (captures[g].start != std::string_view::npos) {
            bestResult.state.groups[g] = captures[g].toGroupSpan();
          }
        }
      }
      if constexpr (Ast.trailing_dot_star_min < 0) {
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

    for (std::size_t pos = startPos; input.canConsume(pos);
         pos = InputView<Dir>::advance(pos)) {
      int prevState = state;
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
                bestResult.state.groups[g] = captures[g].toGroupSpan();
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
                bestResult.state.groups[g] = captures[g].toGroupSpan();
              }
            }
          }
        }
      }

      auto trans = DfaProg.transitions[cls][state];
      if (trans < 0) {
        // Trailing dot-star early exit in tryMatchFrom: if the body
        // already matched, extend analytically.
        if constexpr (Ast.trailing_dot_star_min >= 0) {
          if (bestResult.status == MatchStatus::Matched ||
              isAccepting(state, input, pos) ||
              isAcceptingAtEnd(state, input, pos)) {
            auto extended = computeDotStarExtension<Dir>(
                input,
                pos,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended != std::string_view::npos) {
              bestResult.status = MatchStatus::Matched;
              if constexpr (TrackCaptures) {
                if constexpr (Dir == Direction::Forward) {
                  bestResult.state.groups[0] = {startPos, extended - startPos};
                } else {
                  bestResult.state.groups[0] = {extended, startPos - extended};
                }
              }
            } else {
              bestResult.status = MatchStatus::NoMatch;
            }
          }
        }
        // \Z: accept before trailing newline (unconditional, not
        // gated on trailing dot-star).
        if constexpr (Dir == Direction::Forward) {
          if (pos + 1 == input.size() && ch == '\n' &&
              DfaProg.accepting_before_trailing_newline.test(state)) {
            bestResult.status = MatchStatus::Matched;
            if constexpr (TrackCaptures) {
              bestResult.state.groups[0] = {startPos, pos - startPos};
              for (int g = 1; g <= NumGroups; ++g) {
                if (captures[g].start != std::string_view::npos) {
                  bestResult.state.groups[g] = captures[g].toGroupSpan();
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
                  bestResult.state.groups[g] = captures[g].toGroupSpan();
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

      // Gate non-accepting probe states at transition time: check
      // when LEAVING rather than when entering.  This defers the
      // evaluation until after any self-loop iterations (e.g. a++)
      // have completed, so the probe sees the correct position.
      if constexpr (DfaProg.has_lookaround_probes) {
        if (state != prevState) {
          auto probeIdx = DfaProg.probe_for_state[prevState];
          if (probeIdx > 0 && !DfaProg.accepting.test(prevState)) {
            if (!evaluateProbeForAcceptance(
                    DfaProg.probe_state_info[probeIdx - 1], input, pos)) {
              break;
            }
          }
        }
      }

      std::size_t nextPos = InputView<Dir>::advance(pos);
      if (isAccepting(state, input, nextPos) ||
          (input.atEnd(nextPos) && isAcceptingAtEnd(state, input, nextPos))) {
        bestResult.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          if constexpr (Dir == Direction::Forward) {
            bestResult.state.groups[0] = {startPos, nextPos - startPos};
          } else {
            bestResult.state.groups[0] = {nextPos, startPos - nextPos};
          }
          for (int g = 1; g <= NumGroups; ++g) {
            if (captures[g].start != std::string_view::npos) {
              bestResult.state.groups[g] = captures[g].toGroupSpan();
            }
          }
        }
        if constexpr (Ast.trailing_dot_star_min < 0) {
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
    }

    // After scan loop: extend trailing dot-star if matched.
    if constexpr (Ast.trailing_dot_star_min >= 0) {
      if (bestResult.status == MatchStatus::Matched) {
        std::size_t extendPos;
        if constexpr (Dir == Direction::Forward) {
          extendPos = bestResult.state.groups[0].offset +
              bestResult.state.groups[0].length;
        } else {
          extendPos = bestResult.state.groups[0].offset;
        }
        auto extended = computeDotStarExtension<Dir>(
            input,
            extendPos,
            Ast.trailing_dot_star_dot_all,
            Ast.trailing_dot_star_anchor);
        if (extended != std::string_view::npos) {
          if constexpr (TrackCaptures) {
            if constexpr (Dir == Direction::Forward) {
              bestResult.state.groups[0].length = extended - startPos;
            } else {
              bestResult.state.groups[0] = {extended, startPos - extended};
            }
          }
        } else {
          bestResult.status = MatchStatus::NoMatch;
        }
      }
    }

    // After scan loop: extend leading dot-star if matched.
    if constexpr (Ast.leading_dot_star_min >= 0) {
      if (bestResult.status == MatchStatus::Matched) {
        std::size_t extendPos;
        if constexpr (Dir == Direction::Forward) {
          extendPos = bestResult.state.groups[0].offset;
        } else {
          extendPos = bestResult.state.groups[0].offset +
              bestResult.state.groups[0].length;
        }
        auto extended = computeLeadingDotStarExtension<Dir>(
            input,
            extendPos,
            Ast.leading_dot_star_dot_all,
            Ast.leading_dot_star_anchor);
        if (extended != std::string_view::npos) {
          if constexpr (TrackCaptures) {
            if constexpr (Dir == Direction::Forward) {
              std::size_t currentEnd = bestResult.state.groups[0].offset +
                  bestResult.state.groups[0].length;
              bestResult.state.groups[0] = {extended, currentEnd - extended};
            } else {
              bestResult.state.groups[0].length =
                  extended - bestResult.state.groups[0].offset;
            }
          }
        } else {
          bestResult.status = MatchStatus::NoMatch;
        }
      }
    }

    return bestResult;
  }

  // Single-pass unanchored DFA test:
  // Uses self-loops to determine if a match exists anywhere in the input
  // without per-position retries. Falls back to position-scan search when
  // the unanchored DFA is unavailable or trailing dot-star anchors are
  // present. Always returns a definitive result.
  static bool testUnanchored(InputView<Dir> input) noexcept {
    if constexpr (!DfaProgUnanchored.valid || hasTrailingDotStarAnchors(Ast)) {
      // Position-scan fallback for unsupported constructs.
      for (std::size_t start = input.scanStart(); !input.scanDone(start);
           start = InputView<Dir>::advance(start)) {
        auto outcome = tryMatchFrom(input, start);
        if (outcome.status == MatchStatus::Matched) {
          return true;
        }
      }
      return false;
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

      for (std::size_t pos = input.startPos(); input.canConsume(pos);
           pos = InputView<Dir>::advance(pos)) {
        auto cls =
            classifyUnanchored(static_cast<unsigned char>(input.charAt(pos)));
        auto trans = DfaProgUnanchored.transitions[cls][state];
        if (trans < 0) {
          return false;
        }
        state = trans & kStateMask;
        if (DfaProgUnanchored.accepting.test(state) ||
            (input.atEnd(InputView<Dir>::advance(pos)) &&
             DfaProgUnanchored.accepting_at_end.test(state))) {
          return true;
        }
      }
      return DfaProgUnanchored.accepting_at_end.test(state);
    }
  }
};

} // namespace folly::regex::detail
