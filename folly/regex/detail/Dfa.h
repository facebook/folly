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

#include <cstddef>
#include <cstdint>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Nfa.h>

namespace folly {
namespace regex {
namespace detail {

// NFA state set for DFA subset construction — alias for FixedBitset.
template <int MaxBits>
using NfaStateSet = FixedBitset<MaxBits>;

struct DfaTagOp {
  int8_t group_id = -1;
  bool is_end = false;

  constexpr bool operator==(const DfaTagOp& o) const noexcept {
    return group_id == o.group_id && is_end == o.is_end;
  }
};

struct DfaTagEntry {
  static constexpr int kMaxOps = 8;
  int8_t count = 0;
  DfaTagOp ops[kMaxOps] = {};

  constexpr void add(DfaTagOp op) noexcept {
    for (int i = 0; i < count; ++i) {
      if (ops[i] == op) {
        return;
      }
    }
    if (count < kMaxOps) {
      ops[count++] = op;
    }
  }

  constexpr bool operator==(const DfaTagEntry& o) const noexcept {
    if (count != o.count) {
      return false;
    }
    for (int i = 0; i < count; ++i) {
      if (!(ops[i] == o.ops[i])) {
        return false;
      }
    }
    return true;
  }
};

template <int MaxDfaStates>
struct DfaProgram {
  static constexpr int kMaxTagEntries =
      MaxDfaStates * 4 > 512 ? 512 : MaxDfaStates * 4;
  static constexpr int kMaxClasses = 128;

  // Bit 14 of a transition value signals that tag ops exist for this
  // transition.  The executor checks this bit and only loads from
  // tag_actions[] when set, eliminating a memory load on the common
  // (no-tags) fast path.  Dead state remains -1 (bit 15 set → negative).
  static constexpr int16_t kHasTagsBit = 0x4000;
  static constexpr int16_t kStateMask = 0x3FFF;

  // Range classifier: when equivalence classes decompose into at most 2
  // contiguous byte ranges (e.g. \d → [48,58) vs rest, or [a-z] → [97,123)
  // vs rest), the executor replaces the char_to_class[] table lookup with
  // 1-2 immediate comparisons (pure register ops, no memory load).
  // Each boundary says "chars in [prev_threshold, threshold) → class_id",
  // with last_class covering [last_threshold, 256).
  struct ClassBoundary {
    uint8_t threshold = 0;
    uint8_t class_id = 0;
  };
  static constexpr int kMaxRangeBoundaries = 2;
  ClassBoundary boundaries[kMaxRangeBoundaries] = {};
  int boundary_count = 0;
  uint8_t last_class = 0;
  bool use_range_classifier = false;

  // Character equivalence class mapping: char_to_class[byte] → class ID
  uint8_t char_to_class[256] = {};
  int num_classes = 0;

  // Column-major transition tables indexed by [class][state].
  // All states for a given class are contiguous, concentrating the working
  // set into num_classes * MaxDfaStates * 2 bytes — typically a few KB that
  // fits entirely in L1 cache.
  // Transition values encode: bits 0-13 = next state, bit 14 = has tag ops.
  int16_t transitions[kMaxClasses][MaxDfaStates] = {};
  uint16_t tag_actions[kMaxClasses][MaxDfaStates] = {};

  bool accepting[MaxDfaStates] = {};
  bool accepting_at_end[MaxDfaStates] = {};

  DfaTagEntry tag_entries[kMaxTagEntries] = {};
  int tag_entry_count = 1; // entry 0 reserved for "no ops"

  int start_anchored = -1;
  int start_unanchored = -1;
  uint16_t start_anchored_tags = 0;
  uint16_t start_unanchored_tags = 0;
  int state_count = 0;
  bool valid = false;

