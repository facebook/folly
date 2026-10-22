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

// AST construction helpers and structural utilities for the optimizer.
//
// Provides small constexpr functions for building and inspecting AST nodes:
// linking sibling lists, creating common node types (Empty, Literal,
// Alternation, Sequence, Group, Repeat), structural equality comparison,
// capturing-group detection, and character-set analysis for follow-set
// extraction and disjointness checks.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/CharClass.h>

namespace folly::regex::detail {

// Maximum number of alternation branches we can handle during factoring.
inline constexpr int kMaxAltBranches = 64;

// --------------------------------------------------------------------------
// AST construction helpers for the optimizer. These combine linking and
// node creation into single calls, replacing repeated multi-line patterns.
// --------------------------------------------------------------------------

// Link an array of node indices into a doubly-linked sibling list.
constexpr void linkSiblingList(
    MutableAst auto& ast, const int* arr, int count) noexcept {
  if (count <= 0) {
    return;
  }
  ast.nodes[arr[0]].prev_sibling = kNoNode;
  for (int i = 0; i < count - 1; ++i) {
    ast.nodes[arr[i]].next_sibling = arr[i + 1];
    ast.nodes[arr[i + 1]].prev_sibling = arr[i];
  }
  ast.nodes[arr[count - 1]].next_sibling = kNoNode;
}

// Create and add an Empty node.
constexpr NodeIdx addEmptyNode(MutableAst auto& ast) noexcept {
  AstNode node;
  node.kind = NodeKind::Empty;
  return ast.addNode(node);
}

// Create and add a Literal node from raw chars.
constexpr NodeIdx addLiteralNode(
    MutableAst auto& ast, const char* chars, int len) noexcept {
  AstNode node;
  node.kind = NodeKind::Literal;
  node.literal = ast.appendLiteral(chars, len);
  return ast.addNode(node);
}

// Build an Alternation from an array of child node indices.
// If count == 1, returns children[0] directly (isolated).
// If count == 0, returns an Empty node.
constexpr NodeIdx makeAlternation(
    MutableAst auto& ast, int* children, int count) noexcept {
  if (count <= 0) {
    return addEmptyNode(ast);
  }
  if (count == 1) {
    ast.nodes[children[0]].isolate();
    return children[0];
  }
  linkSiblingList(ast, children, count);
  AstNode alt;
  alt.kind = NodeKind::Alternation;
  alt.child_first = children[0];
  alt.child_last = children[count - 1];
  return ast.addNode(alt);
}

// Build a Sequence from an array of child node indices.
// If count == 1, returns children[0] directly (isolated).
// If count == 0, returns an Empty node.
constexpr NodeIdx makeSequence(
    MutableAst auto& ast, int* children, int count) noexcept {
  if (count <= 0) {
    return addEmptyNode(ast);
  }
  if (count == 1) {
    ast.nodes[children[0]].isolate();
    return children[0];
  }
  linkSiblingList(ast, children, count);
  AstNode seq;
  seq.kind = NodeKind::Sequence;
  seq.child_first = children[0];
  seq.child_last = children[count - 1];
  return ast.addNode(seq);
}

// Wrap a node in a Group.
constexpr NodeIdx wrapInGroup(
    MutableAst auto& ast,
    NodeIdx inner,
    bool capturing = false,
    int groupId = 0) noexcept {
  AstNode grp;
  grp.kind = NodeKind::Group;
  grp.capturing = capturing;
  grp.group_id = groupId;
  grp.child_first = inner;
  grp.child_last = inner;
  return ast.addNode(grp);
}

// Wrap a node in a Repeat.
constexpr NodeIdx wrapInRepeat(
    MutableAst auto& ast,
    NodeIdx inner,
    int minR,
    int maxR,
    RepeatMode mode) noexcept {
  AstNode rep;
  rep.kind = NodeKind::Repeat;
  rep.min_repeat = minR;
  rep.max_repeat = maxR;
  rep.repeat_mode = mode;
  rep.child_first = inner;
  rep.child_last = inner;
  return ast.addNode(rep);
}

// --------------------------------------------------------------------------
// nodesEqual: recursive structural equality check for AST subtrees.
// --------------------------------------------------------------------------

constexpr bool nodesEqual(const ReadOnlyAst auto& ast, int a, int b) noexcept {
  if (a == b) {
    return true;
  }
  if (a < 0 || b < 0) {
    return a == b;
  }

  const auto& na = ast.nodes[a];
  const auto& nb = ast.nodes[b];

  if (na.kind != nb.kind) {
    return false;
  }

  switch (na.kind) {
    case NodeKind::Empty:
    case NodeKind::AnyByte:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return true;
    case NodeKind::Literal:
      return na.literal == nb.literal;
    case NodeKind::Anchor:
      return na.anchor == nb.anchor;
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
      return na.group_id == nb.group_id;
    case NodeKind::CharClass: {
      const auto& ccA = ast.char_classes[na.char_class_index];
      const auto& ccB = ast.char_classes[nb.char_class_index];
      if (ccA.range_count != ccB.range_count) {
        return false;
      }
      for (int i = 0; i < ccA.range_count; ++i) {
        if (!(ast.ranges[ccA.range_offset + i] ==
              ast.ranges[ccB.range_offset + i])) {
          return false;
        }
      }
      return true;
    }
    case NodeKind::Group:
      if (na.capturing != nb.capturing || na.group_id != nb.group_id) {
        return false;
      }
      return nodesEqual(ast, na.child_first, nb.child_first);
    case NodeKind::Repeat:
      if (na.min_repeat != nb.min_repeat || na.max_repeat != nb.max_repeat ||
          na.repeat_mode != nb.repeat_mode) {
        return false;
      }
      return nodesEqual(ast, na.child_first, nb.child_first);
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int ca = na.child_first;
      int cb = nb.child_first;
      while (ca >= 0 && cb >= 0) {
        if (!nodesEqual(ast, ca, cb)) {
          return false;
        }
        ca = ast.nodes[ca].next_sibling;
        cb = ast.nodes[cb].next_sibling;
      }
      return ca < 0 && cb < 0;
    }
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return nodesEqual(ast, na.child_first, nb.child_first);
  }
  return false;
}

