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

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>

namespace folly {
namespace regex {

enum class Flags : unsigned;

namespace detail {

struct GroupSpan {
  std::size_t offset = std::string_view::npos;
  std::size_t length = 0;
};

template <int NumGroups>
struct MatchState {
  std::array<GroupSpan, NumGroups + 1> groups = {};
};

enum class MatchStatus {
  Matched,
  NoMatch,
  BudgetExhausted,
};

template <int NumGroups>
struct MatchOutcome {
  MatchStatus status = MatchStatus::NoMatch;
  MatchState<NumGroups> state;
};

constexpr bool hasAnchorBegin(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Anchor &&
      (node.anchor == AnchorKind::Begin ||
       node.anchor == AnchorKind::StartOfString)) {
    return true;
  }
  if (node.kind == NodeKind::Sequence) {
    int child = node.child_begin;
    if (child >= 0) {
      return hasAnchorBegin(ast, child);
    }
  }
  if (node.kind == NodeKind::Group) {
    return hasAnchorBegin(ast, node.child_begin);
  }
  return false;
}

// Check if the AST contains anchors that require per-position evaluation
// instead of the single-pass unanchored DFA search. This includes:
//   - BeginLine/EndLine (multiline ^/$)
//   - EndOfStringOrNewline (\Z)
//   - Begin/StartOfString (^/\A) when embedded in alternation branches
//     rather than anchoring the entire pattern (hasAnchorBegin handles the
//     root case; the single-pass unanchored DFA cannot replay the anchored
//     start state so we must fall back to per-position matching).
constexpr bool hasLineOrEndAnchors(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Anchor &&
      (node.anchor == AnchorKind::BeginLine ||
       node.anchor == AnchorKind::EndLine ||
       node.anchor == AnchorKind::EndOfStringOrNewline ||
       node.anchor == AnchorKind::Begin ||
       node.anchor == AnchorKind::StartOfString)) {
    return true;
  }
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int child = node.child_begin;
    while (child >= 0) {
      if (hasLineOrEndAnchors(ast, child)) {
        return true;
      }
      child = ast.nodes[child].child_end;
    }
  }
  if (node.kind == NodeKind::Group || node.kind == NodeKind::Repeat) {
    return hasLineOrEndAnchors(ast, node.child_begin);
  }
  return false;
}

// Compile-time trait:

// Compile-time trait: can the node at `Idx` in `Ast` be tested with a single
// character check? When true, matchRepeat uses a tight iterative loop instead
// of recursive CPS — the key optimization for quantified character classes.
//
// For CharClass nodes with 1-2 ranges, uses direct inline comparisons.
// For CharClass nodes with ≥3 ranges, uses the compact bitmap from the AST
// (O(1) word lookup). Falls back to charClassTestAt for edge cases.
template <const auto& Ast, int Idx>
struct CharTestTrait {
  static constexpr bool available = [] {
    if constexpr (Idx < 0) {
      return false;
    } else {
      constexpr auto kind = Ast.nodes[Idx].kind;
      if constexpr (kind == NodeKind::Literal) {
        return Ast.nodes[Idx].literal.size() == 1;
      }
      return kind == NodeKind::AnyByte || kind == NodeKind::CharClass;
    }
  }();

 private:
  static constexpr int kCCIdx = [] {
    if constexpr (Idx >= 0 && Ast.nodes[Idx].kind == NodeKind::CharClass) {
      return Ast.nodes[Idx].char_class_index;
    } else {
      return -1;
    }
  }();

 public:
  static bool testChar(char c) noexcept {
    if constexpr (Idx < 0) {
      return false;
    } else {
      constexpr auto& n = Ast.nodes[Idx];
      if constexpr (n.kind == NodeKind::Literal) {
        return c == n.literal[0];
      } else if constexpr (n.kind == NodeKind::AnyByte) {
        return true;
      } else if constexpr (n.kind == NodeKind::CharClass) {
        constexpr auto& cc = Ast.char_classes[kCCIdx];
        if constexpr (cc.range_count == 1) {
          constexpr auto r = Ast.ranges[cc.range_offset];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r.lo && uc <= r.hi);
        } else if constexpr (cc.range_count == 2) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi);
        } else if constexpr (cc.range_count == 3) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          constexpr auto r2 = Ast.ranges[cc.range_offset + 2];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi) ||
              (uc >= r2.lo && uc <= r2.hi);
        } else if constexpr (cc.range_count == 4) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          constexpr auto r2 = Ast.ranges[cc.range_offset + 2];
          constexpr auto r3 = Ast.ranges[cc.range_offset + 3];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi) ||
              (uc >= r2.lo && uc <= r2.hi) || (uc >= r3.lo && uc <= r3.hi);
        } else {
          return Ast.charClassTestAt(kCCIdx, c);
        }
      } else {
        return false;
      }
    }
  }
};

