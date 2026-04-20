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

#include <folly/portability/Constexpr.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Direction.h>
#include <folly/regex/detail/DynamicBitset.h>
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

// Pre-compiled probe for possessive reverse verification.
// Each probe is a linear chain of interval masks (one per consuming step)
// extracted from the probe NFA at compile time. At runtime, the DFA
// executor walks backward through the input checking each character
// against the corresponding mask — pure register ops, no NFA simulation.
struct DfaPossessiveProbe {
  static constexpr int kMaxSteps = 16;
  uint64_t masks[kMaxSteps] = {};
  int step_count = 0;
  int max_repeat = -1;
  bool valid = false;
};

// Per-DFA-state lookaround probe conditions. During epsilon closure,
// when a lookaround NFA state is traversed, its probe condition is
// recorded. At runtime, the DFA executor evaluates these probes.
struct DfaLookaroundProbeEntry {
  static constexpr int kMaxProbes = 4;
  struct Probe {
    int probe_id = -1;
    bool negated = false;
    int lookbehind_width = -1; // >= 0 for fixed-width lookbehind
  };
  Probe probes[kMaxProbes] = {};
  int count = 0;

  constexpr void add(int probeId, bool neg, int lbWidth = -1) noexcept {
    // Deduplicate
    for (int i = 0; i < count; ++i) {
      if (probes[i].probe_id == probeId) {
        return;
      }
    }
    if (count < kMaxProbes) {
      probes[count++] = {probeId, neg, lbWidth};
    }
  }

  constexpr bool empty() const noexcept { return count == 0; }

  constexpr bool operator==(const DfaLookaroundProbeEntry& o) const noexcept {
    if (count != o.count) {
      return false;
    }
    for (int i = 0; i < count; ++i) {
      if (probes[i].probe_id != o.probes[i].probe_id ||
          probes[i].negated != o.probes[i].negated ||
          probes[i].lookbehind_width != o.probes[i].lookbehind_width) {
        return false;
      }
    }
    return true;
  }
};

template <int MaxDfaStates, int NumClasses = 128, int NumTagEntries = -1>
struct DfaProgram {
  static constexpr int kMaxTagEntries = NumTagEntries >= 0
      ? NumTagEntries
      : (MaxDfaStates * 4 > 512 ? 512 : MaxDfaStates * 4);
  static constexpr int kMaxClasses = NumClasses;

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

  // Pre-compiled possessive probes for reverse matching verification.
  static constexpr int kMaxProbes = 8;
  DfaPossessiveProbe probes[kMaxProbes] = {};
  int probe_count = 0;
  AlphabetPartition probe_partition;
  bool has_possessive_probes = false;

  // Per-DFA-state lookaround probe conditions. Indexed by DFA state.
  // Non-empty entries indicate probes that must be evaluated at runtime.
  DfaLookaroundProbeEntry lookaround_probes[MaxDfaStates] = {};
  bool has_lookaround_probes = false;

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

