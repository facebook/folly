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
#include <type_traits>

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

// Classifies a pattern's overall greediness to enable zero-overhead
// compile-time dispatch in the DFA executor.
enum class MatchPreference : uint8_t {
  AllGreedy, // no accept_early bits set — always extend (common case)
  AllLazy, // all accepting states have accept_early — always stop early
  Mixed, // some accepting states are early, some aren't — check bitset
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

  FixedBitset<MaxDfaStates> accepting;
  FixedBitset<MaxDfaStates> accepting_at_end;
  FixedBitset<MaxDfaStates> accepting_before_trailing_newline;
  FixedBitset<MaxDfaStates> accepting_before_newline;

  // Per-accepting-state flag: "should the executor stop immediately upon
  // reaching this accepting state?" Set for states where the NFA's Match
  // was reached via the priority (next) path through all Split decisions.
  FixedBitset<MaxDfaStates> accept_early;
  MatchPreference match_preference = MatchPreference::AllGreedy;

  DfaTagEntry tag_entries[kMaxTagEntries] = {};
  int tag_entry_count = 1; // entry 0 reserved for "no ops"

  int start_anchored = -1;
  int start_unanchored = -1;
  int start_after_newline = -1;
  uint16_t start_anchored_tags = 0;
  uint16_t start_unanchored_tags = 0;
  uint16_t start_after_newline_tags = 0;
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

// Intermediate result from subset construction. Captures interval-indexed
// transitions before compression into equivalence-class-indexed compact
// tables. Splitting DFA construction across two holder structs gives the
// core (subset construction) and finalize (table compression) phases
// independent constexpr step budgets.
template <int MaxDfaStates>
struct DfaCoreResult {
  static constexpr int kMaxIv = AlphabetPartition::kMaxBoundaries + 1;
  static constexpr int kMaxTagEntries =
      MaxDfaStates * 4 > 512 ? 512 : MaxDfaStates * 4;

  int16_t iv_trans[MaxDfaStates][kMaxIv] = {};
  uint16_t iv_tags[MaxDfaStates][kMaxIv] = {};
  FixedBitset<MaxDfaStates> accepting;
  FixedBitset<MaxDfaStates> accepting_at_end;
  FixedBitset<MaxDfaStates> accepting_before_trailing_newline;
  FixedBitset<MaxDfaStates> accepting_before_newline;
  FixedBitset<MaxDfaStates> accept_early;

  DfaTagEntry tag_entries[kMaxTagEntries] = {};
  int tag_entry_count = 1; // entry 0 reserved for "no ops"

