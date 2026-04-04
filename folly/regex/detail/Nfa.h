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

namespace folly {
namespace regex {
namespace detail {

// Constexpr-safe fixed-size bitset using uint64_t words.
// std::bitset is not constexpr until C++23; this provides the subset
// of operations needed by the regex engine for NFA state sets, thread
// dedup, and counter tracking.
template <int MaxBits>
struct FixedBitset {
  static constexpr int kWords = (MaxBits + 63) / 64;
  uint64_t words[kWords > 0 ? kWords : 1] = {};

  constexpr void set(int i) noexcept {
    words[i / 64] |= uint64_t{1} << (i % 64);
  }

  constexpr void clear(int i) noexcept {
    words[i / 64] &= ~(uint64_t{1} << (i % 64));
  }

  constexpr bool test(int i) const noexcept {
    return (words[i / 64] >> (i % 64)) & 1;
  }

  constexpr void clearAll() noexcept {
    for (int i = 0; i < kWords; ++i) {
      words[i] = 0;
    }
  }

  constexpr bool empty() const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if (words[i] != 0) {
        return false;
      }
    }
    return true;
  }

  constexpr bool operator==(const FixedBitset& o) const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if (words[i] != o.words[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr bool operator!=(const FixedBitset& o) const noexcept {
    return !(*this == o);
  }
};

enum class NfaStateKind : uint8_t {
  Match,
  Split,
  Literal,
  AnyChar,
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

template <
    int MaxStates,
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr NfaProgram<MaxStates> buildNfa(
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>&
        ast) noexcept {
  NfaProgram<MaxStates> prog;

  struct Builder {
    NfaProgram<MaxStates>& prog;
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast;

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
          NfaState s;
          s.kind = NfaStateKind::Literal;
          s.ch = node.ch;
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
        case NodeKind::AnyChar: {
          NfaState s;
          s.kind = NfaStateKind::AnyChar;
          s.next = -1;
          int idx = prog.addState(s);
          return {idx, idx};
        }
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
            splitIdx = prog.addState(newSplit);

            moreChild = ast.nodes[moreChild].child_end;
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
        case NodeKind::NegWordBoundary: {
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
    // AnyChar/AnyByte match everything — no boundaries needed
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
        case NfaStateKind::AnyChar:
        case NfaStateKind::AnyByte:
          mask = (part.intervalCount() >= 64)
              ? ~uint64_t{0}
              : (uint64_t{1} << part.intervalCount()) - 1;
          break;
        case NfaStateKind::CharClass: {
          const auto& cc = ast.char_classes[s.char_class_index];
          if (cc.negated) {
            // Start with all intervals set, then clear matched ones
            mask = (part.intervalCount() >= 64)
                ? ~uint64_t{0}
                : (uint64_t{1} << part.intervalCount()) - 1;
            for (int r = 0; r < cc.range_count; ++r) {
              auto lo = ast.ranges[cc.range_offset + r].lo;
              auto hi = ast.ranges[cc.range_offset + r].hi;
              for (int iv = part.charToInterval(lo); iv < part.intervalCount();
                   ++iv) {
                auto rep = part.representative(iv);
                if (rep > hi) {
                  break;
                }
                mask &= ~(uint64_t{1} << iv);
              }
            }
          } else {
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

} // namespace detail
} // namespace regex
} // namespace folly