  // Per-DFA-state lookaround probe conditions.
  DfaLookaroundProbeEntry lookaround_probes[MaxDfaStates] = {};
  bool has_lookaround_probes = false;

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

// Pre-computed interval→NFA state bitsets. Computed once in its own
// ConstexprHolder and shared by both anchored and unanchored DFA builds.
template <int MaxNfaStates>
struct DfaIntervalSets {
  static constexpr int kMaxIv = AlphabetPartition::kMaxBoundaries + 1;
  NfaStateSet<MaxNfaStates> intervalStates[kMaxIv] = {};
  int numIntervals = 0;
  bool valid = false;
};

template <int MaxNfaStates>
constexpr DfaIntervalSets<MaxNfaStates> buildDfaIntervalSets(
    const NfaProgram<MaxNfaStates>& nfa) noexcept {
  DfaIntervalSets<MaxNfaStates> result;
  const auto& part = nfa.partition;
  if (!part.valid) {
    return result;
  }
  int numIv = part.intervalCount();
  result.numIntervals = numIv;
  for (int s = 0; s < nfa.state_count; ++s) {
    uint64_t mask = nfa.states[s].interval_mask;
    for (int iv = 0; iv < numIv && iv < 64; ++iv) {
      if ((mask >> iv) & 1) {
        result.intervalStates[iv].set(s);
      }
    }
  }
  result.valid = true;
  return result;
}

// Pre-computed epsilon-reachability to Match states. Bit s is set iff a
// Match state is reachable from s via epsilon transitions.
template <int MaxNfaStates>
struct DfaReachMap {
  FixedBitset<MaxNfaStates> matchReachable;
  bool valid = false;
};

template <int MaxNfaStates>
constexpr DfaReachMap<MaxNfaStates> buildDfaReachMap(
    const NfaProgram<MaxNfaStates>& nfa) noexcept {
  DfaReachMap<MaxNfaStates> result;
  for (int start = 0; start < nfa.state_count; ++start) {
    FixedBitset<MaxNfaStates> visited;
    int worklist[MaxNfaStates > 0 ? MaxNfaStates : 1] = {};
    int wlCount = 0;
    worklist[wlCount++] = start;
    bool found = false;
    while (wlCount > 0 && !found) {
      int s = worklist[--wlCount];
      if (s < 0 || s >= nfa.state_count || visited.test(s)) {
        continue;
      }
      visited.set(s);
      const auto& state = nfa.states[s];
      switch (state.kind) {
        case NfaStateKind::Match:
          found = true;
          break;
        case NfaStateKind::Split:
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
        case NfaStateKind::AnchorBegin:
        case NfaStateKind::AnchorEnd:
        case NfaStateKind::AnchorStartOfString:
        case NfaStateKind::AnchorEndOfString:
        case NfaStateKind::AnchorEndOfStringOrNewline:
        case NfaStateKind::AnchorBeginLine:
        case NfaStateKind::AnchorEndLine:
        case NfaStateKind::LookaheadProbe:
        case NfaStateKind::NegLookaheadProbe:
        case NfaStateKind::LookbehindProbe:
        case NfaStateKind::NegLookbehindProbe:
          if (state.next >= 0) {
            worklist[wlCount++] = state.next;
          }
          break;
        case NfaStateKind::Literal:
        case NfaStateKind::AnyByte:
        case NfaStateKind::CharClass:
          break;
      }
    }
    if (found) {
      result.matchReachable.set(start);
    }
  }
  result.valid = true;
  return result;
}

// Phase 1 of DFA construction: subset construction.
// Builds interval-indexed transition tables from the NFA.
template <
    int MaxDfaStates,
    bool AddRestart = false,
    Direction Dir = Direction::Forward,
    int MaxNfaStates>
constexpr DfaCoreResult<MaxDfaStates> buildDfaCore(
    const NfaProgram<MaxNfaStates>& nfa,
    const auto& ast,
    const DfaIntervalSets<MaxNfaStates>& ivSets,
    const DfaReachMap<MaxNfaStates>& reachMap) noexcept {
  DfaCoreResult<MaxDfaStates> core;

  if (nfa.start_state < 0) {
    return core;
  }

  using StateSet = NfaStateSet<MaxNfaStates>;

  struct ClosureResult {
    StateSet states;
    StateSet priority_states;
    DfaTagEntry tags;
    DfaLookaroundProbeEntry lookaround_probes;
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
    const DfaIntervalSets<MaxNfaStates>& ivSets;
    const DfaReachMap<MaxNfaStates>& reachMap;

    // Dynamically allocated workspace — avoids zero-initializing
    // oversized fixed arrays, saving constexpr evaluation steps.
    // ChunkedBuffer grows by appending new blocks without copying.
    ChunkedBuffer<StateSet> state_sets_;
    ChunkedBuffer<StateSet> priority_sets_;

    // DynamicBitset for worklist tracking — grows via append
    // without copying existing data.
    DynamicBitset explored_;
    DynamicBitset queued_;

    // Hash table for O(1) amortized state lookup during subset
    // construction. Open-addressing with linear probing.
    // Flat arrays — hash tables need true O(1) random access.
    // Zero-initialized: 0 = empty slot, values are index + 1.
    int* hash_table_ = nullptr;
    int hash_cap_ = 0;
    uint64_t* state_hashes_ = nullptr;
    int hashes_cap_ = 0;

    constexpr Builder(
        DfaCoreResult<MaxDfaStates>& c,
        const NfaProgram<MaxNfaStates>& n,
        const AstType& a,
        const DfaIntervalSets<MaxNfaStates>& iv,
        const DfaReachMap<MaxNfaStates>& rm) noexcept
        : core(c), nfa(n), ast(a), ivSets(iv), reachMap(rm) {
      hash_cap_ = 256;
      hash_table_ = new int[hash_cap_]{};
      hashes_cap_ = 128;
      state_hashes_ = new uint64_t[hashes_cap_]{};
    }

    constexpr ~Builder() {
      delete[] hash_table_;
      delete[] state_hashes_;
    }

    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    constexpr void ensureHashesCapacity(int needed) noexcept {
      if (needed <= hashes_cap_) {
        return;
      }
      int newCap = hashes_cap_;
      while (newCap < needed) {
        newCap *= 2;
      }
      auto* newH = new uint64_t[newCap]{};
      folly::constexpr_memcpy(newH, state_hashes_, core.state_count - 1);
      delete[] state_hashes_;
      state_hashes_ = newH;
      hashes_cap_ = newCap;
    }

    constexpr void rehash() noexcept {
      int newCap = hash_cap_ * 2;
      int* newTable = new int[newCap]{};
      for (int i = 0; i < core.state_count; ++i) {
        int slot =
            static_cast<int>(state_hashes_[i] % static_cast<unsigned>(newCap));
        while (newTable[slot] != 0) {
          slot = (slot + 1) % newCap;
        }
        newTable[slot] = i + 1;
      }
      delete[] hash_table_;
      hash_table_ = newTable;
      hash_cap_ = newCap;
    }

    // Append a new DFA state to all tracking structures.
    constexpr void appendState() noexcept {
      state_sets_.appendOne();
      priority_sets_.appendOne();
      explored_.append(false);
      queued_.append(false);
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

      prioritySeeds.forEachSetBit([&](int i) { priority_wl[prioCount++] = i; });
      normalSeeds.forEachSetBit([&](int i) {
        if (!prioritySeeds.test(i)) {
          normal_wl[normCount++] = i;
        }
      });

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
              if (state.repeat_mode != RepeatMode::Lazy) {
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
            if constexpr (Dir == Direction::Forward) {
              if (atBegin && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
            } else {
              // Reverse: AnchorBegin is an "end edge" anchor
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
              }
            }
            break;
          case NfaStateKind::AnchorEnd:
            if constexpr (Dir == Direction::Forward) {
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
              }
            } else {
              // Reverse: AnchorEnd is a "start edge" anchor
              if (atBegin && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
            }
            break;
          case NfaStateKind::AnchorStartOfString:
            if constexpr (Dir == Direction::Forward) {
              if (atBegin && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
            } else {
              // Reverse: AnchorStartOfString is an "end edge" anchor
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
              }
            }
            break;
          case NfaStateKind::AnchorEndOfString:
            if constexpr (Dir == Direction::Forward) {
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
              }
            } else {
              // Reverse: AnchorEndOfString is a "start edge" anchor
              if (atBegin && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
            }
            break;
          case NfaStateKind::AnchorEndOfStringOrNewline:
            if constexpr (Dir == Direction::Forward) {
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
                result.has_match_before_trailing_newline = true;
              }
            } else {
              // Reverse: AnchorEndOfStringOrNewline — "start edge" anchor.
              // Followable at the anchored start (atBegin) and also when
              // starting at a trailing newline (afterNewline), since \Z
              // matches both at end-of-string and before trailing newline.
              if ((atBegin || afterNewline) && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
              // Also followable after a newline boundary
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_before_trailing_newline = true;
              }
            }
            break;
          case NfaStateKind::AnchorBeginLine:
            if constexpr (Dir == Direction::Forward) {
              if ((atBegin || afterNewline) && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
            } else {
              // Reverse: AnchorBeginLine is an "end edge" newline anchor
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
                result.has_match_before_newline = true;
              }
            }
            break;
          case NfaStateKind::AnchorEndLine:
            if constexpr (Dir == Direction::Forward) {
              result.states.set(s);
              if (state.next >= 0 && reachMap.matchReachable.test(state.next)) {
                result.has_match_at_end = true;
                result.has_match_before_newline = true;
              }
            } else {
              // Reverse: AnchorEndLine is a "start edge" newline anchor
              if ((atBegin || afterNewline) && state.next >= 0) {
                if (is_priority) {
                  priority_wl[prioCount++] = state.next;
                } else {
                  normal_wl[normCount++] = state.next;
                }
              }
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
          case NfaStateKind::LookaheadProbe:
          case NfaStateKind::NegLookaheadProbe:
          case NfaStateKind::LookbehindProbe:
          case NfaStateKind::NegLookbehindProbe:
            // Record probe condition for runtime evaluation
            result.lookaround_probes.add(
                state.probe_id,
                state.kind == NfaStateKind::NegLookaheadProbe ||
                    state.kind == NfaStateKind::NegLookbehindProbe,
                state.lookbehind_width);
            // Follow next unconditionally — probe is deferred to runtime
            if (state.next >= 0) {
              if (is_priority) {
                priority_wl[prioCount++] = state.next;
              } else {
                normal_wl[normCount++] = state.next;
              }
            }
            break;
        }
      }

      return result;
    }

    constexpr int findOrCreateState(const StateSet& nfaSet) noexcept {
      uint64_t h = nfaSet.hash();
      int slot = static_cast<int>(h % static_cast<unsigned>(hash_cap_));
      while (true) {
        int entry = hash_table_[slot];
        if (entry == 0) {
          break;
        }
        int stateIdx = entry - 1;
        if (state_hashes_[stateIdx] == h && state_sets_[stateIdx] == nfaSet) {
          return stateIdx;
        }
        slot = (slot + 1) % hash_cap_;
      }
      if (core.state_count >= MaxDfaStates) {
        return -2;
      }
      int idx = core.state_count++;
      appendState();
      ensureHashesCapacity(core.state_count);
      state_sets_[idx] = nfaSet;
      state_hashes_[idx] = h;
      hash_table_[slot] = idx + 1;
      // Initialize this state's interval transition row to dead state
      for (int iv = 0; iv < DfaCoreResult<MaxDfaStates>::kMaxIv; ++iv) {
        core.iv_trans[idx][iv] = -1;
      }
      // Rehash when load factor exceeds 75%
      if (core.state_count * 4 > hash_cap_ * 3) {
        rehash();
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
        case NfaStateKind::LookaheadProbe:
        case NfaStateKind::NegLookaheadProbe:
        case NfaStateKind::LookbehindProbe:
        case NfaStateKind::NegLookbehindProbe:
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
      priority_sets_[dfaState].orWith(closure.priority_states);
      // Merge lookaround probe conditions into DFA state
      if (!closure.lookaround_probes.empty()) {
        for (int i = 0; i < closure.lookaround_probes.count; ++i) {
          const auto& p = closure.lookaround_probes.probes[i];
          core.lookaround_probes[dfaState].add(
              p.probe_id, p.negated, p.lookbehind_width);
        }
        core.has_lookaround_probes = true;
      }
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
          case NfaStateKind::LookaheadProbe:
          case NfaStateKind::NegLookaheadProbe:
          case NfaStateKind::LookbehindProbe:
          case NfaStateKind::NegLookbehindProbe:
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
          if (st.isPossessive() &&
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
      [[maybe_unused]] StateSet restartPrioSet;
      [[maybe_unused]] StateSet restartNormSet;
      if constexpr (AddRestart) {
        auto restartClosure = closureUnanchored.states;
        auto restartPriority = closureUnanchored.priority_states;
        restartPrioSet = restartClosure;
        restartPrioSet.andWith(restartPriority);
        restartNormSet = restartClosure;
        restartNormSet.andNotWith(restartPriority);
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

      // Worklist-based subset construction — uses the Builder's
      // DynamicBitset for explored_/queued_ tracking. Worklist is a
      // flat array since it needs LIFO stack access by index.
      int* worklist = new int[MaxDfaStates > 0 ? MaxDfaStates : 1]{};
      int wlCount = 0;

      queued_.set(anchoredState);
      worklist[wlCount++] = anchoredState;
      if (unanchoredState != anchoredState) {
        queued_.set(unanchoredState);
        worklist[wlCount++] = unanchoredState;
      }
      if (!queued_.test(afterNewlineState)) {
        queued_.set(afterNewlineState);
        worklist[wlCount++] = afterNewlineState;
      }

      while (wlCount > 0) {
        int dfaState = worklist[--wlCount];
        if (explored_.test(dfaState)) {
          continue;
        }
        explored_.set(dfaState);

        const StateSet& curSet = state_sets_[dfaState];
        const StateSet& curPriority = priority_sets_[dfaState];

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
            // Early-out: skip intervals with no matching NFA states
            if (!curSet.anyIntersection(ivSets.intervalStates[iv])) {
              if constexpr (!AddRestart) {
                continue;
              }
            }

            // Bulk bitset ops: intersect current DFA state set with
            // pre-computed interval membership, then split by priority.
            StateSet matching = ivSets.intervalStates[iv];
            matching.andWith(curSet);

            StateSet prioMatching = matching;
            prioMatching.andWith(curPriority);

            StateSet normMatching = matching;
            normMatching.andNotWith(curPriority);

            prioMatching.forEachSetBit([&](int s) {
              if (nfa.states[s].next >= 0) {
                priorityMove.set(nfa.states[s].next);
              }
            });
            normMatching.forEachSetBit([&](int s) {
              if (nfa.states[s].next >= 0) {
                normalMove.set(nfa.states[s].next);
              }
            });
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
            priorityMove.orWith(restartPrioSet);
            normalMove.orWith(restartNormSet);
          }

          // Possessive filtering: for each possessive Split whose body
          // and exit states both match this interval, remove exit-path
          // contributions so the body path is committed.
          for (int pi = 0; pi < possessiveSplitCount; ++pi) {
            const auto& psi = possessiveSplits[pi];
            bool bodyMatched = false;
            if (part.valid) {
              StateSet bodyCheck = curSet;
              bodyCheck.andWith(psi.bodyReach);
              bodyCheck.andWith(ivSets.intervalStates[iv]);
              bodyMatched = !bodyCheck.empty();
            } else {
              for (int s = 0; s < nfa.state_count && !bodyMatched; ++s) {
                if (!curSet.test(s) || !psi.bodyReach.test(s)) {
                  continue;
                }
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
            delete[] worklist;
            return false;
          }

          applyClosureToState(targetState, closure);

          core.iv_trans[dfaState][iv] = static_cast<int16_t>(targetState);
          core.iv_tags[dfaState][iv] = core.internTagEntry(closure.tags);

          if (!queued_.test(targetState)) {
            queued_.set(targetState);
            worklist[wlCount++] = targetState;
          }
        }
      }

      delete[] worklist;
      return true;
    }
  };

  Builder builder(core, nfa, ast, ivSets, reachMap);
  if (builder.build()) {
    core.valid = true;
  }

  return core;
}

// Phase 2 of DFA construction: table compression.
// Builds char_to_class mapping, compact transition tables, and range
// classifier from the interval-indexed tables produced by buildDfaCore.

// Compute the number of equivalence classes for a DFA core result.
// When the NFA's alphabet partition is valid, this is simply the interval
// count; otherwise falls back to brute-force column comparison.
template <int MaxDfaStates, int MaxNfaStates>
constexpr int computeDfaClassCount(
    const DfaCoreResult<MaxDfaStates>& core,
    const NfaProgram<MaxNfaStates>& nfa) noexcept {
  const auto& part = nfa.partition;
  if (part.valid) {
    return part.intervalCount();
  }
  // Fallback: compute equiv classes from iv_trans columns
  int classId = 0;
  uint8_t classMap[256] = {};
  for (int c = 0; c < 256; ++c) {
    bool matched = false;
    for (int prev = 0; prev < c; ++prev) {
      bool same = true;
      for (int s = 0; s < core.state_count && same; ++s) {
        if (core.iv_trans[s][prev] != core.iv_trans[s][c] ||
            core.iv_tags[s][prev] != core.iv_tags[s][c]) {
          same = false;
        }
      }
      if (same) {
        classMap[c] = classMap[prev];
        matched = true;
        break;
      }
    }
    if (!matched) {
      classMap[c] = static_cast<uint8_t>(classId++);
    }
  }
  return classId;
}
// OutputStates/OutputClasses/OutputTagEntries precisely size the output
// DfaProgram; CoreMaxStates is the workspace size of the DfaCoreResult.
template <
    int OutputStates,
    int OutputClasses = 128,
    int OutputTagEntries = -1,
    int CoreMaxStates,
    int MaxNfaStates>
constexpr DfaProgram<OutputStates, OutputClasses, OutputTagEntries>
buildDfaFinalize(
    const DfaCoreResult<CoreMaxStates>& core,
    const NfaProgram<MaxNfaStates>& nfa) noexcept {
  DfaProgram<OutputStates, OutputClasses, OutputTagEntries> prog;

  prog.state_count = core.state_count;
  prog.start_anchored = core.start_anchored;
  prog.start_unanchored = core.start_unanchored;
  prog.start_after_newline = core.start_after_newline;
  prog.start_anchored_tags = core.start_anchored_tags;
  prog.start_unanchored_tags = core.start_unanchored_tags;
  prog.start_after_newline_tags = core.start_after_newline_tags;
  prog.tag_entry_count = core.tag_entry_count;

  folly::constexpr_memcpy(
      prog.tag_entries, core.tag_entries, core.tag_entry_count);

  prog.accepting.copyFrom(core.accepting);
  prog.accepting_at_end.copyFrom(core.accepting_at_end);
  prog.accepting_before_trailing_newline.copyFrom(
      core.accepting_before_trailing_newline);
  prog.accepting_before_newline.copyFrom(core.accepting_before_newline);
  prog.accept_early.copyFrom(core.accept_early);

  // Compute match_preference from accept_early vs accepting
  {
    bool anyAcceptEarly = prog.accepting.anyIntersection(prog.accept_early);
    bool allAcceptingAreEarly = prog.accepting.isSubsetOf(prog.accept_early);
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
    if (numIv > decltype(prog)::kMaxClasses) {
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
        if (classId >= decltype(prog)::kMaxClasses) {
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
        trans |= decltype(prog)::kHasTagsBit;
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
        if (bcount >= decltype(prog)::kMaxRangeBoundaries) {
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

  // Copy lookaround probe metadata from core to final DFA.
  prog.has_lookaround_probes = core.has_lookaround_probes;
  if (core.has_lookaround_probes) {
    for (int s = 0; s < core.state_count; ++s) {
      prog.lookaround_probes[s] = core.lookaround_probes[s];
    }
  }

  prog.valid = true;
  return prog;
}

} // namespace detail
} // namespace regex
} // namespace folly