// Compile-time trait: can the Repeat node at `Idx` use a compound tight loop?
// True when the Repeat wraps a (possibly grouped) Sequence consisting of:
//   - One or more leading CharTestTrait-eligible nodes (fixed-length prefix)
//   - A trailing Repeat over a CharTestTrait-eligible node (variable suffix)
// When available, matchRepeat uses an iterative loop that matches the
// fixed prefix then scans the variable suffix, avoiding CPS recursion.
// This handles patterns like (/[a-zA-Z0-9_.\\-]+)* where the outer repeat
// wraps a Sequence(Literal('/'), Repeat(+, CharClass)).
template <const auto& Ast, int Idx>
struct CompoundRepeatTrait {
  // Unwrap groups to find the inner content
  static constexpr int unwrapIdx = [] {
    if constexpr (Idx < 0) {
      return -1;
    } else {
      constexpr auto& n = Ast.nodes[Idx];
      if constexpr (n.kind != NodeKind::Repeat) {
        return -1;
      } else {
        int inner = n.child_begin;
        if (inner < 0) {
          return -1;
        }
        // Unwrap group (capturing or non-capturing)
        if (Ast.nodes[inner].kind == NodeKind::Group) {
          inner = Ast.nodes[inner].child_begin;
        }
        return inner;
      }
    }
  }();

  // Count prefix children (all CharTest-eligible before the trailing Repeat)
  static constexpr auto analyzeSequence = [] {
    struct Result {
      bool valid = false;
      int prefixCount = 0;
      int prefixIndices[8] = {};
      int tailRepeatIdx = -1;
      int tailInnerIdx = -1;
    };
    Result r;
    if constexpr (unwrapIdx < 0) {
      return r;
    } else {
      if (Ast.nodes[unwrapIdx].kind != NodeKind::Sequence) {
        return r;
      }

      // Collect children
      int children[16] = {};
      int count = 0;
      int child = Ast.nodes[unwrapIdx].child_begin;
      while (child >= 0 && count < 16) {
        children[count++] = child;
        child = Ast.nodes[child].child_end;
      }

      if (count < 2) {
        return r;
      }

      // Last child must be a Repeat with CharTestTrait-eligible inner
      int lastIdx = children[count - 1];
      if (Ast.nodes[lastIdx].kind != NodeKind::Repeat) {
        return r;
      }
      int tailInner = Ast.nodes[lastIdx].child_begin;
      if (tailInner < 0) {
        return r;
      }
      auto tailKind = Ast.nodes[tailInner].kind;
      if (tailKind != NodeKind::Literal && tailKind != NodeKind::CharClass &&
          tailKind != NodeKind::AnyChar && tailKind != NodeKind::AnyByte) {
        return r;
      }

      // All preceding children must be CharTestTrait-eligible
      for (int i = 0; i < count - 1; ++i) {
        auto kind = Ast.nodes[children[i]].kind;
        if (kind != NodeKind::Literal && kind != NodeKind::CharClass &&
            kind != NodeKind::AnyChar && kind != NodeKind::AnyByte) {
          return r;
        }
        if (i < 8) {
          r.prefixIndices[i] = children[i];
        } else {
          return r;
        }
      }

      r.valid = true;
      r.prefixCount = count - 1;
      r.tailRepeatIdx = lastIdx;
      r.tailInnerIdx = tailInner;
      return r;
    }
  }();

 public:
  static constexpr bool available = analyzeSequence.valid;
};

constexpr FirstCharFilter extractFirstCharFilter(
    const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return {.accepts_all = true};
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      FirstCharFilter f;
      f.accepts_all = false;
      f.addChar(static_cast<unsigned char>(node.literal[0]));
      return f;
    }
    case NodeKind::CharClass: {
      const auto& cc = ast.char_classes[node.char_class_index];
      if (cc.range_count > FirstCharFilter::kMaxRanges) {
        return {.accepts_all = true};
      }
      FirstCharFilter f;
      f.accepts_all = false;
      for (int i = 0; i < cc.range_count; ++i) {
        f.ranges[i] = ast.ranges[cc.range_offset + i];
      }
      f.range_count = cc.range_count;
      return f;
    }
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
      return {.accepts_all = true};
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return {.accepts_all = true};
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return {.accepts_all = true};
    case NodeKind::Backref:
      return {.accepts_all = true};
    case NodeKind::Group:
      return extractFirstCharFilter(ast, node.child_begin);
    case NodeKind::Sequence: {
      int child = node.child_begin;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f = extractFirstCharFilter(ast, child);
      if (f.accepts_all && ast.nodes[child].kind == NodeKind::Anchor) {
        int next = ast.nodes[child].child_end;
        if (next >= 0) {
          return extractFirstCharFilter(ast, next);
        }
      }
      return f;
    }
    case NodeKind::Alternation: {
      int child = node.child_begin;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f = extractFirstCharFilter(ast, child);
      if (f.accepts_all) {
        return f;
      }
      int next = ast.nodes[child].child_end;
      while (next >= 0) {
        auto alt = extractFirstCharFilter(ast, next);
        if (alt.accepts_all) {
          return alt;
        }
        f.mergeFrom(alt.ranges, alt.range_count);
        next = ast.nodes[next].child_end;
      }
      return f;
    }
    case NodeKind::Repeat: {
      if (node.min_repeat > 0) {
        return extractFirstCharFilter(ast, node.child_begin);
      }
      return {.accepts_all = true};
    }
  }
  return {.accepts_all = true};
}