// Check if a subtree contains any capturing groups.
constexpr bool containsCapturingGroup(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Group && node.capturing) {
    return true;
  }
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (containsCapturingGroup(ast, child)) {
          return true;
        }
        child = ast.nodes[child].next_sibling;
      }
      return false;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return containsCapturingGroup(ast, node.child_first);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return false;
  }
}

// --------------------------------------------------------------------------
// Character-set analysis utilities used by possessive promotion and
// literal prefix strip length computation.
// --------------------------------------------------------------------------

// Extract the set of characters that could be the first character matched
// by the subtree rooted at nodeIdx. Returns accepts_all=true when the
// set cannot be determined precisely. Uses the existing FirstCharFilter
// from CharClass.h.
//
// When firstCharOnly is true (default), only the first character of each
// node is included — this is correct for possessive promotion where we
// check disjointness with the follow set at the current position.
// When false, all characters at every position are included — this is
// needed by prefix stripping which checks overlap across the entire
// following content.
constexpr FirstCharFilter extractFollowFilter(
    const ReadOnlyAst auto& ast,
    int nodeIdx,
    int ignoreIdx = -1,
    bool firstCharOnly = true,
    bool reverse = false) noexcept {
  if (nodeIdx < 0) {
    return {.accepts_all = true};
  }
  if (nodeIdx == ignoreIdx) {
    FirstCharFilter empty;
    empty.accepts_all = false;
    return empty;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      if (!node.literal.empty()) {
        FirstCharFilter f;
        f.accepts_all = false;
        if (firstCharOnly) {
          int idx = reverse ? static_cast<int>(node.literal.size()) - 1 : 0;
          f.addChar(static_cast<unsigned char>(node.literal[idx]));
        } else {
          for (int i = 0; i < static_cast<int>(node.literal.size()); ++i) {
            f.addChar(static_cast<unsigned char>(node.literal[i]));
            if (f.accepts_all) {
              break;
            }
          }
        }
        return f;
      }
      return {.accepts_all = true};
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

    case NodeKind::AnyByte:
      return {.accepts_all = true};

    case NodeKind::Group:
      return extractFollowFilter(
          ast, node.child_first, ignoreIdx, firstCharOnly, reverse);

    case NodeKind::Sequence: {
      FirstCharFilter f;
      f.accepts_all = false;
      int child = reverse ? node.child_last : node.child_first;
      while (child >= 0) {
        if (ast.nodes[child].isZeroWidth()) {
          child = reverse
              ? ast.nodes[child].prev_sibling
              : ast.nodes[child].next_sibling;
          continue;
        }
        auto cf =
            extractFollowFilter(ast, child, ignoreIdx, firstCharOnly, reverse);
        if (cf.accepts_all) {
          return cf;
        }
        f.mergeFrom(cf.ranges, cf.range_count);
        if (firstCharOnly) {
          if (f.accepts_all ||
              (!isPossiblyZeroWidth(ast, child) && child != ignoreIdx)) {
            return f;
          }
        } else {
          if (f.accepts_all) {
            return f;
          }
        }
        child = reverse
            ? ast.nodes[child].prev_sibling
            : ast.nodes[child].next_sibling;
      }
      if (f.range_count > 0) {
        return f;
      }
      return {.accepts_all = true};
    }

    case NodeKind::Alternation: {
      int child = node.child_first;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f =
          extractFollowFilter(ast, child, ignoreIdx, firstCharOnly, reverse);
      if (f.accepts_all) {
        return f;
      }
      int next = ast.nodes[child].next_sibling;
      while (next >= 0) {
        auto alt =
            extractFollowFilter(ast, next, ignoreIdx, firstCharOnly, reverse);
        if (alt.accepts_all) {
          return alt;
        }
        f.mergeFrom(alt.ranges, alt.range_count);
        if (f.accepts_all) {
          return f;
        }
        next = ast.nodes[next].next_sibling;
      }
      return f;
    }

    case NodeKind::Repeat:
      return extractFollowFilter(
          ast, node.child_first, ignoreIdx, firstCharOnly, reverse);

    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return {.accepts_all = true};
  }
  return {.accepts_all = true};
}

