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
#include <stdexcept>
#include <type_traits>

#include <folly/lang/Exception.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/FixedBitset.h>

namespace folly {
namespace regex {
namespace detail {

enum class NfaStateKind : uint8_t {
  Match,
  Split,
  Literal,
  AnyByte,
  CharClass,
  CountedRepeat,
  AnchorBegin,
  AnchorEnd,
  AnchorStartOfString,
  AnchorEndOfString,
  AnchorEndOfStringOrNewline,
  AnchorBeginLine,
  AnchorEndLine,
  GroupStart,
  GroupEnd,
};

struct NfaState {
  NfaStateKind kind = NfaStateKind::Match;
  char ch = '\0';
  int char_class_index = -1;
  int next = -1;
  int alt = -1;
  int group_id = 0;
  uint64_t interval_mask = 0;
  int counter_id = -1;
  int min_repeat = 0;
  int max_repeat = -1;
  bool greedy = true;
  bool possessive = false;
  // Alternation discriminator fields.
  // discriminator_offset: set on the outermost Split of an alternation chain;
  //   the offset from the current position to peek at for branch selection.
  // discriminator_alt_char / discriminator_next_char: packed character info
  //   for the alt / next branch of this Split. Encoding:
  //     -1 = no info (include branch unconditionally)
  //     0-255 = literal character value
  //     256+ = char class index (value - 256)
  int discriminator_offset = -1;
  int discriminator_alt_char = -1;
  int discriminator_next_char = -1;
};

// Global alphabet partition: boundary points that divide the 256-byte
// alphabet into intervals where all characters within an interval
// behave identically with respect to every NFA state in the pattern.
struct AlphabetPartition {
  static constexpr int kMaxBoundaries = 63;
  uint8_t boundaries[kMaxBoundaries] = {};
  int boundary_count = 0;
  bool valid = false;

  constexpr int intervalCount() const noexcept { return boundary_count + 1; }

  // Map a character to its interval ID via binary search.
  constexpr int charToInterval(unsigned char c) const noexcept {
    int lo = 0, hi = boundary_count;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (c < boundaries[mid]) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo;
  }

  // Get a representative character for an interval.
  constexpr unsigned char representative(int interval) const noexcept {
    if (interval == 0) {
      return 0;
    }
    return boundaries[interval - 1];
  }

  constexpr void addBoundary(unsigned char b) noexcept {
    if (b == 0) {
      return;
    }
    // Check for duplicate
    for (int i = 0; i < boundary_count; ++i) {
      if (boundaries[i] == b) {
        return;
      }
    }
    if (boundary_count >= kMaxBoundaries) {
      return;
    }
    boundaries[boundary_count++] = b;
  }

  constexpr void sort() noexcept {
    // Insertion sort — boundary_count is small
    for (int i = 1; i < boundary_count; ++i) {
      uint8_t key = boundaries[i];
      int j = i - 1;
      while (j >= 0 && boundaries[j] > key) {
        boundaries[j + 1] = boundaries[j];
        --j;
      }
      boundaries[j + 1] = key;
    }
  }
};

template <int MaxStates>
struct NfaProgram {
  NfaState states[MaxStates] = {};
  int state_count = 0;
  int start_state = -1;
  AlphabetPartition partition;
  int num_counters = 0;
  int max_repeat_value = 0;
  bool has_possessive = false;
  bool has_discriminators = false;