struct RequiredLiteral {
  char ch = 0;
  bool found = false;
};

constexpr RequiredLiteral extractRequiredLiteral(
    const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return {};
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal:
      return {node.literal[0], true};
    case NodeKind::Sequence: {
      int child = node.child_begin;
      while (child >= 0) {
        auto r = extractRequiredLiteral(ast, child);
        if (r.found) {
          return r;
        }
        child = ast.nodes[child].child_end;
      }
      return {};
    }
    case NodeKind::Group:
      return extractRequiredLiteral(ast, node.child_begin);
    case NodeKind::Repeat:
      if (node.min_repeat > 0) {
        return extractRequiredLiteral(ast, node.child_begin);
      }
      return {};
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
      return {};
  }
  return {};
}

// Extract a single first character from a FirstCharFilter when the bitmap
// has exactly one bit set.  Used to enable memchr skip in the non-memchr
// search path (when the required-literal memchr condition doesn't trigger
// because the required literal IS the first char).
constexpr RequiredLiteral extractSingleFirstChar(
    const FirstCharFilter& filter) noexcept {
  if (filter.accepts_all) {
    return {};
  }
  if (filter.range_count == 1 && filter.ranges[0].lo == filter.ranges[0].hi) {
    return {static_cast<char>(filter.ranges[0].lo), true};
  }
  return {};
}

// Find the first consuming (non-zero-width) node index from the root.
// Skips Sequence containers, non-capturing Groups, and Empty nodes.
// Used to detect if a Repeat is the first consuming element, enabling
// the sliding window optimization in matchRepeat.
constexpr int findFirstConsumingNode(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return -1;
  }
  const auto& n = ast.nodes[nodeIdx];
  if (n.kind == NodeKind::Sequence) {
    int child = n.child_begin;
    while (child >= 0) {
      int result = findFirstConsumingNode(ast, child);
      if (result >= 0) {
        return result;
      }
      child = ast.nodes[child].child_end;
    }
    return -1;
  }
  if (n.kind == NodeKind::Group && !n.capturing) {
    return findFirstConsumingNode(ast, n.child_begin);
  }
  if (n.kind == NodeKind::Empty) {
    return -1;
  }
  return nodeIdx;
}

template <
    const auto& Ast,
    Flags F,
    int NodeIdx,
    bool TrackCaptures,
    int NumGroups,
    bool AllowSlide = false>
struct BacktrackExecutor {
  static constexpr auto& node = Ast.nodes[NodeIdx];