constexpr bool isSimpleCharTest(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  auto kind = ast.nodes[nodeIdx].kind;
  if (kind == NodeKind::Literal) {
    return ast.nodes[nodeIdx].literal.size() == 1;
  }
  return kind == NodeKind::CharClass || kind == NodeKind::AnyByte;
}

constexpr bool isDisjointFromFilter(
    const ReadOnlyAst auto& ast,
    int charTestNodeIdx,
    const FirstCharFilter& filter) noexcept {
  const auto& node = ast.nodes[charTestNodeIdx];

  if (node.kind == NodeKind::Literal) {
    auto ch = static_cast<unsigned char>(node.literal[0]);
    for (int i = 0; i < filter.range_count; ++i) {
      if (ch >= filter.ranges[i].lo && ch <= filter.ranges[i].hi) {
        return false;
      }
    }
    return true;
  }

  if (node.kind == NodeKind::CharClass) {
    const auto& cc = ast.char_classes[node.char_class_index];
    for (int i = 0; i < cc.range_count; ++i) {
      const auto& r = ast.ranges[cc.range_offset + i];
      for (int j = 0; j < filter.range_count; ++j) {
        if (r.lo <= filter.ranges[j].hi && filter.ranges[j].lo <= r.hi) {
          return false;
        }
      }
    }
    return true;
  }

  return false;
}

constexpr bool areFiltersDisjoint(
    const FirstCharFilter& a, const FirstCharFilter& b) noexcept {
  if (a.accepts_all || b.accepts_all) {
    return false;
  }
  for (int i = 0; i < a.range_count; ++i) {
    for (int j = 0; j < b.range_count; ++j) {
      if (a.ranges[i].lo <= b.ranges[j].hi &&
          b.ranges[j].lo <= a.ranges[i].hi) {
        return false;
      }
    }
  }
  return true;
}

} // namespace folly::regex::detail