  constexpr int addState(NfaState s) noexcept {
    int idx = state_count++;
    states[idx] = s;
    return idx;
  }
};

struct NfaFragment {
  int start;
  int end; // index of a state whose 'next' needs to be patched
};

template <int MaxStates>
constexpr NfaProgram<MaxStates> buildNfa(const auto& ast) noexcept {
  NfaProgram<MaxStates> prog;

  using AstType = std::remove_cvref_t<decltype(ast)>;
  struct Builder {
    NfaProgram<MaxStates>& prog;
    const AstType& ast;

    constexpr void patch(int stateIdx, int target) noexcept {
      if (stateIdx >= 0 && prog.states[stateIdx].next < 0) {
        prog.states[stateIdx].next = target;
      }
    }

    constexpr void patchList(int head, int target) noexcept {
      int cur = head;
      while (cur >= 0) {
        auto& s = prog.states[cur];
        int next = s.next;
        s.next = target;
        if (next == cur)
          break;
        cur = (next < 0) ? -1 : next;
      }
    }

    // Pack a branch's discriminator character into a single int.
    // Returns: 0-255 for a literal char, 256+ for a char class index, -1 if
    // the character at `offset` could not be resolved.
    constexpr int packDiscriminatorChar(int astChildIdx, int offset) noexcept {
      auto charInfo = resolveCharAtOffset(ast, astChildIdx, offset);
      if (!charInfo.valid) {
        return -1;
      }
      const auto& n = ast.nodes[charInfo.nodeIdx];
      if (n.kind == NodeKind::Literal) {
        return static_cast<int>(
            static_cast<unsigned char>(n.literal[charInfo.charOffset]));
      } else if (n.kind == NodeKind::CharClass) {
        return 256 + n.char_class_index;
      }
      return -1;
    }

    constexpr NfaFragment build(int nodeIdx) noexcept {
      if (nodeIdx < 0) {
        NfaState s;
        s.kind = NfaStateKind::Split;
        int idx = prog.addState(s);
        return {idx, idx};
      }

      const auto& node = ast.nodes[nodeIdx];

      switch (node.kind) {
        case NodeKind::Empty: {
          NfaState s;
          s.kind = NfaStateKind::Split;
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
        case NodeKind::Literal: {
          auto lit = node.literal;
          int firstState = -1;
          int prevState = -1;
          for (std::size_t i = 0; i < lit.size(); ++i) {
            NfaState s;
            s.kind = NfaStateKind::Literal;
            s.ch = lit[i];
            s.next = -1;
            int idx = prog.addState(s);
            if (firstState < 0) {
              firstState = idx;
            }
            if (prevState >= 0) {
              prog.states[prevState].next = idx;
            }
            prevState = idx;
          }
          return {firstState, prevState};
        }
        case NodeKind::AnyChar:
          // AnyChar must be resolved by applyDotAllFlag before NFA
          // construction.
          folly::throw_exception<std::runtime_error>(
              "unresolved AnyChar node in NFA construction");
        case NodeKind::AnyByte: {
          NfaState s;
          s.kind = NfaStateKind::AnyByte;
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
        case NodeKind::CharClass: {
          NfaState s;
          s.kind = NfaStateKind::CharClass;
          s.char_class_index = node.char_class_index;
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
        case NodeKind::Anchor: {
          NfaState s;
          if (node.anchor == AnchorKind::Begin) {
            s.kind = NfaStateKind::AnchorBegin;
          } else if (node.anchor == AnchorKind::End) {
            s.kind = NfaStateKind::AnchorEnd;
          } else if (node.anchor == AnchorKind::StartOfString) {
            s.kind = NfaStateKind::AnchorStartOfString;
          } else if (node.anchor == AnchorKind::EndOfString) {
            s.kind = NfaStateKind::AnchorEndOfString;
          } else if (node.anchor == AnchorKind::EndOfStringOrNewline) {
            s.kind = NfaStateKind::AnchorEndOfStringOrNewline;
          } else if (node.anchor == AnchorKind::BeginLine) {
            s.kind = NfaStateKind::AnchorBeginLine;
          } else if (node.anchor == AnchorKind::EndLine) {
            s.kind = NfaStateKind::AnchorEndLine;
          } else {
            s.kind = NfaStateKind::AnchorBegin;
          }
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
        case NodeKind::Group: {
          NfaFragment inner = build(node.child_begin);
          if (node.capturing) {
            NfaState gs;
            gs.kind = NfaStateKind::GroupStart;
            gs.group_id = node.group_id;
            gs.next = inner.start;
            int gsIdx = prog.addState(gs);

            NfaState ge;
            ge.kind = NfaStateKind::GroupEnd;
            ge.group_id = node.group_id;
            ge.next = -1;
            int geIdx = prog.addState(ge);

            patch(inner.end, geIdx);

            return {gsIdx, geIdx};
          }
          return inner;
        }
        case NodeKind::Sequence: {
          int child = node.child_begin;
          if (child < 0) {
            NfaState s;
            s.kind = NfaStateKind::Split;
            s.next = -1;
            int idx = prog.addState(s);
            return {idx, idx};
          }

          NfaFragment first = build(child);
          NfaFragment current = first;
          child = ast.nodes[child].child_end;

          while (child >= 0) {
            NfaFragment next = build(child);
            patch(current.end, next.start);
            current.end = next.end;
            child = ast.nodes[child].child_end;
          }

          return {first.start, current.end};
        }
        case NodeKind::Alternation: {
          int child = node.child_begin;
          if (child < 0) {
            NfaState s;
            s.kind = NfaStateKind::Split;
            s.next = -1;
            int idx = prog.addState(s);
            return {idx, idx};
          }

          NfaFragment first = build(child);
          int nextChild = ast.nodes[child].child_end;

          if (nextChild < 0)
            return first;

          NfaFragment second = build(nextChild);

          NfaState split;
          split.kind = NfaStateKind::Split;
          split.next = first.start;
          split.alt = second.start;
          if (node.discriminator_offset >= 0) {
            split.discriminator_alt_char =
                packDiscriminatorChar(nextChild, node.discriminator_offset);
            split.discriminator_next_char =
                packDiscriminatorChar(child, node.discriminator_offset);
          }
          int splitIdx = prog.addState(split);

          // Dangling end: we need a join point (pass-through)
          NfaState join;
          join.kind = NfaStateKind::Split;
          join.next = -1;
          int joinIdx = prog.addState(join);

          patch(first.end, joinIdx);
          patch(second.end, joinIdx);

          // Handle more than 2 alternatives
          int moreChild = ast.nodes[nextChild].child_end;
          while (moreChild >= 0) {
            NfaFragment alt = build(moreChild);
            patch(alt.end, joinIdx);

            NfaState newSplit;
            newSplit.kind = NfaStateKind::Split;
            newSplit.next = splitIdx;
            newSplit.alt = alt.start;
            if (node.discriminator_offset >= 0) {
              newSplit.discriminator_alt_char =
                  packDiscriminatorChar(moreChild, node.discriminator_offset);
            }
            splitIdx = prog.addState(newSplit);

            moreChild = ast.nodes[moreChild].child_end;
          }

          if (node.discriminator_offset >= 0) {
            prog.states[splitIdx].discriminator_offset =
                node.discriminator_offset;
            prog.has_discriminators = true;
          }

          return {splitIdx, joinIdx};
        }
        case NodeKind::Repeat: {
          int innerIdx = node.child_begin;
          int minR = node.min_repeat;
          int maxR = node.max_repeat;
          bool greedy = node.greedy;

          if (minR == 0 && maxR == 1) {
            // ? quantifier
            NfaFragment inner = build(innerIdx);
            NfaState split;
            split.kind = NfaStateKind::Split;
            split.possessive = node.possessive;
            if (node.possessive) {
              prog.has_possessive = true;
            }
            if (greedy) {
              split.next = inner.start;
              split.alt = -1;
            } else {
              split.next = -1;
              split.alt = inner.start;
            }
            int splitIdx = prog.addState(split);

            NfaState join;
            join.kind = NfaStateKind::Split;
            join.next = -1;
            int joinIdx = prog.addState(join);

            patch(inner.end, joinIdx);
            if (greedy) {
              prog.states[splitIdx].alt = joinIdx;
            } else {
              prog.states[splitIdx].next = joinIdx;
            }

            return {splitIdx, joinIdx};
          }

          if (minR == 0 && maxR < 0) {
            // * quantifier
            NfaFragment inner = build(innerIdx);
            NfaState split;
            split.kind = NfaStateKind::Split;
            split.possessive = node.possessive;
            if (node.possessive) {
              prog.has_possessive = true;
            }
            if (greedy) {
              split.next = inner.start;
              split.alt = -1;
            } else {
              split.next = -1;
              split.alt = inner.start;
            }
            int splitIdx = prog.addState(split);

            patch(inner.end, splitIdx);

            NfaState out;
            out.kind = NfaStateKind::Split;
            out.next = -1;
            int outIdx = prog.addState(out);

            if (greedy) {
              prog.states[splitIdx].alt = outIdx;
            } else {
              prog.states[splitIdx].next = outIdx;
            }

            return {splitIdx, outIdx};
          }

          if (minR == 1 && maxR < 0) {
            // + quantifier
            NfaFragment inner = build(innerIdx);
            NfaState split;
            split.kind = NfaStateKind::Split;
            split.possessive = node.possessive;
            if (node.possessive) {
              prog.has_possessive = true;
            }
            if (greedy) {
              split.next = inner.start;
              split.alt = -1;
            } else {
              split.next = -1;
              split.alt = inner.start;
            }
            int splitIdx = prog.addState(split);

            patch(inner.end, splitIdx);

            NfaState out;
            out.kind = NfaStateKind::Split;
            out.next = -1;
            int outIdx = prog.addState(out);

            if (greedy) {
              prog.states[splitIdx].alt = outIdx;
            } else {
              prog.states[splitIdx].next = outIdx;
            }

            return {inner.start, outIdx};
          }

          // General {n,m}: use a CountedRepeat loop state instead of
          // unrolling into n+m copies. This reduces NFA state count from
          // O(n) to O(1) per counted repetition.
          NfaFragment inner = build(innerIdx);
          int counterId = prog.num_counters++;
          if (maxR > prog.max_repeat_value) {
            prog.max_repeat_value = maxR;
          }
          if (minR > prog.max_repeat_value) {
            prog.max_repeat_value = minR;
          }

          NfaState cr;
          cr.kind = NfaStateKind::CountedRepeat;
          cr.counter_id = counterId;
          cr.min_repeat = minR;
          cr.max_repeat = maxR;
          cr.greedy = greedy;
          cr.possessive = node.possessive;
          if (node.possessive) {
            prog.has_possessive = true;
          }
          cr.alt = inner.start; // loop back to inner fragment
          cr.next = -1; // exit target (will be patched by caller)
          int crIdx = prog.addState(cr);

          // Patch inner fragment end to loop back to the CountedRepeat state
          patch(inner.end, crIdx);

          // The CountedRepeat state is both the entry point (it checks the
          // counter and decides to enter inner or exit) and the loop target.
          return {crIdx, crIdx};
        }
        // Unreachable for NFA-compatible patterns (parser sets
        // nfa_compatible=false for these)
        case NodeKind::Lookahead:
        case NodeKind::NegLookahead:
        case NodeKind::Lookbehind:
        case NodeKind::NegLookbehind:
        case NodeKind::Backref:
        case NodeKind::WordBoundary:
        case NodeKind::NegWordBoundary:
        case NodeKind::Dead: {
          NfaState s;
          s.kind = NfaStateKind::Split;
          int idx = prog.addState(s);
          return {idx, idx};
        }
      }

      // Unreachable
      NfaState s;
      s.kind = NfaStateKind::Match;
      int idx = prog.addState(s);
      return {idx, idx};
    }
  };

  Builder builder{prog, ast};
  auto frag = builder.build(ast.root);

  // Add final match state
  NfaState matchState;
  matchState.kind = NfaStateKind::Match;
  int matchIdx = prog.addState(matchState);
  builder.patch(frag.end, matchIdx);

  prog.start_state = frag.start;

  // Compute the global alphabet partition from all character-consuming
  // NFA states. Each boundary point divides the 256-byte alphabet into
  // intervals where all characters within an interval behave identically
  // with respect to every NFA state.
  AlphabetPartition& part = prog.partition;
  for (int i = 0; i < prog.state_count; ++i) {
    const auto& s = prog.states[i];
    if (s.kind == NfaStateKind::Literal) {
      auto uc = static_cast<unsigned char>(s.ch);
      part.addBoundary(uc);
      if (uc < 255) {
        part.addBoundary(uc + 1);
      }
    } else if (s.kind == NfaStateKind::CharClass) {
      const auto& cc = ast.char_classes[s.char_class_index];
      for (int r = 0; r < cc.range_count; ++r) {
        part.addBoundary(ast.ranges[cc.range_offset + r].lo);
        auto hi = ast.ranges[cc.range_offset + r].hi;
        if (hi < 255) {
          part.addBoundary(hi + 1);
        }
      }
    }
    // AnyByte matches everything — no boundaries needed
  }
  part.sort();

  if (part.intervalCount() <= 64) {
    part.valid = true;

    // Populate interval masks for each NFA state
    for (int i = 0; i < prog.state_count; ++i) {
      auto& s = prog.states[i];
      uint64_t mask = 0;
      switch (s.kind) {
        case NfaStateKind::Literal: {
          int iv = part.charToInterval(static_cast<unsigned char>(s.ch));
          mask = uint64_t{1} << iv;
          break;
        }
        case NfaStateKind::AnyByte:
          mask = (part.intervalCount() >= 64)
              ? ~uint64_t{0}
              : (uint64_t{1} << part.intervalCount()) - 1;
          break;
        case NfaStateKind::CharClass: {
          const auto& cc = ast.char_classes[s.char_class_index];
          for (int r = 0; r < cc.range_count; ++r) {
            auto lo = ast.ranges[cc.range_offset + r].lo;
            auto hi = ast.ranges[cc.range_offset + r].hi;
            for (int iv = part.charToInterval(lo); iv < part.intervalCount();
                 ++iv) {
              auto rep = part.representative(iv);
              if (rep > hi) {
                break;
              }
              mask |= uint64_t{1} << iv;
            }
          }
          break;
        }
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
          break;
      }
      s.interval_mask = mask;
    }
  }

  return prog;
}

// Unroll qualifying CountedRepeat states directly in the NFA.
// Small repeats (max_repeat * body_size <= threshold) are expanded into
// explicit copies of the body, eliminating CountedRepeat states that the
// DFA's epsilonClosure would otherwise handle incorrectly. For nested
// repeats, force-unrolls the cheaper level to provide DFA state diversity.
// Returns an NfaProgram with potentially more states but fewer (or zero)
// CountedRepeat states.
template <int MaxOutputStates, int MaxInputStates>
constexpr NfaProgram<MaxOutputStates> unrollCountedRepeats(
    const NfaProgram<MaxInputStates>& input) noexcept {
  NfaProgram<MaxOutputStates> out;

  for (int i = 0; i < input.state_count; ++i) {
    out.states[i] = input.states[i];
  }
  out.state_count = input.state_count;
  out.start_state = input.start_state;
  out.partition = input.partition;
  out.num_counters = input.num_counters;
  out.max_repeat_value = input.max_repeat_value;
  out.has_possessive = input.has_possessive;
  out.has_discriminators = input.has_discriminators;

  constexpr int kMaxCRs = 16;
  constexpr int kMaxBodySize = 64;
  constexpr int kSmallRepeatThreshold = 16;

  struct CRInfo {
    int index = -1;
    int bodyStates[kMaxBodySize] = {};
    int bodyCount = 0;
    int bodyEntryOffset = -1;
    int cost = 0;
    bool shouldUnroll = false;
    int nestParent = -1;
  };

  CRInfo crs[kMaxCRs] = {};
  int crCount = 0;

  for (int i = 0; i < out.state_count && crCount < kMaxCRs; ++i) {
    if (out.states[i].kind == NfaStateKind::CountedRepeat) {
      crs[crCount++].index = i;
    }
  }

  if (crCount == 0) {
    return out;
  }

  // Compute body states for each CR via worklist walk from CR.alt,
  // stopping at (excluding) the CR state itself.
  for (int ci = 0; ci < crCount; ++ci) {
    int crIdx = crs[ci].index;
    const auto& cr = out.states[crIdx];
    auto& info = crs[ci];

    FixedBitset<(MaxOutputStates > 0 ? MaxOutputStates : 1)> visited;
    int worklist[MaxOutputStates > 0 ? MaxOutputStates : 1] = {};
    int wlCount = 0;

    if (cr.alt >= 0 && cr.alt != crIdx) {
      worklist[wlCount++] = cr.alt;
    }

    while (wlCount > 0) {
      int s = worklist[--wlCount];
      if (s < 0 || s >= out.state_count || s == crIdx || visited.test(s)) {
        continue;
      }
      visited.set(s);

      if (info.bodyCount < kMaxBodySize) {
        info.bodyStates[info.bodyCount++] = s;
      }

      const auto& state = out.states[s];
      if (state.next >= 0 && state.next != crIdx && !visited.test(state.next)) {
        worklist[wlCount++] = state.next;
      }
      if (state.alt >= 0 && state.alt != crIdx && !visited.test(state.alt)) {
        worklist[wlCount++] = state.alt;
      }
    }

    for (int j = 0; j < info.bodyCount; ++j) {
      if (info.bodyStates[j] == cr.alt) {
        info.bodyEntryOffset = j;
        break;
      }
    }

    int effectiveRepeat = (cr.max_repeat > 0) ? cr.max_repeat : cr.min_repeat;
    info.cost = effectiveRepeat * info.bodyCount;
  }

  // Detect direct nesting: CR_j is nested inside CR_i if CR_j's index
  // appears in CR_i's body.
  for (int ci = 0; ci < crCount; ++ci) {
    for (int j = 0; j < crs[ci].bodyCount; ++j) {
      int bodyState = crs[ci].bodyStates[j];
      for (int ck = 0; ck < crCount; ++ck) {
        if (ck != ci && crs[ck].index == bodyState) {
          crs[ck].nestParent = ci;
        }
      }
    }
  }

  // Determine which CRs to unroll.
  // Step 1: candidates by small-repeat threshold.
  for (int ci = 0; ci < crCount; ++ci) {
    if (crs[ci].cost <= kSmallRepeatThreshold && crs[ci].cost > 0) {
      crs[ci].shouldUnroll = true;
    }
  }

  // Step 2: for nested pairs where both are candidates, keep only cheaper.
  for (int ci = 0; ci < crCount; ++ci) {
    int pi = crs[ci].nestParent;
    if (pi < 0 || !crs[ci].shouldUnroll || !crs[pi].shouldUnroll) {
      continue;
    }
    if (crs[ci].cost <= crs[pi].cost) {
      crs[pi].shouldUnroll = false;
    } else {
      crs[ci].shouldUnroll = false;
    }
  }

  // Step 3: for nested pairs where neither is candidate, force the cheaper.
  for (int ci = 0; ci < crCount; ++ci) {
    int pi = crs[ci].nestParent;
    if (pi < 0 || crs[ci].shouldUnroll || crs[pi].shouldUnroll) {
      continue;
    }
    int cheaperIdx = (crs[ci].cost <= crs[pi].cost) ? ci : pi;
    crs[cheaperIdx].shouldUnroll = true;
  }

  // Unroll qualifying CRs. For each CR being unrolled, we build the
  // chain of clones right-to-left so each clone knows its back-target
  // at creation time.
  struct Unroller {
    NfaProgram<MaxOutputStates>& out;
    const CRInfo& info;
    int crIdx;
    bool possessive = false;

    constexpr int findBodyOffset(int idx) const noexcept {
      for (int j = 0; j < info.bodyCount; ++j) {
        if (info.bodyStates[j] == idx) {
          return j;
        }
      }
      return -1;
    }

    // Clone the body once, remapping internal references. Edges that
    // pointed back to the CR state are redirected to backTarget.
    constexpr int cloneBody(int backTarget) noexcept {
      int bodySize = info.bodyCount;
      if (out.state_count + bodySize > MaxOutputStates) {
        return -1;
      }
      int cloneBase = out.state_count;

      for (int j = 0; j < bodySize; ++j) {
        out.states[cloneBase + j] = out.states[info.bodyStates[j]];
      }
      out.state_count += bodySize;

      for (int j = 0; j < bodySize; ++j) {
        auto& s = out.states[cloneBase + j];
        if (s.next >= 0) {
          if (s.next == crIdx) {
            s.next = backTarget;
          } else {
            int off = findBodyOffset(s.next);
            if (off >= 0) {
              s.next = cloneBase + off;
            }
          }
        }
        if (s.alt >= 0) {
          if (s.alt == crIdx) {
            s.alt = backTarget;
          } else {
            int off = findBodyOffset(s.alt);
            if (off >= 0) {
              s.alt = cloneBase + off;
            }
          }
        }
      }

      return cloneBase + info.bodyEntryOffset;
    }

    constexpr int addSplit(int nextTarget, int altTarget) noexcept {
      if (out.state_count >= MaxOutputStates) {
        return -1;
      }
      NfaState s;
      s.kind = NfaStateKind::Split;
      s.next = nextTarget;
      s.alt = altTarget;
      s.possessive = possessive;
      out.states[out.state_count] = s;
      return out.state_count++;
    }

    // Build the unrolled chain and convert the CR to a Split.
    // Returns true on success, false on overflow.
    constexpr bool unroll() noexcept {
      const auto& cr = out.states[crIdx];
      int minR = cr.min_repeat;
      int maxR = cr.max_repeat;
      bool greedy = cr.greedy;
      int exitTarget = cr.next;

      if (info.bodyCount == 0 || info.bodyEntryOffset < 0) {
        return false;
      }

      bool unbounded = (maxR < 0);
      int numMandatory = unbounded ? (minR > 0 ? minR - 1 : 0) : minR;
      int numOptional = unbounded ? 0 : (maxR - minR);
      int chainEntry = -1;

      if (unbounded) {
        // +-style loop: Split -> body -> back to Split, or exit.
        int splitIdx = addSplit(-1, -1);
        if (splitIdx < 0) {
          return false;
        }

        int loopEntry = cloneBody(splitIdx);
        if (loopEntry < 0) {
          return false;
        }

        if (greedy) {
          out.states[splitIdx].next = loopEntry;
          out.states[splitIdx].alt = exitTarget;
        } else {
          out.states[splitIdx].next = exitTarget;
          out.states[splitIdx].alt = loopEntry;
        }

        chainEntry = loopEntry;

        for (int k = numMandatory - 1; k >= 0; --k) {
          int entry = cloneBody(chainEntry);
          if (entry < 0) {
            return false;
          }
          chainEntry = entry;
        }
      } else {
        // Bounded (includes exact when numOptional == 0).
        chainEntry = exitTarget;

        // Optional copies right-to-left, each preceded by a Split.
        for (int k = numOptional - 1; k >= 0; --k) {
          int optEntry = cloneBody(chainEntry);
          if (optEntry < 0) {
            return false;
          }

          int splitIdx = greedy
              ? addSplit(optEntry, chainEntry)
              : addSplit(chainEntry, optEntry);
          if (splitIdx < 0) {
            return false;
          }

          chainEntry = splitIdx;
        }

        // Mandatory copies right-to-left.
        for (int k = numMandatory - 1; k >= 0; --k) {
          int entry = cloneBody(chainEntry);
          if (entry < 0) {
            return false;
          }
          chainEntry = entry;
        }
      }

      // Repurpose CR as a pass-through Split.
      out.states[crIdx].kind = NfaStateKind::Split;
      out.states[crIdx].next = chainEntry;
      out.states[crIdx].alt = -1;
      out.states[crIdx].counter_id = -1;
      out.states[crIdx].min_repeat = 0;
      out.states[crIdx].max_repeat = -1;
      return true;
    }
  };

  for (int ci = 0; ci < crCount; ++ci) {
    if (!crs[ci].shouldUnroll) {
      continue;
    }
    Unroller u{
        out, crs[ci], crs[ci].index, out.states[crs[ci].index].possessive};
    if (!u.unroll()) {
      crs[ci].shouldUnroll = false;
    }
  }

  // Compact surviving CountedRepeat counter_ids to 0..n-1.
  int nextCounterId = 0;
  for (int i = 0; i < out.state_count; ++i) {
    if (out.states[i].kind == NfaStateKind::CountedRepeat) {
      out.states[i].counter_id = nextCounterId++;
    }
  }
  out.num_counters = nextCounterId;

  return out;
}

} // namespace detail
} // namespace regex
} // namespace folly
