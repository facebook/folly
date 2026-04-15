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

// AST inspection helpers for regex optimizer tests.
//
// All functions are constexpr and work with the compile-time ParseResult
// accessible via Regex<Pat>::parsed_. This enables static_assert checks
// that verify optimizations produce the intended AST transformations.

#pragma once

#include <string_view>

#include <folly/regex/Regex.h>

namespace folly::regex::testing {

using detail::AnchorKind;
using detail::NodeKind;

// Get the NodeKind of the AST root node.
constexpr NodeKind rootKind(const auto& ast) noexcept {
  if (ast.root < 0) {
    return NodeKind::Empty;
  }
  return ast.nodes[ast.root].kind;
}

// Count nodes of a specific kind in the subtree rooted at nodeIdx.
constexpr int countNodesOfKind(
    const auto& ast, int nodeIdx, NodeKind kind) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& node = ast.nodes[nodeIdx];
  int count = (node.kind == kind) ? 1 : 0;
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        count += countNodesOfKind(ast, child, kind);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      count += countNodesOfKind(ast, node.child_first, kind);
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return count;
}

constexpr int countNodesOfKind(const auto& ast, NodeKind kind) noexcept {
  return countNodesOfKind(ast, ast.root, kind);
}

// Check if any node of the given kind exists in the subtree.
constexpr bool hasNodeOfKind(
    const auto& ast, int nodeIdx, NodeKind kind) noexcept {
  return countNodesOfKind(ast, nodeIdx, kind) > 0;
}

constexpr bool hasNodeOfKind(const auto& ast, NodeKind kind) noexcept {
  return hasNodeOfKind(ast, ast.root, kind);
}

// Find first node of the given kind (DFS order). Returns index or -1.
constexpr int findNodeOfKind(
    const auto& ast, int nodeIdx, NodeKind kind) noexcept {
  if (nodeIdx < 0) {
    return -1;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == kind) {
    return nodeIdx;
  }
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        int found = findNodeOfKind(ast, child, kind);
        if (found >= 0) {
          return found;
        }
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return findNodeOfKind(ast, node.child_first, kind);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return -1;
}

constexpr int findNodeOfKind(const auto& ast, NodeKind kind) noexcept {
  return findNodeOfKind(ast, ast.root, kind);
}

// Count direct children of a Sequence or Alternation node.
constexpr int getChildCount(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind != NodeKind::Sequence && node.kind != NodeKind::Alternation) {
    return 0;
  }
  int count = 0;
  int child = node.child_first;
  while (child >= 0) {
    ++count;
    child = ast.nodes[child].next_sibling;
  }
  return count;
}

// Get the Nth direct child (0-based) of a Sequence or Alternation.
constexpr int getNthChild(const auto& ast, int nodeIdx, int n) noexcept {
  if (nodeIdx < 0) {
    return -1;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind != NodeKind::Sequence && node.kind != NodeKind::Alternation) {
    return -1;
  }
  int child = node.child_first;
  for (int i = 0; i < n && child >= 0; ++i) {
    child = ast.nodes[child].next_sibling;
  }
  return child;
}

// Get the last direct child of a Sequence or Alternation.
// Uses child_last for O(1) access.
constexpr int getLastChild(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return -1;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind != NodeKind::Sequence && node.kind != NodeKind::Alternation) {
    return -1;
  }
  return node.child_last;
}

// Check if any Repeat in the subtree is marked possessive.
constexpr bool hasPossessiveRepeat(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Repeat && node.isPossessive()) {
    return true;
  }
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (hasPossessiveRepeat(ast, child)) {
          return true;
        }
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return hasPossessiveRepeat(ast, node.child_first);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return false;
}

constexpr bool hasPossessiveRepeat(const auto& ast) noexcept {
  return hasPossessiveRepeat(ast, ast.root);
}

// Check if any Repeat with specific min/max bounds exists in the subtree.
constexpr bool hasRepeatWithBounds(
    const auto& ast, int nodeIdx, int minRep, int maxRep) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Repeat && node.min_repeat == minRep &&
      node.max_repeat == maxRep) {
    return true;
  }
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (hasRepeatWithBounds(ast, child, minRep, maxRep)) {
          return true;
        }
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return hasRepeatWithBounds(ast, node.child_first, minRep, maxRep);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return false;
}

constexpr bool hasRepeatWithBounds(
    const auto& ast, int minRep, int maxRep) noexcept {
  return hasRepeatWithBounds(ast, ast.root, minRep, maxRep);
}

// Check that the prefix buffer matches the expected string.
constexpr bool prefixEquals(
    const auto& ast, std::string_view expected) noexcept {
  return ast.literal_prefix() == expected;
}

// Check that the suffix buffer matches the expected string.
constexpr bool suffixEquals(
    const auto& ast, std::string_view expected) noexcept {
  return ast.literal_suffix() == expected;
}

// Human-readable name for NodeKind (for assertion error messages).
constexpr const char* nodeKindName(NodeKind kind) noexcept {
  switch (kind) {
    case NodeKind::Empty:
      return "Empty";
    case NodeKind::Literal:
      return "Literal";
    case NodeKind::AnyChar:
      return "AnyChar";
    case NodeKind::AnyByte:
      return "AnyByte";
    case NodeKind::CharClass:
      return "CharClass";
    case NodeKind::Sequence:
      return "Sequence";
    case NodeKind::Alternation:
      return "Alternation";
    case NodeKind::Repeat:
      return "Repeat";
    case NodeKind::Group:
      return "Group";
    case NodeKind::Anchor:
      return "Anchor";
    case NodeKind::WordBoundary:
      return "WordBoundary";
    case NodeKind::NegWordBoundary:
      return "NegWordBoundary";
    case NodeKind::Lookahead:
      return "Lookahead";
    case NodeKind::NegLookahead:
      return "NegLookahead";
    case NodeKind::Lookbehind:
      return "Lookbehind";
    case NodeKind::NegLookbehind:
      return "NegLookbehind";
    case NodeKind::Backref:
      return "Backref";
    case NodeKind::Dead:
      return "Dead";
  }
  return "Unknown";
}

// Verify that prev_sibling links are consistent with next_sibling (next
// sibling) links for all Sequence and Alternation nodes in the AST. Returns
// true if the doubly-linked invariant holds: for adjacent siblings A→B,
// B.prev_sibling == A and A.next_sibling == B.
constexpr bool verifyChildPrevLinks(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }
  const auto& node = ast.nodes[nodeIdx];

  // For container nodes, verify the sibling list is consistent
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int prev = detail::kNoNode;
    int child = node.child_first;
    while (child >= 0) {
      if (ast.nodes[child].prev_sibling != prev) {
        return false;
      }
      prev = child;
      child = ast.nodes[child].next_sibling;
    }
  }

  // Recurse into children
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (!verifyChildPrevLinks(ast, child)) {
          return false;
        }
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      if (!verifyChildPrevLinks(ast, node.child_first)) {
        return false;
      }
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return true;
}

} // namespace folly::regex::testing