  constexpr uint16_t internTagEntry(const DfaTagEntry& e) noexcept {
    if (e.count == 0) {
      return 0;
    }
    for (int i = 1; i < tag_entry_count; ++i) {
      if (tag_entries[i] == e) {
        return static_cast<uint16_t>(i);
      }
    }
    if (tag_entry_count >= kMaxTagEntries) {
      return 0;
    }
    tag_entries[tag_entry_count] = e;
    return static_cast<uint16_t>(tag_entry_count++);
  }
};

template <
    int MaxDfaStates,
    int MaxNfaStates,
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr DfaProgram<MaxDfaStates> buildDfa(
    const NfaProgram<MaxNfaStates>& nfa,
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>&
        ast) noexcept {
  DfaProgram<MaxDfaStates> prog;

  if (nfa.start_state < 0) {
    return prog;
  }

  using StateSet = NfaStateSet<MaxNfaStates>;

  StateSet state_sets[MaxDfaStates] = {};

  // Use interval-indexed tables when the partition is valid, otherwise
  // fall back to the full 256-wide tables.
  constexpr int kMaxIv = AlphabetPartition::kMaxBoundaries + 1;
  int16_t iv_trans[MaxDfaStates][kMaxIv] = {};
  uint16_t iv_tags[MaxDfaStates][kMaxIv] = {};

  // Initialize interval tables to -1 (dead state)
  for (int s = 0; s < MaxDfaStates; ++s) {
    for (int iv = 0; iv < kMaxIv; ++iv) {
      iv_trans[s][iv] = -1;
    }
  }

  struct ClosureResult {
    StateSet states;
    DfaTagEntry tags;
    bool has_match = false;
    bool has_match_at_end = false;
  };

  struct Builder {
    DfaProgram<MaxDfaStates>& prog;
    const NfaProgram<MaxNfaStates>& nfa;
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast;
    StateSet (&state_sets)[MaxDfaStates];
    int16_t (&iv_trans)[MaxDfaStates][kMaxIv];
    uint16_t (&iv_tags)[MaxDfaStates][kMaxIv];

    constexpr bool canReachMatch(
        int start, const StateSet& alreadyVisited) noexcept {
      if (start < 0) {
        return false;
      }

      StateSet visited = alreadyVisited;
      int worklist[MaxNfaStates > 0 ? MaxNfaStates : 1] = {};
      int wlCount = 0;
      worklist[wlCount++] = start;

      while (wlCount > 0) {
        int s = worklist[--wlCount];
        if (s < 0 || s >= nfa.state_count) {
          continue;
        }
        if (visited.test(s)) {
          continue;
        }
        visited.set(s);

        const auto& state = nfa.states[s];
        switch (state.kind) {
          case NfaStateKind::Match:
            return true;
          case NfaStateKind::Split:
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            if (state.alt >= 0) {
              worklist[wlCount++] = state.alt;
            }
            break;
          case NfaStateKind::CountedRepeat:
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            if (state.alt >= 0) {
              worklist[wlCount++] = state.alt;
            }
            break;
          case NfaStateKind::GroupStart:
          case NfaStateKind::GroupEnd:
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::Literal:
          case NfaStateKind::AnyChar:
          case NfaStateKind::AnyByte:
          case NfaStateKind::CharClass:
          case NfaStateKind::AnchorBegin:
          case NfaStateKind::AnchorEnd:
          case NfaStateKind::AnchorStartOfString:
          case NfaStateKind::AnchorEndOfString:
          case NfaStateKind::AnchorEndOfStringOrNewline:
          case NfaStateKind::AnchorBeginLine:
          case NfaStateKind::AnchorEndLine:
            break;
        }
      }
      return false;
    }

    constexpr ClosureResult epsilonClosure(
        const StateSet& startStates, bool atBegin) noexcept {
      ClosureResult result;

      StateSet visited;
      int worklist[MaxNfaStates > 0 ? MaxNfaStates * 2 : 1] = {};
      int wlCount = 0;

      for (int i = 0; i < nfa.state_count; ++i) {
        if (startStates.test(i)) {
          worklist[wlCount++] = i;
        }
      }

      while (wlCount > 0) {
        int s = worklist[--wlCount];
        if (s < 0 || s >= nfa.state_count) {
          continue;
        }
        if (visited.test(s)) {
          continue;
        }
        visited.set(s);

        const auto& state = nfa.states[s];

        switch (state.kind) {
          case NfaStateKind::Split:
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            if (state.alt >= 0) {
              worklist[wlCount++] = state.alt;
            }
            break;
          case NfaStateKind::CountedRepeat:
            // DFA virtual expansion: follow both loop and exit paths
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            if (state.alt >= 0) {
              worklist[wlCount++] = state.alt;
            }
            break;
          case NfaStateKind::GroupStart:
            result.tags.add(
                {static_cast<int8_t>(state.group_id), /*is_end=*/false});
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::GroupEnd:
            result.tags.add(
                {static_cast<int8_t>(state.group_id), /*is_end=*/true});
            if (state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::AnchorBegin:
            if (atBegin && state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::AnchorEnd:
            result.states.set(s);
            if (canReachMatch(state.next, visited)) {
              result.has_match_at_end = true;
            }
            break;
          case NfaStateKind::AnchorStartOfString:
            if (atBegin && state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::AnchorEndOfString:
            result.states.set(s);
            if (canReachMatch(state.next, visited)) {
              result.has_match_at_end = true;
            }
            break;
          case NfaStateKind::AnchorEndOfStringOrNewline:
            result.states.set(s);
            if (canReachMatch(state.next, visited)) {
              result.has_match_at_end = true;
            }
            break;
          case NfaStateKind::AnchorBeginLine:
            if (atBegin && state.next >= 0) {
              worklist[wlCount++] = state.next;
            }
            break;
          case NfaStateKind::AnchorEndLine:
            result.states.set(s);
            if (canReachMatch(state.next, visited)) {
              result.has_match_at_end = true;
            }
            break;
          case NfaStateKind::Match:
            result.states.set(s);
            result.has_match = true;
            break;
          case NfaStateKind::Literal:
          case NfaStateKind::AnyChar:
          case NfaStateKind::AnyByte:
          case NfaStateKind::CharClass:
            result.states.set(s);
            break;
        }
      }

      return result;
    }

    constexpr int findOrCreateState(const StateSet& nfaSet) noexcept {
      for (int i = 0; i < prog.state_count; ++i) {
        if (state_sets[i] == nfaSet) {
          return i;
        }
      }
      if (prog.state_count >= MaxDfaStates) {
        return -2; // overflow
      }
      int idx = prog.state_count++;
      state_sets[idx] = nfaSet;
      return idx;
    }

    constexpr bool stateMatchesChar(int s, unsigned char c) noexcept {
      const auto& state = nfa.states[s];
      switch (state.kind) {
        case NfaStateKind::Literal:
          return static_cast<unsigned char>(state.ch) == c;
        case NfaStateKind::AnyChar:
          return true;
        case NfaStateKind::AnyByte:
          return true;
        case NfaStateKind::CharClass:
          return ast.charClassTestAt(
              state.char_class_index, static_cast<char>(c));
        case NfaStateKind::Match:
        case NfaStateKind::Split:
        case NfaStateKind::CountedRepeat:
        case NfaStateKind::AnchorBegin:
        case NfaStateKind::AnchorEnd:
        case NfaStateKind::AnchorStartOfString:
        case NfaStateKind::AnchorEndOfString:
        case NfaStateKind::AnchorEndOfStringOrNewline:
        case NfaStateKind::AnchorBeginLine:
        case NfaStateKind::AnchorEndLine:
        case NfaStateKind::GroupStart:
        case NfaStateKind::GroupEnd:
          return false;
      }
      return false;
    }

    constexpr bool build() noexcept {
      StateSet startSet;
      startSet.set(nfa.start_state);

      auto closureAnchored = epsilonClosure(startSet, /*atBegin=*/true);
      auto closureUnanchored = epsilonClosure(startSet, /*atBegin=*/false);

      int anchoredState = findOrCreateState(closureAnchored.states);
      if (anchoredState == -2) {
        return false;
      }
      prog.start_anchored = anchoredState;
      prog.accepting[anchoredState] = closureAnchored.has_match;
      prog.accepting_at_end[anchoredState] = closureAnchored.has_match_at_end;
      prog.start_anchored_tags = prog.internTagEntry(closureAnchored.tags);

      int unanchoredState;
      if (closureAnchored.states == closureUnanchored.states) {
        unanchoredState = anchoredState;
      } else {
        unanchoredState = findOrCreateState(closureUnanchored.states);
        if (unanchoredState == -2) {
          return false;
        }
        prog.accepting[unanchoredState] = closureUnanchored.has_match;
        prog.accepting_at_end[unanchoredState] =
            closureUnanchored.has_match_at_end;
      }
      prog.start_unanchored = unanchoredState;
      prog.start_unanchored_tags = prog.internTagEntry(closureUnanchored.tags);

      // Worklist-based subset construction — writes to full_trans/full_tags
      bool explored[MaxDfaStates] = {};
      bool queued[MaxDfaStates] = {};
      int worklist[MaxDfaStates] = {};
      int wlCount = 0;

      queued[anchoredState] = true;
      worklist[wlCount++] = anchoredState;
      if (unanchoredState != anchoredState) {
        queued[unanchoredState] = true;
        worklist[wlCount++] = unanchoredState;
      }

      while (wlCount > 0) {
        int dfaState = worklist[--wlCount];
        if (explored[dfaState]) {
          continue;
        }
        explored[dfaState] = true;

        const StateSet& curSet = state_sets[dfaState];

        // Interval-based subset construction: iterate over partition
        // intervals instead of all 256 characters. Each interval contains
        // characters that behave identically w.r.t. every NFA state.
        const auto& part = nfa.partition;
        int numIntervals = part.valid ? part.intervalCount() : 256;

        for (int iv = 0; iv < numIntervals; ++iv) {
          StateSet moveSet;
          if (part.valid) {
            // Use interval masks for O(1) per-state membership test
            for (int s = 0; s < nfa.state_count; ++s) {
              if (curSet.test(s) && ((nfa.states[s].interval_mask >> iv) & 1)) {
                if (nfa.states[s].next >= 0) {
                  moveSet.set(nfa.states[s].next);
                }
              }
            }
          } else {
            // Fallback: treat each char as its own interval
            for (int s = 0; s < nfa.state_count; ++s) {
              if (curSet.test(s) &&
                  stateMatchesChar(s, static_cast<unsigned char>(iv))) {
                if (nfa.states[s].next >= 0) {
                  moveSet.set(nfa.states[s].next);
                }
              }
            }
          }

          if (moveSet.empty()) {
            continue;
          }

          auto closure = epsilonClosure(moveSet, /*atBegin=*/false);

          if (closure.states.empty()) {
            continue;
          }

          int targetState = findOrCreateState(closure.states);
          if (targetState == -2) {
            return false;
          }

          prog.accepting[targetState] =
              prog.accepting[targetState] || closure.has_match;
          prog.accepting_at_end[targetState] =
              prog.accepting_at_end[targetState] || closure.has_match_at_end;

          iv_trans[dfaState][iv] = static_cast<int16_t>(targetState);
          iv_tags[dfaState][iv] = prog.internTagEntry(closure.tags);

          if (!queued[targetState]) {
            queued[targetState] = true;
            worklist[wlCount++] = targetState;
          }
        }
      }

      // Build char_to_class directly from the alphabet partition.
      // When the partition is valid, each interval maps to a class ID.
      // This eliminates the O(256² × DFA_states) post-hoc comparison.
      const auto& part2 = nfa.partition;
      if (part2.valid) {
        int numIv = part2.intervalCount();
        if (numIv > DfaProgram<MaxDfaStates>::kMaxClasses) {
          return false;
        }
        prog.num_classes = numIv;

        // Map each byte to its interval/class
        for (int c = 0; c < 256; ++c) {
          prog.char_to_class[c] = static_cast<uint8_t>(
              part2.charToInterval(static_cast<unsigned char>(c)));
        }
      } else {
        // Fallback: intervals = individual chars, compute equiv classes
        // from iv_trans (which is indexed 0-255 in fallback mode)
        int classId = 0;
        for (int c = 0; c < 256; ++c) {
          bool matched = false;
          for (int prev = 0; prev < c; ++prev) {
            bool same = true;
            for (int s = 0; s < prog.state_count && same; ++s) {
              if (iv_trans[s][prev] != iv_trans[s][c] ||
                  iv_tags[s][prev] != iv_tags[s][c]) {
                same = false;
              }
            }
            if (same) {
              prog.char_to_class[c] = prog.char_to_class[prev];
              matched = true;
              break;
            }
          }
          if (!matched) {
            if (classId >= DfaProgram<MaxDfaStates>::kMaxClasses) {
              return false;
            }
            prog.char_to_class[c] = static_cast<uint8_t>(classId++);
          }
        }
        prog.num_classes = classId;
      }

      // Initialize compact tables to -1 (dead state)
      for (int cls = 0; cls < prog.num_classes; ++cls) {
        for (int s = 0; s < MaxDfaStates; ++s) {
          prog.transitions[cls][s] = -1;
        }
      }

      // Populate column-major compact tables from the interval tables.
      // Set kHasTagsBit in transitions where tag_actions is non-zero so the
      // executor can skip the tag_actions load on the common (no-tags) path.
      for (int s = 0; s < prog.state_count; ++s) {
        int numIv = part2.valid ? part2.intervalCount() : 256;
        for (int iv = 0; iv < numIv; ++iv) {
          int cls = part2.valid ? iv : prog.char_to_class[iv];
          int16_t trans = iv_trans[s][iv];
          uint16_t tags = iv_tags[s][iv];
          if (trans >= 0 && tags > 0) {
            trans |= DfaProgram<MaxDfaStates>::kHasTagsBit;
          }
          prog.transitions[cls][s] = trans;
          prog.tag_actions[cls][s] = tags;
        }
      }

      // Build boundary list from char_to_class for the range classifier.
      // If the classes decompose into ≤2 boundaries (e.g. \d, [a-z]),
      // the executor uses 1-2 immediate comparisons instead of a table load.
      {
        int bcount = 0;
        uint8_t cur = prog.char_to_class[0];
        bool fits = true;
        for (int c = 1; c < 256; ++c) {
          if (prog.char_to_class[c] != cur) {
            if (bcount >= DfaProgram<MaxDfaStates>::kMaxRangeBoundaries) {
              fits = false;
              break;
            }
            prog.boundaries[bcount++] = {static_cast<uint8_t>(c), cur};
            cur = prog.char_to_class[c];
          }
        }
        if (fits) {
          prog.boundary_count = bcount;
          prog.last_class = cur;
          prog.use_range_classifier = true;
        }
      }

      return true;
    }
  };

  Builder builder{prog, nfa, ast, state_sets, iv_trans, iv_tags};
  if (builder.build()) {
    prog.valid = true;
  }

  return prog;
}

} // namespace detail
} // namespace regex
} // namespace folly