  int start_anchored = -1;
  int start_unanchored = -1;
  int start_after_newline = -1;
  uint16_t start_anchored_tags = 0;
  uint16_t start_unanchored_tags = 0;
  uint16_t start_after_newline_tags = 0;
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

// Phase 1 of DFA construction: subset construction.
// Builds interval-indexed transition tables from the NFA.
template <int MaxDfaStates, bool AddRestart = false, int MaxNfaStates>
constexpr DfaCoreResult<MaxDfaStates> buildDfaCore(
    const NfaProgram<MaxNfaStates>& nfa, const auto& ast) noexcept {
  DfaCoreResult<MaxDfaStates> core;

  if (nfa.start_state < 0) {
    return core;
  }

  using StateSet = NfaStateSet<MaxNfaStates>;

  StateSet state_sets[MaxDfaStates] = {};
  StateSet priority_sets[MaxDfaStates] = {};

  // Hash table for O(1) amortized state lookup during subset construction.
  // Open-addressing with linear probing. Size is 2× MaxDfaStates for low
  // load factor. Zero-initialized: 0 = empty slot, values are index + 1.
  int hash_table[MaxDfaStates * 2 > 0 ? MaxDfaStates * 2 : 1] = {};
  uint64_t state_hashes[MaxDfaStates] = {};

  struct ClosureResult {
    StateSet states;
    StateSet priority_states;
    DfaTagEntry tags;
    bool has_match = false;
    bool has_match_at_end = false;
    bool has_match_before_trailing_newline = false;
    bool has_match_before_newline = false;
    bool priority_match = false;
  };

  using AstType = std::remove_cvref_t<decltype(ast)>;
  struct Builder {
    DfaCoreResult<MaxDfaStates>& core;
    const NfaProgram<MaxNfaStates>& nfa;
    const AstType& ast;
    StateSet (&state_sets)[MaxDfaStates];
    StateSet (&priority_sets)[MaxDfaStates];
    int (&hash_table)[MaxDfaStates * 2 > 0 ? MaxDfaStates * 2 : 1];
    uint64_t (&state_hashes)[MaxDfaStates];

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

    // Two-phase epsilon closure that tracks NFA thread priority.
    //
    // Phase 1 processes priority seeds first: at Split states, the `next`
    // edge stays in the priority worklist while `alt` is demoted to the
    // normal worklist. All states reached in Phase 1 are marked in
    // priority_states.
    //
    // Phase 2 processes remaining (non-priority) seeds and any states
    // demoted from Phase 1. Both edges at Split states go to the normal
    // worklist.
    //
    // The visited bitset spans both phases, ensuring Phase 1 results
    // take precedence: if a state is reachable via both priority and
    // non-priority paths, it's processed in Phase 1 and marked as
    // priority.
    //
    // priority_match is set iff any Match state is in priority_states.
    constexpr ClosureResult epsilonClosure(
        const StateSet& prioritySeeds,
        const StateSet& normalSeeds,
        bool atBegin,
        bool afterNewline = false) noexcept {
      ClosureResult result;

      StateSet visited;
      int priority_wl[MaxNfaStates > 0 ? MaxNfaStates * 2 : 1] = {};
      int normal_wl[MaxNfaStates > 0 ? MaxNfaStates * 2 : 1] = {};
      int prioCount = 0;
      int normCount = 0;

      for (int i = 0; i < nfa.state_count; ++i) {
        if (prioritySeeds.test(i)) {
          priority_wl[prioCount++] = i;
        }
      }
      for (int i = 0; i < nfa.state_count; ++i) {
        if (normalSeeds.test(i) && !prioritySeeds.test(i)) {
          normal_wl[normCount++] = i;
        }
      }

      // Drain priority worklist before normal worklist. This ensures
      // states reachable via priority-only paths are processed and
      // marked before any non-priority path can claim them.
      while (prioCount > 0 || normCount > 0) {
        int s;
        bool is_priority;
        if (prioCount > 0) {
          s = priority_wl[--prioCount];
          is_priority = true;
        } else {
          s = normal_wl[--normCount];
          is_priority = false;
        }

        if (s < 0 || s >= nfa.state_count) {
          continue;
        }
        if (visited.test(s)) {
          continue;
        }
        visited.set(s);

        if (is_priority) {
          result.priority_states.set(s);
        }

        const auto& state = nfa.states[s];

        switch (state.kind) {
          case NfaStateKind::Split:
            if (is_priority) {
              if (state.next >= 0) {
                priority_wl[prioCount++] = state.next;
              }
              if (state.alt >= 0) {
                normal_wl[normCount++] = state.alt;
              }
            } else {
              if (state.next >= 0) {
                normal_wl[normCount++] = state.next;
              }
              if (state.alt >= 0) {
                normal_wl[normCount++] = state.alt;
              }
            }
            break;
          case NfaStateKind::CountedRepeat:
            // For greedy: alt (loop) is priority, next (exit) is fallback
            // For lazy: next (exit) is priority, alt (loop) is fallback
            if (is_priority) {
              if (state.greedy) {
                if (state.alt >= 0) {
                  priority_wl[prioCount++] = state.alt;
                }
                if (state.next >= 0) {
                  normal_wl[normCount++] = state.next;
                }
              } else {
                if (state.next >= 0) {
                  priority_wl[prioCount++] = state.next;
                }
                if (state.alt >= 0) {
                  normal_wl[normCount++] = state.alt;
                }
              }
            } else {
              if (state.next >= 0) {
                normal_wl[normCount++] = state.next;
              }
              if (state.alt >= 0) {
                normal_wl[normCount++] = state.alt;
              }
            }
            break;
          case NfaStateKind::GroupStart:
            result.tags.add(
                {static_cast<int8_t>(state.group_id), /*is_end=*/false});
            if (state.next >= 0) {
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
            }
            break;
          case NfaStateKind::GroupEnd:
            result.tags.add(
                {static_cast<int8_t>(state.group_id), /*is_end=*/true});
            if (state.next >= 0) {
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
            }
            break;
          case NfaStateKind::AnchorBegin:
            if (atBegin && state.next >= 0) {
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
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
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
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
              result.has_match_before_trailing_newline = true;
            }
            break;
          case NfaStateKind::AnchorBeginLine:
            if ((atBegin || afterNewline) && state.next >= 0) {
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
            }
            break;
          case NfaStateKind::AnchorEndLine:
            result.states.set(s);
            if (canReachMatch(state.next, visited)) {
              result.has_match_at_end = true;
              result.has_match_before_newline = true;
            }
            break;
          case NfaStateKind::Match:
            result.states.set(s);
            result.has_match = true;
            if (is_priority) {
              result.priority_match = true;
            }
            break;
          case NfaStateKind::Literal:
          case NfaStateKind::AnyByte:
          case NfaStateKind::CharClass:
            result.states.set(s);
            break;
        }
      }

      return result;
    }

    constexpr int findOrCreateState(const StateSet& nfaSet) noexcept {
      uint64_t h = nfaSet.hash();
      int slot = static_cast<int>(h % (MaxDfaStates * 2));
      while (true) {
        int entry = hash_table[slot];
        if (entry == 0) {
          break;
        }
        int stateIdx = entry - 1;
        if (state_hashes[stateIdx] == h && state_sets[stateIdx] == nfaSet) {
          return stateIdx;
        }
        slot = (slot + 1) % (MaxDfaStates * 2);
      }
      if (core.state_count >= MaxDfaStates) {
        return -2;
      }
      int idx = core.state_count++;
      state_sets[idx] = nfaSet;
      state_hashes[idx] = h;
      hash_table[slot] = idx + 1;
      // Initialize this state's interval transition row to dead state
      for (int iv = 0; iv < DfaCoreResult<MaxDfaStates>::kMaxIv; ++iv) {
        core.iv_trans[idx][iv] = -1;
      }
      return idx;
    }

    constexpr bool stateMatchesChar(int s, unsigned char c) noexcept {
      const auto& state = nfa.states[s];
      switch (state.kind) {
        case NfaStateKind::Literal:
          return static_cast<unsigned char>(state.ch) == c;
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

    constexpr void applyClosureToState(
        int dfaState, const ClosureResult& closure) noexcept {
      if (closure.has_match) {
        core.accepting.set(dfaState);
        if (closure.priority_match) {
          core.accept_early.set(dfaState);
        }
      }
      if (closure.has_match_at_end) {
        core.accepting_at_end.set(dfaState);
      }
      if (closure.has_match_before_trailing_newline) {
        core.accepting_before_trailing_newline.set(dfaState);
      }
      if (closure.has_match_before_newline) {
        core.accepting_before_newline.set(dfaState);
      }
      priority_sets[dfaState].orWith(closure.priority_states);
    }

    // Walk epsilon transitions from a starting NFA state and return
    // all character-consuming (or Match) NFA states reachable.
    constexpr StateSet epsilonReachCharStates(int start) noexcept {
      StateSet result;
      if (start < 0) {
        return result;
      }
      StateSet visited;
      int wl[MaxNfaStates > 0 ? MaxNfaStates : 1] = {};
      int wlCount = 0;
      wl[wlCount++] = start;
      while (wlCount > 0) {
        int s = wl[--wlCount];
        if (s < 0 || s >= nfa.state_count || visited.test(s)) {
          continue;
        }
        visited.set(s);
        const auto& st = nfa.states[s];
        switch (st.kind) {
          case NfaStateKind::Split:
          case NfaStateKind::CountedRepeat:
            if (st.next >= 0) {
              wl[wlCount++] = st.next;
            }
            if (st.alt >= 0) {
              wl[wlCount++] = st.alt;
            }
            break;
          case NfaStateKind::GroupStart:
          case NfaStateKind::GroupEnd:
          case NfaStateKind::AnchorBegin:
          case NfaStateKind::AnchorEnd:
          case NfaStateKind::AnchorStartOfString:
          case NfaStateKind::AnchorEndOfString:
          case NfaStateKind::AnchorEndOfStringOrNewline:
          case NfaStateKind::AnchorBeginLine:
          case NfaStateKind::AnchorEndLine:
            if (st.next >= 0) {
              wl[wlCount++] = st.next;
            }
            break;
          case NfaStateKind::Literal:
          case NfaStateKind::AnyByte:
          case NfaStateKind::CharClass:
          case NfaStateKind::Match:
            result.set(s);
            break;
        }
      }
      return result;
    }

    constexpr bool build() noexcept {
      StateSet startSet;
      startSet.set(nfa.start_state);
      StateSet emptySet;

      // Pre-compute possessive Split body/exit reachable states.
      constexpr int kMaxPossessiveSplits = 16;
      struct PossessiveSplitInfo {
        int splitIdx = -1;
        StateSet bodyReach;
        StateSet exitReach;
      };
      PossessiveSplitInfo possessiveSplits[kMaxPossessiveSplits] = {};
      int possessiveSplitCount = 0;

      if (nfa.has_possessive) {
        for (int i = 0;
             i < nfa.state_count && possessiveSplitCount < kMaxPossessiveSplits;
             ++i) {
          const auto& st = nfa.states[i];
          if (st.possessive &&
              (st.kind == NfaStateKind::Split ||
               st.kind == NfaStateKind::CountedRepeat)) {
            auto& info = possessiveSplits[possessiveSplitCount++];
            info.splitIdx = i;
            info.bodyReach = epsilonReachCharStates(
                st.kind == NfaStateKind::Split ? st.next : st.alt);
            info.exitReach = epsilonReachCharStates(
                st.kind == NfaStateKind::Split ? st.alt : st.next);
          }
        }
      }

      auto closureAnchored =
          epsilonClosure(startSet, emptySet, /*atBegin=*/true);
      auto closureUnanchored =
          epsilonClosure(startSet, emptySet, /*atBegin=*/false);

      // Precompute restart closure for unanchored DFA construction.
      // When AddRestart is true, this set is ORed into every moveSet
      // so the DFA never goes dead when a viable restart exists.
      [[maybe_unused]] StateSet restartClosure;
      [[maybe_unused]] StateSet restartPriority;
      if constexpr (AddRestart) {
        restartClosure = closureUnanchored.states;
        restartPriority = closureUnanchored.priority_states;
      }

      int anchoredState = findOrCreateState(closureAnchored.states);
      if (anchoredState == -2) {
        return false;
      }
      core.start_anchored = anchoredState;
      applyClosureToState(anchoredState, closureAnchored);
      core.start_anchored_tags = core.internTagEntry(closureAnchored.tags);

      int unanchoredState;
      if (closureAnchored.states == closureUnanchored.states) {
        unanchoredState = anchoredState;
      } else {
        unanchoredState = findOrCreateState(closureUnanchored.states);
        if (unanchoredState == -2) {
          return false;
        }
        applyClosureToState(unanchoredState, closureUnanchored);
      }
      core.start_unanchored = unanchoredState;
      core.start_unanchored_tags = core.internTagEntry(closureUnanchored.tags);

      // Compute start_after_newline: epsilon closure following
      // AnchorBeginLine (as if previous char was '\n') but not
      // AnchorBegin/AnchorStartOfString.
      auto closureAfterNewline = epsilonClosure(
          startSet, emptySet, /*atBegin=*/false, /*afterNewline=*/true);
      int afterNewlineState;
      if (closureAfterNewline.states == closureAnchored.states) {
        afterNewlineState = anchoredState;
      } else if (closureAfterNewline.states == closureUnanchored.states) {
        afterNewlineState = unanchoredState;
      } else {
        afterNewlineState = findOrCreateState(closureAfterNewline.states);
        if (afterNewlineState == -2) {
          return false;
        }
        applyClosureToState(afterNewlineState, closureAfterNewline);
      }
      core.start_after_newline = afterNewlineState;
      core.start_after_newline_tags =
          core.internTagEntry(closureAfterNewline.tags);

      // Worklist-based subset construction
      FixedBitset<MaxDfaStates> explored;
      FixedBitset<MaxDfaStates> queued;
      int worklist[MaxDfaStates] = {};
      int wlCount = 0;

      queued.set(anchoredState);
      worklist[wlCount++] = anchoredState;
      if (unanchoredState != anchoredState) {
        queued.set(unanchoredState);
        worklist[wlCount++] = unanchoredState;
      }
      if (!queued.test(afterNewlineState)) {
        queued.set(afterNewlineState);
        worklist[wlCount++] = afterNewlineState;
      }

      while (wlCount > 0) {
        int dfaState = worklist[--wlCount];
        if (explored.test(dfaState)) {
          continue;
        }
        explored.set(dfaState);

        const StateSet& curSet = state_sets[dfaState];
        const StateSet& curPriority = priority_sets[dfaState];

        // Interval-based subset construction: iterate over partition
        // intervals instead of all 256 characters. Each interval contains
        // characters that behave identically w.r.t. every NFA state.
        const auto& part = nfa.partition;
        int numIntervals = part.valid ? part.intervalCount() : 256;

        for (int iv = 0; iv < numIntervals; ++iv) {
          StateSet priorityMove;
          StateSet normalMove;

          // Determine if this interval contains '\n' for anchor handling
          bool isNewlineInterval = part.valid
              ? (part.charToInterval('\n') == iv)
              : (iv == static_cast<int>('\n'));

          if (part.valid) {
            // Use interval masks for O(1) per-state membership test
            for (int s = 0; s < nfa.state_count; ++s) {
              if (curSet.test(s) && ((nfa.states[s].interval_mask >> iv) & 1)) {
                if (nfa.states[s].next >= 0) {
                  if (curPriority.test(s)) {
                    priorityMove.set(nfa.states[s].next);
                  } else {
                    normalMove.set(nfa.states[s].next);
                  }
                }
              }
            }
          } else {
            // Fallback: treat each char as its own interval
            for (int s = 0; s < nfa.state_count; ++s) {
              if (curSet.test(s) &&
                  stateMatchesChar(s, static_cast<unsigned char>(iv))) {
                if (nfa.states[s].next >= 0) {
                  if (curPriority.test(s)) {
                    priorityMove.set(nfa.states[s].next);
                  } else {
                    normalMove.set(nfa.states[s].next);
                  }
                }
              }
            }
          }

          // AnchorEndLine is handled via accepting_before_newline
          // in the executor, not by adding to moveSet (which would
          // incorrectly consume the '\n').

          if constexpr (AddRestart) {
            // Restart states get their priority from the initial closure
            for (int i = 0; i < nfa.state_count; ++i) {
              if (restartClosure.test(i)) {
                if (restartPriority.test(i)) {
                  priorityMove.set(i);
                } else {
                  normalMove.set(i);
                }
              }
            }
          }

          // Possessive filtering: for each possessive Split whose body
          // and exit states both match this interval, remove exit-path
          // contributions so the body path is committed.
          for (int pi = 0; pi < possessiveSplitCount; ++pi) {
            const auto& psi = possessiveSplits[pi];
            bool bodyMatched = false;
            for (int s = 0; s < nfa.state_count && !bodyMatched; ++s) {
              if (!curSet.test(s) || !psi.bodyReach.test(s)) {
                continue;
              }
              if (part.valid) {
                if ((nfa.states[s].interval_mask >> iv) & 1) {
                  bodyMatched = true;
                }
              } else {
                if (stateMatchesChar(s, static_cast<unsigned char>(iv))) {
                  bodyMatched = true;
                }
              }
            }
            if (!bodyMatched) {
              continue;
            }
            for (int s = 0; s < nfa.state_count; ++s) {
              if (!curSet.test(s) || !psi.exitReach.test(s)) {
                continue;
              }
              bool exitMatches = part.valid
                  ? ((nfa.states[s].interval_mask >> iv) & 1)
                  : stateMatchesChar(s, static_cast<unsigned char>(iv));
              if (exitMatches && nfa.states[s].next >= 0) {
                priorityMove.clear(nfa.states[s].next);
                normalMove.clear(nfa.states[s].next);
              }
            }
          }

          if (priorityMove.empty() && normalMove.empty()) {
            continue;
          }

          // After consuming '\n', begin-of-line anchors should be
          // followable in the resulting epsilon closure.
          auto closure = epsilonClosure(
              priorityMove,
              normalMove,
              /*atBegin=*/false,
              /*afterNewline=*/isNewlineInterval);

          if (closure.states.empty()) {
            continue;
          }

          int targetState = findOrCreateState(closure.states);
          if (targetState == -2) {
            return false;
          }

          applyClosureToState(targetState, closure);

          core.iv_trans[dfaState][iv] = static_cast<int16_t>(targetState);
          core.iv_tags[dfaState][iv] = core.internTagEntry(closure.tags);

          if (!queued.test(targetState)) {
            queued.set(targetState);
            worklist[wlCount++] = targetState;
          }
        }
      }

      return true;
    }
  };

  Builder builder{
      core, nfa, ast, state_sets, priority_sets, hash_table, state_hashes};
  if (builder.build()) {
    core.valid = true;
  }

  return core;
}

// Phase 2 of DFA construction: table compression.
// Builds char_to_class mapping, compact transition tables, and range
// classifier from the interval-indexed tables produced by buildDfaCore.
template <int MaxDfaStates, int MaxNfaStates>
constexpr DfaProgram<MaxDfaStates> buildDfaFinalize(
    const DfaCoreResult<MaxDfaStates>& core,
    const NfaProgram<MaxNfaStates>& nfa) noexcept {
  DfaProgram<MaxDfaStates> prog;

  prog.state_count = core.state_count;
  prog.start_anchored = core.start_anchored;
  prog.start_unanchored = core.start_unanchored;
  prog.start_after_newline = core.start_after_newline;
  prog.start_anchored_tags = core.start_anchored_tags;
  prog.start_unanchored_tags = core.start_unanchored_tags;
  prog.start_after_newline_tags = core.start_after_newline_tags;
  prog.tag_entry_count = core.tag_entry_count;

  for (int i = 0; i < core.tag_entry_count; ++i) {
    prog.tag_entries[i] = core.tag_entries[i];
  }
  for (int s = 0; s < core.state_count; ++s) {
    if (core.accepting.test(s)) {
      prog.accepting.set(s);
    }
    if (core.accepting_at_end.test(s)) {
      prog.accepting_at_end.set(s);
    }
    if (core.accepting_before_trailing_newline.test(s)) {
      prog.accepting_before_trailing_newline.set(s);
    }
    if (core.accepting_before_newline.test(s)) {
      prog.accepting_before_newline.set(s);
    }
    if (core.accept_early.test(s)) {
      prog.accept_early.set(s);
    }
  }

  // Compute match_preference from accept_early vs accepting
  {
    bool anyAcceptEarly = false;
    bool allAcceptingAreEarly = true;
    for (int s = 0; s < core.state_count; ++s) {
      if (core.accepting.test(s)) {
        if (core.accept_early.test(s)) {
          anyAcceptEarly = true;
        } else {
          allAcceptingAreEarly = false;
        }
      }
    }
    if (anyAcceptEarly && allAcceptingAreEarly) {
      prog.match_preference = MatchPreference::AllLazy;
    } else if (anyAcceptEarly) {
      prog.match_preference = MatchPreference::Mixed;
    } else {
      prog.match_preference = MatchPreference::AllGreedy;
    }
  }

  // Build char_to_class directly from the alphabet partition.
  // When the partition is valid, each interval maps to a class ID.
  // This eliminates the O(256² × DFA_states) post-hoc comparison.
  const auto& part = nfa.partition;
  if (part.valid) {
    int numIv = part.intervalCount();
    if (numIv > DfaProgram<MaxDfaStates>::kMaxClasses) {
      return prog;
    }
    prog.num_classes = numIv;

    // Map each byte to its interval/class
    for (int c = 0; c < 256; ++c) {
      prog.char_to_class[c] = static_cast<uint8_t>(
          part.charToInterval(static_cast<unsigned char>(c)));
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
          if (core.iv_trans[s][prev] != core.iv_trans[s][c] ||
              core.iv_tags[s][prev] != core.iv_tags[s][c]) {
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
          return prog;
        }
        prog.char_to_class[c] = static_cast<uint8_t>(classId++);
      }
    }
    prog.num_classes = classId;
  }

  // Initialize compact tables to -1 (dead state), only for valid states
  for (int cls = 0; cls < prog.num_classes; ++cls) {
    for (int s = 0; s < core.state_count; ++s) {
      prog.transitions[cls][s] = -1;
    }
  }

  // Populate column-major compact tables from the interval tables.
  // Set kHasTagsBit in transitions where tag_actions is non-zero so the
  // executor can skip the tag_actions load on the common (no-tags) path.
  for (int s = 0; s < prog.state_count; ++s) {
    int numIv = part.valid ? part.intervalCount() : 256;
    for (int iv = 0; iv < numIv; ++iv) {
      int cls = part.valid ? iv : prog.char_to_class[iv];
      int16_t trans = core.iv_trans[s][iv];
      uint16_t tags = core.iv_tags[s][iv];
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

  prog.valid = true;
  return prog;
}

// Convenience wrapper: runs both phases in a single call.
// When called directly (not through separate holders), both phases share
// a single constexpr step budget. Use DfaCoreHolder + DfaHolder for
// independent budgets.
template <int MaxDfaStates, bool AddRestart = false, int MaxNfaStates>
constexpr DfaProgram<MaxDfaStates> buildDfa(
    const NfaProgram<MaxNfaStates>& nfa, const auto& ast) noexcept {
  auto core = buildDfaCore<MaxDfaStates, AddRestart>(nfa, ast);
  if (!core.valid) {
    return DfaProgram<MaxDfaStates>{};
  }
  return buildDfaFinalize<MaxDfaStates>(core, nfa);
}

} // namespace detail
} // namespace regex
} // namespace folly