  template <typename Cont>
  static bool match(
      std::string_view input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (node.kind == NodeKind::Empty) {
      return cont(pos);
    } else if constexpr (node.kind == NodeKind::Literal) {
      constexpr auto lit = node.literal;
      if (pos + lit.size() <= input.size() &&
          input.substr(pos, lit.size()) == lit) {
        return cont(pos + lit.size());
      }
      return false;
    } else if constexpr (node.kind == NodeKind::AnyByte) {
      if (pos < input.size()) {
        return cont(pos + 1);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::CharClass) {
      if (pos < input.size() &&
          Ast.charClassTestAt(node.char_class_index, input[pos])) {
        return cont(pos + 1);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Anchor) {
      if constexpr (node.anchor == AnchorKind::Begin) {
        if (pos == 0) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::End) {
        if (pos == input.size()) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::StartOfString) {
        if (pos == 0) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndOfString) {
        if (pos == input.size()) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndOfStringOrNewline) {
        if (pos == input.size() ||
            (pos + 1 == input.size() && input[pos] == '\n')) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::BeginLine) {
        if (pos == 0 || input[pos - 1] == '\n') {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndLine) {
        if (pos == input.size() || input[pos] == '\n') {
          return cont(pos);
        }
        return false;
      } else {
        return false;
      }
    } else if constexpr (node.kind == NodeKind::WordBoundary) {
      bool prevWord = pos > 0 && isWordChar(input[pos - 1]);
      bool currWord = pos < input.size() && isWordChar(input[pos]);
      if (prevWord != currWord) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::NegWordBoundary) {
      bool prevWord = pos > 0 && isWordChar(input[pos - 1]);
      bool currWord = pos < input.size() && isWordChar(input[pos]);
      if (prevWord == currWord) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Group) {
      if constexpr (TrackCaptures && node.capturing) {
        auto saved = state.groups[node.group_id];
        state.groups[node.group_id].offset = pos;
        bool result = BacktrackExecutor<
            Ast,
            F,
            node.child_begin,
            TrackCaptures,
            NumGroups,
            AllowSlide>::
            match(input, pos, state, budget, [&](std::size_t endPos) {
              state.groups[node.group_id].length = endPos - pos;
              return cont(endPos);
            });
        if (!result) {
          state.groups[node.group_id] = saved;
        }
        return result;
      } else {
        return BacktrackExecutor<
            Ast,
            F,
            node.child_begin,
            TrackCaptures,
            NumGroups,
            AllowSlide>::match(input, pos, state, budget, cont);
      }
    } else if constexpr (node.kind == NodeKind::Sequence) {
      return matchSequence<node.child_begin>(input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Alternation) {
      constexpr auto altFilter = extractFirstCharFilter(Ast, NodeIdx);
      if constexpr (!altFilter.accepts_all) {
        if (pos >= input.size() || !altFilter.test(input[pos])) {
          return false;
        }
      }
      if constexpr (node.discriminator_offset >= 0) {
        if (pos + node.discriminator_offset >= input.size()) {
          return false;
        }
        return matchAlternationDispatched<
            node.child_begin,
            node.discriminator_offset>(input, pos, state, budget, cont);
      }
      return matchAlternation<node.child_begin>(
          input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Repeat) {
      return matchRepeat(input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Lookahead) {
      // Positive lookahead: match inner without consuming input
      auto savedState = state;
      std::size_t savedPos = pos;
      bool innerMatched = BacktrackExecutor<
          Ast,
          F,
          node.child_begin,
          TrackCaptures,
          NumGroups,
          AllowSlide>::match(input, pos, state, budget, [](std::size_t) {
        return true;
      });
      state = savedState;
      if (innerMatched) {
        return cont(savedPos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::NegLookahead) {
      // Negative lookahead: fail if inner matches
      auto savedState = state;
      std::size_t savedPos = pos;
      bool innerMatched = BacktrackExecutor<
          Ast,
          F,
          node.child_begin,
          TrackCaptures,
          NumGroups,
          AllowSlide>::match(input, pos, state, budget, [](std::size_t) {
        return true;
      });
      state = savedState;
      if (!innerMatched) {
        return cont(savedPos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Lookbehind) {
      // Positive lookbehind: match inner ending at current position
      constexpr int width = node.min_repeat;
      if (pos < static_cast<std::size_t>(width)) {
        return false;
      }
      auto savedState = state;
      bool innerMatched = BacktrackExecutor<
          Ast,
          F,
          node.child_begin,
          TrackCaptures,
          NumGroups,
          AllowSlide>::
          match(input, pos - width, state, budget, [pos](std::size_t endPos) {
            return endPos == pos;
          });
      if (innerMatched) {
        return cont(pos);
      }
      state = savedState;
      return false;
    } else if constexpr (node.kind == NodeKind::NegLookbehind) {
      // Negative lookbehind: fail if inner matches ending at current position
      constexpr int width = node.min_repeat;
      if (pos < static_cast<std::size_t>(width)) {
        return cont(pos);
      }
      auto savedState = state;
      bool innerMatched = BacktrackExecutor<
          Ast,
          F,
          node.child_begin,
          TrackCaptures,
          NumGroups,
          AllowSlide>::
          match(input, pos - width, state, budget, [pos](std::size_t endPos) {
            return endPos == pos;
          });
      state = savedState;
      if (!innerMatched) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Backref) {
      // Backreference: match same text as captured group
      if constexpr (TrackCaptures) {
        const auto& captured = state.groups[node.group_id];
        if (captured.offset == std::string_view::npos) {
          return false;
        }
        std::size_t len = captured.length;
        if (pos + len > input.size()) {
          return false;
        }
        for (std::size_t i = 0; i < len; ++i) {
          if (input[pos + i] != input[captured.offset + i]) {
            return false;
          }
        }
        return cont(pos + len);
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  template <int ChildIdx, typename Cont>
  static bool matchSequence(
      std::string_view input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (ChildIdx < 0) {
      return cont(pos);
    } else {
      constexpr int nextChild = Ast.nodes[ChildIdx].child_end;
      return BacktrackExecutor<
          Ast,
          F,
          ChildIdx,
          TrackCaptures,
          NumGroups,
          AllowSlide>::
          match(input, pos, state, budget, [&](std::size_t nextPos) {
            return matchSequence<nextChild>(
                input, nextPos, state, budget, cont);
          });
    }
  }

  template <int AltIdx, typename Cont>
  static bool matchAlternation(
      std::string_view input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (AltIdx < 0) {
      return false;
    } else {
      if (budget == 0) {
        return false;
      }
      --budget;
      constexpr int nextAlt = Ast.nodes[AltIdx].child_end;
      if (BacktrackExecutor<
              Ast,
              F,
              AltIdx,
              TrackCaptures,
              NumGroups,
              AllowSlide>::match(input, pos, state, budget, cont)) {
        return true;
      }
      return matchAlternation<nextAlt>(input, pos, state, budget, cont);
    }
  }

  template <int AltIdx, int DiscOffset, typename Cont>
  static bool matchAlternationDispatched(
      std::string_view input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (AltIdx < 0) {
      return false;
    } else {
      if (budget == 0) {
        return false;
      }
      --budget;
      constexpr int nextAlt = Ast.nodes[AltIdx].child_end;
      constexpr auto charInfo = resolveCharAtOffset(Ast, AltIdx, DiscOffset);
      bool tryBranch = false;
      if constexpr (charInfo.valid) {
        constexpr auto& dn = Ast.nodes[charInfo.nodeIdx];
        if constexpr (dn.kind == NodeKind::Literal) {
          tryBranch =
              (static_cast<unsigned char>(input[pos + DiscOffset]) ==
               static_cast<unsigned char>(dn.literal[charInfo.charOffset]));
        } else if constexpr (dn.kind == NodeKind::CharClass) {
          tryBranch =
              Ast.charClassTestAt(dn.char_class_index, input[pos + DiscOffset]);
        }
      } else {
        tryBranch = true;
      }
      if (tryBranch) {
        if (BacktrackExecutor<
                Ast,
                F,
                AltIdx,
                TrackCaptures,
                NumGroups,
                AllowSlide>::match(input, pos, state, budget, cont)) {
          return true;
        }
      }
      return matchAlternationDispatched<nextAlt, DiscOffset>(
          input, pos, state, budget, cont);
    }
  }

  template <const auto& Info, int PrefixIdx>
  static bool matchCompoundPrefix(
      std::string_view input, std::size_t& pos) noexcept {
    if constexpr (PrefixIdx >= Info.prefixCount) {
      return true;
    } else {
      constexpr int nodeIdx = Info.prefixIndices[PrefixIdx];
      if (pos >= input.size() ||
          !CharTestTrait<Ast, nodeIdx>::testChar(input[pos])) {
        return false;
      }
      ++pos;
      return matchCompoundPrefix<Info, PrefixIdx + 1>(input, pos);
    }
  }

  template <typename Cont>
  static bool matchRepeat(
      std::string_view input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    constexpr int innerIdx = node.child_begin;
    constexpr int minR = node.min_repeat;
    constexpr int maxR = node.max_repeat;

    // Optimization: when the inner expression is a simple character test,
    // use a tight iterative scan loop instead of recursive CPS.
    if constexpr (CharTestTrait<Ast, innerIdx>::available) {
      if constexpr (node.possessive) {
        // Possessive: consume maximum, never backtrack
        std::size_t start = pos;
        std::size_t count = 0;
        std::size_t limit =
            maxR >= 0 ? static_cast<std::size_t>(maxR) : input.size() - pos;
        while (count < limit && pos < input.size() &&
               CharTestTrait<Ast, innerIdx>::testChar(input[pos])) {
          ++pos;
          ++count;
        }
        if (static_cast<int>(count) >= minR) {
          if (cont(pos)) {
            return true;
          }
        }
        // Sliding window for possessive root repeat: no backtracking
        // within each position, just try cont at the possessive count.
        constexpr bool isRootRepeat =
            AllowSlide && (findFirstConsumingNode(Ast, Ast.root) == NodeIdx);
        if constexpr (isRootRepeat) {
          std::size_t totalScanned = count;
          std::size_t scanEnd = pos;
          if (totalScanned > static_cast<std::size_t>(minR) && minR > 0) {
            if (totalScanned == limit && maxR >= 0) {
              while (scanEnd < input.size() &&
                     CharTestTrait<Ast, innerIdx>::testChar(input[scanEnd])) {
                ++scanEnd;
              }
            }
            for (std::size_t offset = 1;
                 start + offset + static_cast<std::size_t>(minR) <= scanEnd;
                 ++offset) {
              std::size_t newStart = start + offset;
              std::size_t available = scanEnd - newStart;
              std::size_t effectiveMax = maxR >= 0
                  ? std::min(available, static_cast<std::size_t>(maxR))
                  : available;
              if (static_cast<int>(effectiveMax) >= minR) {
                if (cont(newStart + effectiveMax)) {
                  if constexpr (TrackCaptures) {
                    state.groups[0].offset = newStart;
                  }
                  return true;
                }
              }
            }
          }
        }
        return false;
      } else if constexpr (node.greedy) {
        // Greedy: scan forward as far as possible, then backtrack
        std::size_t start = pos;
        std::size_t count = 0;
        std::size_t limit =
            maxR >= 0 ? static_cast<std::size_t>(maxR) : input.size() - pos;
        while (count < limit && pos < input.size() &&
               CharTestTrait<Ast, innerIdx>::testChar(input[pos])) {
          ++pos;
          ++count;
        }
        std::size_t totalScanned = count;
        std::size_t scanEnd = pos;
        // Backtrack from longest to shortest, trying continuation
        while (true) {
          if (static_cast<int>(count) >= minR) {
            if (cont(pos)) {
              return true;
            }
          }
          if (pos <= start) {
            break;
          }
          --pos;
          --count;
        }
        // Sliding window: when this repeat is the first consuming
        // element in the pattern, slide the start forward within the
        // already-scanned region instead of returning to the search loop.
        constexpr bool isRootRepeat =
            AllowSlide && (findFirstConsumingNode(Ast, Ast.root) == NodeIdx);
        if constexpr (isRootRepeat) {
          if (totalScanned > static_cast<std::size_t>(minR) && minR > 0) {
            // If the original scan was limit-capped (hit maxR), extend
            // to find the full region of verified characters.
            if (totalScanned == limit && maxR >= 0) {
              while (scanEnd < input.size() &&
                     CharTestTrait<Ast, innerIdx>::testChar(input[scanEnd])) {
                ++scanEnd;
              }
            }
            for (std::size_t offset = 1;
                 start + offset + static_cast<std::size_t>(minR) <= scanEnd;
                 ++offset) {
              std::size_t newStart = start + offset;
              std::size_t available = scanEnd - newStart;
              std::size_t effectiveMax = maxR >= 0
                  ? std::min(available, static_cast<std::size_t>(maxR))
                  : available;
              pos = newStart + effectiveMax;
              count = effectiveMax;
              while (true) {
                if (static_cast<int>(count) >= minR) {
                  if (cont(pos)) {
                    if constexpr (TrackCaptures) {
                      state.groups[0].offset = newStart;
                    }
                    return true;
                  }
                }
                if (count <= static_cast<std::size_t>(minR)) {
                  break;
                }
                --pos;
                --count;
              }
            }
          }
        }
        return false;
      } else {
        // Lazy: try continuation first with minimum matches, then extend
        std::size_t count = 0;
        // First match minimum required
        for (int i = 0; i < minR; ++i) {
          if (pos >= input.size() ||
              !CharTestTrait<Ast, innerIdx>::testChar(input[pos])) {
            return false;
          }
          ++pos;
          ++count;
        }
        // Try continuation at each position from min to max
        while (true) {
          if (cont(pos)) {
            return true;
          }
          if (maxR >= 0 && count >= static_cast<std::size_t>(maxR)) {
            break;
          }
          if (pos >= input.size() ||
              !CharTestTrait<Ast, innerIdx>::testChar(input[pos])) {
            break;
          }
          ++pos;
          ++count;
        }
        return false;
      }
    } else if constexpr (
        CompoundRepeatTrait<Ast, NodeIdx>::available && node.greedy) {
      using CRT = CompoundRepeatTrait<Ast, NodeIdx>;
      constexpr int tailMinR =
          Ast.nodes[CRT::analyzeSequence.tailRepeatIdx].min_repeat;
      constexpr int tailMaxR =
          Ast.nodes[CRT::analyzeSequence.tailRepeatIdx].max_repeat;

      constexpr int kMaxCompoundIters = 64;

      std::size_t positions[kMaxCompoundIters + 1];
      int iterCount = 0;
      positions[0] = pos;

      std::size_t limit =
          maxR >= 0 ? static_cast<std::size_t>(maxR) : kMaxCompoundIters;
      if (limit > static_cast<std::size_t>(kMaxCompoundIters)) {
        limit = kMaxCompoundIters;
      }

      while (iterCount < static_cast<int>(limit) && pos < input.size()) {
        std::size_t seqStart = pos;

        if (!matchCompoundPrefix<CRT::analyzeSequence, 0>(input, pos)) {
          pos = seqStart;
          break;
        }

        int tailCount = 0;
        while (pos < input.size() &&
               CharTestTrait<Ast, CRT::analyzeSequence.tailInnerIdx>::testChar(
                   input[pos])) {
          ++pos;
          ++tailCount;
          if (tailMaxR >= 0 && tailCount >= tailMaxR) {
            break;
          }
        }
        if (tailCount < tailMinR) {
          pos = seqStart;
          break;
        }

        ++iterCount;
        positions[iterCount] = pos;
      }

      for (int i = iterCount; i >= minR; --i) {
        if (cont(positions[i])) {
          return true;
        }
      }
      return false;
    } else {
      if constexpr (node.possessive) {
        return matchRepeatPossessive<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      } else if constexpr (node.greedy) {
        return matchRepeatGreedy<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      } else {
        return matchRepeatLazy<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      }
    }
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatGreedy(
      std::string_view input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if (budget == 0) {
      return false;
    }
    --budget;

    if constexpr (MaxR >= 0) {
      if (count >= MaxR) {
        if (count >= MinR) {
          return cont(pos);
        }
        return false;
      }
    }

    if (BacktrackExecutor<
            Ast,
            F,
            InnerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide>::
            match(input, pos, state, budget, [&](std::size_t nextPos) {
              if (nextPos == pos) {
                return false;
              }
              return matchRepeatGreedy<InnerIdx, MinR, MaxR>(
                  input, nextPos, count + 1, state, budget, cont);
            })) {
      return true;
    }

    if (count >= MinR) {
      return cont(pos);
    }
    return false;
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatLazy(
      std::string_view input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if (budget == 0) {
      return false;
    }
    --budget;

    if (count >= MinR) {
      if (cont(pos)) {
        return true;
      }
    }

    if constexpr (MaxR >= 0) {
      if (count >= MaxR) {
        return false;
      }
    }

    return BacktrackExecutor<
        Ast,
        F,
        InnerIdx,
        TrackCaptures,
        NumGroups,
        AllowSlide>::match(input, pos, state, budget, [&](std::size_t nextPos) {
      if (nextPos == pos) {
        return false;
      }
      return matchRepeatLazy<InnerIdx, MinR, MaxR>(
          input, nextPos, count + 1, state, budget, cont);
    });
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatPossessive(
      std::string_view input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if (budget == 0) {
      return false;
    }
    --budget;

    if constexpr (MaxR >= 0) {
      if (count >= MaxR) {
        if (count >= MinR) {
          return cont(pos);
        }
        return false;
      }
    }

    auto savedState = state;
    std::size_t matchedPos = pos;
    if (BacktrackExecutor<
            Ast,
            F,
            InnerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide>::
            match(input, pos, state, budget, [&](std::size_t nextPos) {
              matchedPos = nextPos;
              return true;
            })) {
      if (matchedPos != pos) {
        return matchRepeatPossessive<InnerIdx, MinR, MaxR>(
            input, matchedPos, count + 1, state, budget, cont);
      }
    }
    state = savedState;

    if (count >= MinR) {
      return cont(pos);
    }
    return false;
  }
};

template <
    const auto& Ast,
    Flags F,
    bool TrackCaptures,
    int NumGroups,
    bool UseBudget>
struct BacktrackRunner {
  static MatchOutcome<NumGroups> matchAnchored(
      std::string_view input) noexcept {
    MatchOutcome<NumGroups> outcome;
    MatchState<NumGroups> state;
    std::size_t budget = UseBudget
        ? input.size() * static_cast<std::size_t>(Ast.node_count) * 8 + 1024
        : static_cast<std::size_t>(-1);

    std::size_t budgetCopy = budget;

    bool matched =
        BacktrackExecutor<Ast, F, Ast.root, TrackCaptures, NumGroups>::match(
            input, 0, state, budgetCopy, [&](std::size_t endPos) {
              return endPos == input.size();
            });

    if (matched) {
      outcome.status = MatchStatus::Matched;
      if constexpr (TrackCaptures) {
        state.groups[0] = {0, input.size()};
      }
      outcome.state = state;
    } else if (UseBudget && budgetCopy == 0) {
      outcome.status = MatchStatus::BudgetExhausted;
    } else {
      outcome.status = MatchStatus::NoMatch;
    }
    return outcome;
  }

  struct SearchBoolResult {
    bool matched = false;
    bool budgetExhausted = false;
  };

  // Unified search: when BoolOnly=true, returns just matched/budgetExhausted
  // with minimal state (MatchState<0>). When BoolOnly=false, returns a full
  // MatchOutcome with captures (groups[0] set to match span).
  template <bool BoolOnly>
  static auto search(std::string_view input) noexcept {
    static constexpr int kStateGroups =
        (BoolOnly || !TrackCaptures) ? 0 : NumGroups;

    constexpr bool anchored = hasAnchorBegin(Ast, Ast.root);
    constexpr auto kFilter = extractFirstCharFilter(Ast, Ast.root);
    constexpr auto kReqLit = extractRequiredLiteral(Ast, Ast.root);
    std::size_t maxStart = anchored ? 0 : input.size();

    std::size_t totalBudget = UseBudget
        ? input.size() * static_cast<std::size_t>(Ast.node_count) * 8 + 1024
        : static_cast<std::size_t>(-1);

    // Use memchr path when we have a required literal that is more selective
    // than the first-char filter. This is the case when:
    // - The first-char filter accepts all chars (accepts_all), OR
    // - The required literal char is NOT in the first-char filter's bitmap
    //   (meaning the literal appears AFTER the first char, like @ in \w+@\w+)
    constexpr bool useMemchr = kReqLit.found && !anchored &&
        (kFilter.accepts_all ||
         (!kFilter.accepts_all && !kFilter.test(kReqLit.ch)));

    auto makeBudgetExhausted = []() {
      if constexpr (BoolOnly) {
        return SearchBoolResult{false, true};
      } else {
        MatchOutcome<NumGroups> outcome;
        outcome.status = MatchStatus::BudgetExhausted;
        return outcome;
      }
    };

    auto makeNoMatch = []() {
      if constexpr (BoolOnly) {
        return SearchBoolResult{false, false};
      } else {
        MatchOutcome<NumGroups> outcome;
        outcome.status = MatchStatus::NoMatch;
        return outcome;
      }
    };

    constexpr bool kBoolOnlyCaptures = BoolOnly ? false : TrackCaptures;

    auto tryMatch = [&](std::size_t start) {
      MatchState<kStateGroups> state;
      std::size_t budgetCopy = totalBudget;
      [[maybe_unused]] std::size_t matchEnd = std::string_view::npos;

      bool matched = BacktrackExecutor<
          Ast,
          F,
          Ast.root,
          kBoolOnlyCaptures,
          kStateGroups,
          true>::
          match(input, start, state, budgetCopy, [&](std::size_t endPos) {
            if constexpr (!BoolOnly) {
              matchEnd = endPos;
            }
            return true;
          });

      if (matched) {
        if constexpr (BoolOnly) {
          return SearchBoolResult{true, false};
        } else {
          MatchOutcome<NumGroups> outcome;
          outcome.status = MatchStatus::Matched;
          if constexpr (TrackCaptures) {
            std::size_t actualStart =
                state.groups[0].offset != std::string_view::npos
                ? state.groups[0].offset
                : start;
            state.groups[0] = {actualStart, matchEnd - actualStart};
          }
          outcome.state = state;
          return outcome;
        }
      }
      if (UseBudget && budgetCopy == 0) {
        return makeBudgetExhausted();
      }
      if constexpr (BoolOnly) {
        return SearchBoolResult{false, false};
      } else {
        return decltype(makeBudgetExhausted()){};
      }
    };

    if constexpr (useMemchr) {
      // memchr-accelerated search: skip to positions near the required
      // literal
      const char* data = input.data();
      const char* end = data + input.size();
      const char* scan = data;

      while (scan < end) {
        const void* found = std::memchr(scan, kReqLit.ch, end - scan);
        if (!found) {
          break;
        }
        std::size_t litPos = static_cast<const char*>(found) - data;

        // Walk backwards from litPos to find valid starting positions
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

          auto result = tryMatch(start);
          if constexpr (BoolOnly) {
            if (result.matched || result.budgetExhausted) {
              return result;
            }
          } else {
            if (result.status != MatchStatus::NoMatch) {
              return result;
            }
          }
        }
        scan = static_cast<const char*>(found) + 1;
      }

      return makeNoMatch();
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

          auto result = tryMatch(start);
          if constexpr (BoolOnly) {
            if (result.matched || result.budgetExhausted) {
              return result;
            }
          } else {
            if (result.status != MatchStatus::NoMatch) {
              return result;
            }
          }
          scan = static_cast<const char*>(found) + 1;
        }

        return makeNoMatch();
      } else {
        for (std::size_t start = 0; start <= maxStart; ++start) {
          if constexpr (!kFilter.accepts_all) {
            if (start < input.size() && !kFilter.test(input[start])) {
              continue;
            }
          }

          auto result = tryMatch(start);
          if constexpr (BoolOnly) {
            if (result.matched || result.budgetExhausted) {
              return result;
            }
          } else {
            if (result.status != MatchStatus::NoMatch) {
              return result;
            }
          }
        }

        return makeNoMatch();
      }
    }
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
