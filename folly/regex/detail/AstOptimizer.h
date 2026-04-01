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

// AST-level optimization pass for the compile-time regex engine.
//
// Currently implemented:
//   - Literal prefix stripping: extracts the leading literal characters from
//     the pattern and stores them separately so the matcher can verify them
//     with memcmp and run the engine on the remainder.  Gated on
//     !has_lookbehind (lookbehinds need to see characters before the current
//     position, which the stripped prefix would remove).
//   - Alternation common prefix/suffix factoring: groups alternation branches
//     that share a common literal prefix (or suffix) and factors it out into
//     a Sequence + non-capturing Group, reducing NFA/DFA state explosion and
//     eliminating redundant backtracker work.
//   - Non-capturing group flattening: (?:(?:a)) -> a
//   - Trivial repeat simplification: a{1} -> a, a{0,0} -> epsilon
//   - Duplicate branch elimination: (foo|foo|bar) -> (foo|bar)
//   - Single-char alternation -> CharClass: (a|b|c|d) -> [abcd]
//   - CharClass merging in alternation: [a-m]|[n-z] -> [a-z]
//   - Anchor hoisting: (^foo|^bar) -> ^(foo|bar)
//   - Nested quantifier flattening: (a+)+ → (a+) when inner is unbounded;
//     generalized for non-capturing groups: (?:a*)* → a*, (?:a+)* → a*, etc.
//   - Empty-branch alternation to optional: (?:|b) → b??, (?:b|) → b?
//   - Adjacent repeat merging: x+x+ → x{2,∞} within sequences
//   - Single-char CharClass to Literal: [a] → a
//   - Backtrack safety for Repeat(Group(CharTest)) and
//     Repeat(Sequence(CharTests...)) (fixed-length sequences)

#pragma once

#include <folly/regex/detail/Ast.h>

namespace folly {
namespace regex {
namespace detail {

// Maximum number of alternation branches we can handle during factoring.
inline constexpr int kMaxAltBranches = 64;

// --------------------------------------------------------------------------
// Helpers for walking AST nodes to extract leading/trailing literal chars.
// --------------------------------------------------------------------------

// Extract leading literal characters from a single branch of an alternation.
// Stops at non-Literal nodes (CharClass, AnyChar, Alternation, optional
// Repeat, etc.).  Returns the number of literal chars extracted.
template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int extractBranchLiteralPrefix(
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx,
    char* out,
    int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal:
      out[0] = node.ch;
      return 1;
    case NodeKind::Sequence: {
      int total = 0;
      int child = node.child_begin;
      while (child >= 0 && total < maxLen) {
        int got =
            extractBranchLiteralPrefix(ast, child, out + total, maxLen - total);
        if (got == 0) {
          break;
        }
        total += got;
        child = ast.nodes[child].child_end;
      }
      return total;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Anchor:
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return 0;
  }
  return 0;
}

// Extract trailing literal characters from a single branch.
// Returns chars in reverse order (caller must reverse).
template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int extractBranchLiteralSuffix(
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx,
    char* out,
    int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal:
      out[0] = node.ch;
      return 1;
    case NodeKind::Sequence: {
      // Collect children for right-to-left traversal
      int children[kMaxAltBranches] = {};
      int count = 0;
      int child = node.child_begin;
      while (child >= 0 && count < kMaxAltBranches) {
        children[count++] = child;
        child = ast.nodes[child].child_end;
      }
      int total = 0;
      for (int i = count - 1; i >= 0 && total < maxLen; --i) {
        int got = extractBranchLiteralSuffix(
            ast, children[i], out + total, maxLen - total);
        if (got == 0) {
          break;
        }
        total += got;
      }
      return total;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Anchor:
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return 0;
  }
  return 0;
}

// --------------------------------------------------------------------------
// stripLiteralPrefix:
// stripLiteralPrefix: remove N leading Literal nodes from a branch.
// Returns the new node index for the branch (may create new nodes).
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int stripLiteralPrefix(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx,
    int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      if (count >= 1) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }
      return nodeIdx;
    }
    case NodeKind::Sequence: {
      int remaining = count;
      int child = node.child_begin;
      int prevChild = -1;

      while (child >= 0 && remaining > 0) {
        const auto& cnode = ast.nodes[child];
        if (cnode.kind == NodeKind::Literal) {
          --remaining;
          prevChild = child;
          child = ast.nodes[child].child_end;
        } else if (cnode.kind == NodeKind::Group && !cnode.capturing) {
          // Recurse into non-capturing group
          int stripped = stripLiteralPrefix(ast, child, remaining);
          if (stripped != child) {
            // Replace this child in the sequence
            if (prevChild >= 0) {
              ast.nodes[prevChild].child_end = stripped;
              ast.nodes[stripped].child_end = ast.nodes[child].child_end;
            } else {
              int nextSib = ast.nodes[child].child_end;
              ast.nodes[stripped].child_end = nextSib;
              // Create new sequence with stripped child as first
              AstNode seq;
              seq.kind = NodeKind::Sequence;
              seq.child_begin = stripped;
              return ast.addNode(seq);
            }
          }
          return nodeIdx;
        } else {
          break;
        }
      }

      if (child < 0) {
        // All children stripped
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }

      if (child == node.child_begin) {
        // Nothing stripped
        return nodeIdx;
      }

      // child is now the first unstripped child
      int next = ast.nodes[child].child_end;
      if (next < 0) {
        // Only one child left, return it directly
        ast.nodes[child].child_end = -1;
        return child;
      }

      // Multiple children remain — create new sequence
      AstNode seq;
      seq.kind = NodeKind::Sequence;
      seq.child_begin = child;
      return ast.addNode(seq);
    }
    case NodeKind::Group: {
      if (node.capturing) {
        return nodeIdx;
      }
      int inner = stripLiteralPrefix(ast, node.child_begin, count);
      if (inner == node.child_begin) {
        return nodeIdx;
      }
      // If inner is Empty, return Empty
      if (ast.nodes[inner].kind == NodeKind::Empty) {
        return inner;
      }
      AstNode grp;
      grp.kind = NodeKind::Group;
      grp.capturing = false;
      grp.child_begin = inner;
      grp.child_end = inner;
      return ast.addNode(grp);
    }
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// stripLiteralSuffix:
// stripLiteralSuffix: remove N trailing Literal nodes from a branch.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int stripLiteralSuffix(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx,
    int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      if (count >= 1) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }
      return nodeIdx;
    }
    case NodeKind::Sequence: {
      // Collect children into array
      int children[kMaxAltBranches] = {};
      int childCount = 0;
      int child = node.child_begin;
      while (child >= 0 && childCount < kMaxAltBranches) {
        children[childCount++] = child;
        child = ast.nodes[child].child_end;
      }

      int remaining = count;
      int keepCount = childCount;

      // Remove trailing Literal children
      while (keepCount > 0 && remaining > 0) {
        int lastIdx = children[keepCount - 1];
        if (ast.nodes[lastIdx].kind == NodeKind::Literal) {
          --keepCount;
          --remaining;
        } else if (
            ast.nodes[lastIdx].kind == NodeKind::Group &&
            !ast.nodes[lastIdx].capturing) {
          int stripped = stripLiteralSuffix(ast, lastIdx, remaining);
          children[keepCount - 1] = stripped;
          remaining = 0;
        } else {
          break;
        }
      }

      if (keepCount == 0) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }

      if (keepCount == childCount) {
        return nodeIdx;
      }

      // Re-link remaining children
      for (int i = 0; i < keepCount - 1; ++i) {
        ast.nodes[children[i]].child_end = children[i + 1];
      }
      ast.nodes[children[keepCount - 1]].child_end = -1;

      if (keepCount == 1) {
        return children[0];
      }

      AstNode seq;
      seq.kind = NodeKind::Sequence;
      seq.child_begin = children[0];
      return ast.addNode(seq);
    }
    case NodeKind::Group: {
      if (node.capturing) {
        return nodeIdx;
      }
      int inner = stripLiteralSuffix(ast, node.child_begin, count);
      if (inner == node.child_begin) {
        return nodeIdx;
      }
      if (ast.nodes[inner].kind == NodeKind::Empty) {
        return inner;
      }
      AstNode grp;
      grp.kind = NodeKind::Group;
      grp.capturing = false;
      grp.child_begin = inner;
      grp.child_end = inner;
      return ast.addNode(grp);
    }
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// factorAlternationPrefix:
// factorAlternationPrefix: group branches by common literal prefix and
// factor each group.  Recurses into factored groups for nested prefixes.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int factorAlternationPrefix(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int altNodeIdx) noexcept {
  auto& altNode = ast.nodes[altNodeIdx];
  if (altNode.kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  // 1. Collect branch indices
  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = altNode.child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].child_end;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  // 2. Extract leading literal prefix for each branch
  constexpr int kMaxPrefixLen = 64;
  char prefixes[kMaxAltBranches][kMaxPrefixLen] = {};
  int prefixLens[kMaxAltBranches] = {};

  for (int i = 0; i < branchCount; ++i) {
    prefixLens[i] = extractBranchLiteralPrefix(
        ast, branches[i], prefixes[i], kMaxPrefixLen);
  }

  // 3. Group branches by common prefix.
  // We use a greedy approach: iterate branches, for each unprocessed branch,
  // find all other branches sharing a common prefix with it.
  bool processed[kMaxAltBranches] = {};
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed[i]) {
      continue;
    }

    // Find all branches sharing a prefix with branch i
    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;

    if (prefixLens[i] > 0) {
      for (int j = i + 1; j < branchCount; ++j) {
        if (processed[j]) {
          continue;
        }
        if (prefixLens[j] > 0 && prefixes[j][0] == prefixes[i][0]) {
          group[groupCount++] = j;
        }
      }
    }

    if (groupCount < 2) {
      // No other branch shares a prefix — keep as-is
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    // Find longest common prefix across this group
    int commonLen = prefixLens[group[0]];
    for (int g = 1; g < groupCount; ++g) {
      int bIdx = group[g];
      int len = prefixLens[bIdx] < commonLen ? prefixLens[bIdx] : commonLen;
      int match = 0;
      while (
          match < len && prefixes[group[0]][match] == prefixes[bIdx][match]) {
        ++match;
      }
      commonLen = match;
    }

    if (commonLen == 0) {
      // First chars match but no further common prefix — keep branches as-is
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    // Bounds check: we need commonLen (prefix literals) + 1 (alternation)
    // + 1 (group) + 1 (sequence) + groupCount (possible Empty nodes)
    int needed = commonLen + 3 + groupCount;
    if (ast.node_count + needed > static_cast<int>(MaxNodes)) {
      // Not enough space — keep branches as-is
      for (int g = 0; g < groupCount; ++g) {
        if (g > 0) {
          processed[group[g]] = false;
        }
      }
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    // Mark all group members as processed
    for (int g = 0; g < groupCount; ++g) {
      processed[group[g]] = true;
    }
    anyFactored = true;

    // 4. Strip prefix from each branch in the group
    int strippedBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      strippedBranches[g] =
          stripLiteralPrefix(ast, branches[group[g]], commonLen);
    }

    // Link stripped branches as siblings
    for (int g = 0; g < groupCount - 1; ++g) {
      ast.nodes[strippedBranches[g]].child_end = strippedBranches[g + 1];
    }
    ast.nodes[strippedBranches[groupCount - 1]].child_end = -1;

    // Create inner Alternation
    AstNode innerAlt;
    innerAlt.kind = NodeKind::Alternation;
    innerAlt.child_begin = strippedBranches[0];
    innerAlt.child_end = -1;
    int innerAltIdx = ast.addNode(innerAlt);

    // Recurse into the inner alternation for further factoring
    innerAltIdx = factorAlternationPrefix(ast, innerAltIdx);

    // Wrap in non-capturing group
    AstNode grp;
    grp.kind = NodeKind::Group;
    grp.capturing = false;
    grp.child_begin = innerAltIdx;
    grp.child_end = innerAltIdx;
    int grpIdx = ast.addNode(grp);

    // Create prefix literal nodes and sequence
    int firstLit = -1;
    int prevLit = -1;
    for (int c = 0; c < commonLen; ++c) {
      AstNode lit;
      lit.kind = NodeKind::Literal;
      lit.ch = prefixes[group[0]][c];
      int litIdx = ast.addNode(lit);
      if (firstLit < 0) {
        firstLit = litIdx;
      }
      if (prevLit >= 0) {
        ast.nodes[prevLit].child_end = litIdx;
      }
      prevLit = litIdx;
    }
    // Link last literal to the group
    ast.nodes[prevLit].child_end = grpIdx;
    ast.nodes[grpIdx].child_end = -1;

    // Create sequence: prefix literals -> group(alternation)
    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = firstLit;
    seq.child_end = -1;
    int seqIdx = ast.addNode(seq);

    newBranches[newBranchCount++] = seqIdx;
  }

  if (!anyFactored) {
    return altNodeIdx;
  }

  // Reassemble alternation
  if (newBranchCount == 1) {
    return newBranches[0];
  }

  // Link new branches
  for (int i = 0; i < newBranchCount - 1; ++i) {
    ast.nodes[newBranches[i]].child_end = newBranches[i + 1];
  }
  ast.nodes[newBranches[newBranchCount - 1]].child_end = -1;

  AstNode newAlt;
  newAlt.kind = NodeKind::Alternation;
  newAlt.child_begin = newBranches[0];
  newAlt.child_end = -1;
  return ast.addNode(newAlt);
}

// --------------------------------------------------------------------------
// factorAlternationSuffix: same as prefix but for trailing literals.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int factorAlternationSuffix(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int altNodeIdx) noexcept {
  auto& altNode = ast.nodes[altNodeIdx];
  if (altNode.kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = altNode.child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].child_end;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  constexpr int kMaxSuffixLen = 64;
  // Suffixes are stored in reverse order
  char suffixes[kMaxAltBranches][kMaxSuffixLen] = {};
  int suffixLens[kMaxAltBranches] = {};

  for (int i = 0; i < branchCount; ++i) {
    suffixLens[i] = extractBranchLiteralSuffix(
        ast, branches[i], suffixes[i], kMaxSuffixLen);
  }

  bool processed[kMaxAltBranches] = {};
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed[i]) {
      continue;
    }

    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;

    if (suffixLens[i] > 0) {
      for (int j = i + 1; j < branchCount; ++j) {
        if (processed[j]) {
          continue;
        }
        if (suffixLens[j] > 0 && suffixes[j][0] == suffixes[i][0]) {
          group[groupCount++] = j;
        }
      }
    }

    if (groupCount < 2) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int commonLen = suffixLens[group[0]];
    for (int g = 1; g < groupCount; ++g) {
      int bIdx = group[g];
      int len = suffixLens[bIdx] < commonLen ? suffixLens[bIdx] : commonLen;
      int match = 0;
      while (
          match < len && suffixes[group[0]][match] == suffixes[bIdx][match]) {
        ++match;
      }
      commonLen = match;
    }

    if (commonLen == 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int needed = commonLen + 3 + groupCount;
    if (ast.node_count + needed > static_cast<int>(MaxNodes)) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    for (int g = 0; g < groupCount; ++g) {
      processed[group[g]] = true;
    }
    anyFactored = true;

    int strippedBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      strippedBranches[g] =
          stripLiteralSuffix(ast, branches[group[g]], commonLen);
    }

    for (int g = 0; g < groupCount - 1; ++g) {
      ast.nodes[strippedBranches[g]].child_end = strippedBranches[g + 1];
    }
    ast.nodes[strippedBranches[groupCount - 1]].child_end = -1;

    AstNode innerAlt;
    innerAlt.kind = NodeKind::Alternation;
    innerAlt.child_begin = strippedBranches[0];
    innerAlt.child_end = -1;
    int innerAltIdx = ast.addNode(innerAlt);

    innerAltIdx = factorAlternationSuffix(ast, innerAltIdx);

    AstNode grp;
    grp.kind = NodeKind::Group;
    grp.capturing = false;
    grp.child_begin = innerAltIdx;
    grp.child_end = innerAltIdx;
    int grpIdx = ast.addNode(grp);

    // Create suffix literal nodes (reverse the reversed suffix)
    int firstLit = -1;
    int prevLit = -1;
    for (int c = commonLen - 1; c >= 0; --c) {
      AstNode lit;
      lit.kind = NodeKind::Literal;
      lit.ch = suffixes[group[0]][c];
      int litIdx = ast.addNode(lit);
      if (firstLit < 0) {
        firstLit = litIdx;
      }
      if (prevLit >= 0) {
        ast.nodes[prevLit].child_end = litIdx;
      }
      prevLit = litIdx;
    }
    ast.nodes[prevLit].child_end = -1;

    // Sequence: group(alternation) -> suffix literals
    ast.nodes[grpIdx].child_end = firstLit;

    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = grpIdx;
    seq.child_end = -1;
    int seqIdx = ast.addNode(seq);

    newBranches[newBranchCount++] = seqIdx;
  }

  if (!anyFactored) {
    return altNodeIdx;
  }

  if (newBranchCount == 1) {
    return newBranches[0];
  }

  for (int i = 0; i < newBranchCount - 1; ++i) {
    ast.nodes[newBranches[i]].child_end = newBranches[i + 1];
  }
  ast.nodes[newBranches[newBranchCount - 1]].child_end = -1;

  AstNode newAlt;
  newAlt.kind = NodeKind::Alternation;
  newAlt.child_begin = newBranches[0];
  newAlt.child_end = -1;
  return ast.addNode(newAlt);
}

// --------------------------------------------------------------------------
// stripRootLiteralPrefix: strip the literal prefix from the AST root and
// store it in ParseResult.  Only safe when !has_lookbehind.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr void stripRootLiteralPrefix(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>&
        ast) noexcept {
  if (ast.has_lookbehind || ast.root < 0) {
    return;
  }

  // Determine prefix length
  char prefix[64] = {};
  int prefixLen = extractBranchLiteralPrefix(ast, ast.root, prefix, 64);

  if (prefixLen == 0) {
    return;
  }

  // Store the stripped prefix in ParseResult
  for (int i = 0; i < prefixLen && i < 64; ++i) {
    ast.stripped_prefix[i] = prefix[i];
  }
  ast.stripped_prefix_len = prefixLen;

  // Strip from AST
  ast.root = stripLiteralPrefix(ast, ast.root, prefixLen);
}

// --------------------------------------------------------------------------
// nodesEqual: recursive structural equality check for AST subtrees.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr bool nodesEqual(
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int a,
    int b) noexcept {
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
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return true;
    case NodeKind::Literal:
      return na.ch == nb.ch;
    case NodeKind::Anchor:
      return na.anchor == nb.anchor;
    case NodeKind::Backref:
      return na.group_id == nb.group_id;
    case NodeKind::CharClass: {
      if (na.negated != nb.negated) {
        return false;
      }
      const auto& ccA = ast.char_classes[na.char_class_index];
      const auto& ccB = ast.char_classes[nb.char_class_index];
      if (ccA.range_count != ccB.range_count || ccA.negated != ccB.negated) {
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
      return nodesEqual(ast, na.child_begin, nb.child_begin);
    case NodeKind::Repeat:
      if (na.min_repeat != nb.min_repeat || na.max_repeat != nb.max_repeat ||
          na.greedy != nb.greedy) {
        return false;
      }
      return nodesEqual(ast, na.child_begin, nb.child_begin);
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int ca = na.child_begin;
      int cb = nb.child_begin;
      while (ca >= 0 && cb >= 0) {
        if (!nodesEqual(ast, ca, cb)) {
          return false;
        }
        ca = ast.nodes[ca].child_end;
        cb = ast.nodes[cb].child_end;
      }
      return ca < 0 && cb < 0;
    }
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return nodesEqual(ast, na.child_begin, nb.child_begin);
  }
  return false;
}

// --------------------------------------------------------------------------
// removeDuplicateBranches: eliminate duplicate alternation branches.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int removeDuplicateBranches(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].child_end;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  int kept[kMaxAltBranches] = {};
  int keptCount = 0;

  for (int i = 0; i < branchCount; ++i) {
    bool isDuplicate = false;
    for (int j = 0; j < keptCount; ++j) {
      if (nodesEqual(ast, branches[i], kept[j])) {
        isDuplicate = true;
        break;
      }
    }
    if (!isDuplicate) {
      kept[keptCount++] = branches[i];
    }
  }

  if (keptCount == branchCount) {
    return altNodeIdx;
  }

  if (keptCount == 1) {
    ast.nodes[kept[0]].child_end = -1;
    return kept[0];
  }

  for (int i = 0; i < keptCount - 1; ++i) {
    ast.nodes[kept[i]].child_end = kept[i + 1];
  }
  ast.nodes[kept[keptCount - 1]].child_end = -1;

  AstNode newAlt;
  newAlt.kind = NodeKind::Alternation;
  newAlt.child_begin = kept[0];
  newAlt.child_end = -1;
  return ast.addNode(newAlt);
}

// --------------------------------------------------------------------------
// mergeCharBranches: merge single-Literal and non-negated CharClass branches
// in an alternation into a single CharClass node.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int mergeCharBranches(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].child_end;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  bool mergeable[kMaxAltBranches] = {};
  int mergeCount = 0;
  for (int i = 0; i < branchCount; ++i) {
    const auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Literal ||
        (bnode.kind == NodeKind::CharClass && !bnode.negated)) {
      mergeable[i] = true;
      ++mergeCount;
    }
  }

  if (mergeCount < 2) {
    return altNodeIdx;
  }

  if (ast.node_count + 1 > static_cast<int>(MaxNodes) ||
      ast.char_class_count + 1 > static_cast<int>(MaxClasses)) {
    return altNodeIdx;
  }

  CharRangeSet merged;
  for (int i = 0; i < branchCount; ++i) {
    if (!mergeable[i]) {
      continue;
    }
    const auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Literal) {
      merged.addChar(static_cast<unsigned char>(bnode.ch));
    } else {
      const auto& cc = ast.char_classes[bnode.char_class_index];
      for (int j = 0; j < cc.range_count; ++j) {
        merged.addRange(
            ast.ranges[cc.range_offset + j].lo,
            ast.ranges[cc.range_offset + j].hi);
      }
    }
  }

  int ccIdx = ast.addCharClass(merged);
  AstNode ccNode;
  ccNode.kind = NodeKind::CharClass;
  ccNode.char_class_index = ccIdx;
  int mergedIdx = ast.addNode(ccNode);

  int keepCount = branchCount - mergeCount;
  if (keepCount == 0) {
    return mergedIdx;
  }

  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  newBranches[newBranchCount++] = mergedIdx;
  for (int i = 0; i < branchCount; ++i) {
    if (!mergeable[i]) {
      newBranches[newBranchCount++] = branches[i];
    }
  }

  for (int i = 0; i < newBranchCount - 1; ++i) {
    ast.nodes[newBranches[i]].child_end = newBranches[i + 1];
  }
  ast.nodes[newBranches[newBranchCount - 1]].child_end = -1;

  if (newBranchCount == 1) {
    return newBranches[0];
  }

  AstNode newAlt;
  newAlt.kind = NodeKind::Alternation;
  newAlt.child_begin = newBranches[0];
  newAlt.child_end = -1;
  return ast.addNode(newAlt);
}

// --------------------------------------------------------------------------
// hoistCommonAnchor: factor a common leading anchor out of all alternation
// branches.  (^foo|^bar) -> ^(foo|bar)
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int hoistCommonAnchor(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  {
    int child = ast.nodes[altNodeIdx].child_begin;
    while (child >= 0 && branchCount < kMaxAltBranches) {
      branches[branchCount++] = child;
      child = ast.nodes[child].child_end;
    }
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  AnchorKind commonAnchor{};
  for (int i = 0; i < branchCount; ++i) {
    const auto& bnode = ast.nodes[branches[i]];
    bool hasAnchor = false;
    AnchorKind thisAnchor{};

    if (bnode.kind == NodeKind::Anchor) {
      hasAnchor = true;
      thisAnchor = bnode.anchor;
    } else if (
        bnode.kind == NodeKind::Sequence && bnode.child_begin >= 0 &&
        ast.nodes[bnode.child_begin].kind == NodeKind::Anchor) {
      hasAnchor = true;
      thisAnchor = ast.nodes[bnode.child_begin].anchor;
    }

    if (!hasAnchor) {
      return altNodeIdx;
    }
    if (i == 0) {
      commonAnchor = thisAnchor;
    } else if (thisAnchor != commonAnchor) {
      return altNodeIdx;
    }
  }

  // Need up to branchCount Empty nodes + Alternation + Group + Anchor + Seq
  if (ast.node_count + branchCount + 4 > static_cast<int>(MaxNodes)) {
    return altNodeIdx;
  }

  for (int i = 0; i < branchCount; ++i) {
    auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Anchor) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      branches[i] = ast.addNode(empty);
    } else {
      int firstChild = bnode.child_begin;
      int secondChild = ast.nodes[firstChild].child_end;
      if (secondChild < 0) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        branches[i] = ast.addNode(empty);
      } else {
        int thirdChild = ast.nodes[secondChild].child_end;
        if (thirdChild < 0) {
          ast.nodes[secondChild].child_end = -1;
          branches[i] = secondChild;
        } else {
          bnode.child_begin = secondChild;
        }
      }
    }
  }

  for (int i = 0; i < branchCount - 1; ++i) {
    ast.nodes[branches[i]].child_end = branches[i + 1];
  }
  ast.nodes[branches[branchCount - 1]].child_end = -1;

  AstNode innerAlt;
  innerAlt.kind = NodeKind::Alternation;
  innerAlt.child_begin = branches[0];
  innerAlt.child_end = -1;
  int innerAltIdx = ast.addNode(innerAlt);

  AstNode grp;
  grp.kind = NodeKind::Group;
  grp.capturing = false;
  grp.child_begin = innerAltIdx;
  grp.child_end = innerAltIdx;
  int grpIdx = ast.addNode(grp);

  AstNode anchorNode;
  anchorNode.kind = NodeKind::Anchor;
  anchorNode.anchor = commonAnchor;
  int anchorIdx = ast.addNode(anchorNode);

  ast.nodes[anchorIdx].child_end = grpIdx;
  ast.nodes[grpIdx].child_end = -1;

  AstNode seq;
  seq.kind = NodeKind::Sequence;
  seq.child_begin = anchorIdx;
  seq.child_end = -1;
  return ast.addNode(seq);
}

// --------------------------------------------------------------------------
// optimizeNode: recursively optimize a single AST node.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int optimizeNode(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence: {
      // Optimize each child
      int child = node.child_begin;
      int prevChild = -1;
      while (child >= 0) {
        int nextSib = ast.nodes[child].child_end;
        int optimized = optimizeNode(ast, child);
        if (optimized != child) {
          ast.nodes[optimized].child_end = nextSib;
          if (prevChild >= 0) {
            ast.nodes[prevChild].child_end = optimized;
          } else {
            node.child_begin = optimized;
          }
          child = optimized;
        }
        prevChild = child;
        child = ast.nodes[child].child_end;
      }
      // Adjacent repeat merging: x+x+ → x{2,∞}
      // When two adjacent children are Repeat nodes with the same inner
      // expression (by nodesEqual), same greediness, and neither is
      // possessive, merge into a single Repeat with combined bounds.
      {
        int cur = node.child_begin;
        int prev2 = -1;
        while (cur >= 0) {
          int next = ast.nodes[cur].child_end;
          if (next >= 0 && ast.nodes[cur].kind == NodeKind::Repeat &&
              ast.nodes[next].kind == NodeKind::Repeat &&
              ast.nodes[cur].greedy == ast.nodes[next].greedy &&
              !ast.nodes[cur].possessive && !ast.nodes[next].possessive &&
              nodesEqual(
                  ast,
                  ast.nodes[cur].child_begin,
                  ast.nodes[next].child_begin)) {
            int minC = ast.nodes[cur].min_repeat + ast.nodes[next].min_repeat;
            int maxC = (ast.nodes[cur].max_repeat == -1 ||
                        ast.nodes[next].max_repeat == -1)
                ? -1
                : ast.nodes[cur].max_repeat + ast.nodes[next].max_repeat;
            AstNode merged;
            merged.kind = NodeKind::Repeat;
            merged.min_repeat = minC;
            merged.max_repeat = maxC;
            merged.greedy = ast.nodes[cur].greedy;
            merged.child_begin = ast.nodes[cur].child_begin;
            merged.child_end = ast.nodes[cur].child_begin;
            int mergedIdx = ast.addNode(merged);
            ast.nodes[mergedIdx].child_end = ast.nodes[next].child_end;
            if (prev2 >= 0) {
              ast.nodes[prev2].child_end = mergedIdx;
            } else {
              node.child_begin = mergedIdx;
            }
            cur = mergedIdx;
          } else {
            prev2 = cur;
            cur = next;
          }
        }
      }
      return nodeIdx;
    }
    case NodeKind::Alternation: {
      // Optimize each branch first
      int child = node.child_begin;
      int prevChild = -1;
      while (child >= 0) {
        int nextSib = ast.nodes[child].child_end;
        int optimized = optimizeNode(ast, child);
        if (optimized != child) {
          ast.nodes[optimized].child_end = nextSib;
          if (prevChild >= 0) {
            ast.nodes[prevChild].child_end = optimized;
          } else {
            node.child_begin = optimized;
          }
          child = optimized;
        }
        prevChild = child;
        child = ast.nodes[child].child_end;
      }

      // Duplicate elimination, char merging, anchor hoisting, then factoring
      int result = removeDuplicateBranches(ast, nodeIdx);
      result = mergeCharBranches(ast, result);
      result = hoistCommonAnchor(ast, result);
      result = factorAlternationPrefix(ast, result);
      result = factorAlternationSuffix(ast, result);
      return result;
    }
    case NodeKind::Group: {
      int inner = optimizeNode(ast, node.child_begin);
      if (inner != node.child_begin) {
        node.child_begin = inner;
      }
      if (!node.capturing) {
        auto childKind = ast.nodes[node.child_begin].kind;
        switch (childKind) {
          case NodeKind::Group:
          case NodeKind::Literal:
          case NodeKind::CharClass:
          case NodeKind::AnyChar:
          case NodeKind::AnyByte:
          case NodeKind::Empty:
          case NodeKind::Anchor:
          case NodeKind::Backref:
          case NodeKind::WordBoundary:
          case NodeKind::NegWordBoundary:
          case NodeKind::Repeat:
          case NodeKind::Lookahead:
          case NodeKind::NegLookahead:
          case NodeKind::Lookbehind:
          case NodeKind::NegLookbehind:
            return node.child_begin;
          case NodeKind::Sequence:
          case NodeKind::Alternation:
            break;
        }
      }
      return nodeIdx;
    }
    case NodeKind::Repeat: {
      int inner = optimizeNode(ast, node.child_begin);
      if (inner != node.child_begin) {
        node.child_begin = inner;
      }
      if (node.min_repeat == 0 && node.max_repeat == 0) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }
      if (node.min_repeat == 1 && node.max_repeat == 1) {
        return node.child_begin;
      }
      // Nested quantifier flattening.
      // When the outer Repeat wraps a Group whose sole child is an inner
      // Repeat over a simple char test (Literal, CharClass, AnyChar,
      // AnyByte), AND the inner Repeat is unbounded (max == -1), AND
      // both share the same greediness and neither is possessive:
      //
      //   Case A (capturing OK): outer min==1, inner min>=1.
      //     The inner greedy quantifier consumes all matching chars on
      //     its first (and only) iteration, so the outer Repeat is
      //     redundant.  Drop the outer Repeat, return the Group.
      //
      //   Case B (non-capturing only): any other combination.
      //     Flatten to Repeat{inner_min*outer_min, -1}(CharTest),
      //     bypassing the Group entirely.  Not safe for capturing
      //     groups because repeated-capture semantics differ.
      if (!node.possessive) {
        int childIdx = node.child_begin;
        const auto& child = ast.nodes[childIdx];
        if (child.kind == NodeKind::Group) {
          // Repeat wraps a Group wrapping an inner Repeat over a CharTest.
          int gcIdx = child.child_begin;
          if (gcIdx >= 0) {
            const auto& gc = ast.nodes[gcIdx];
            if (gc.kind == NodeKind::Repeat && gc.max_repeat == -1 &&
                gc.greedy == node.greedy && !gc.possessive) {
              int charIdx = gc.child_begin;
              if (charIdx >= 0) {
                auto ck = ast.nodes[charIdx].kind;
                if (ck == NodeKind::Literal || ck == NodeKind::CharClass ||
                    ck == NodeKind::AnyChar || ck == NodeKind::AnyByte) {
                  // Case A: capturing OK when outer min==1, inner min>=1.
                  if (node.min_repeat == 1 && gc.min_repeat >= 1) {
                    return node.child_begin;
                  }
                  // Case B: non-capturing — flatten completely.
                  if (!child.capturing) {
                    AstNode flat;
                    flat.kind = NodeKind::Repeat;
                    flat.min_repeat = gc.min_repeat * node.min_repeat;
                    flat.max_repeat = -1;
                    flat.greedy = node.greedy;
                    flat.child_begin = charIdx;
                    flat.child_end = charIdx;
                    return ast.addNode(flat);
                  }
                }
              }
            }
          }
        } else if (child.kind == NodeKind::Repeat) {
          // Repeat wraps a Repeat directly (Group already flattened).
          // Always safe since no capturing group is involved.
          if (child.max_repeat == -1 && child.greedy == node.greedy &&
              !child.possessive) {
            int charIdx = child.child_begin;
            if (charIdx >= 0) {
              auto ck = ast.nodes[charIdx].kind;
              if (ck == NodeKind::Literal || ck == NodeKind::CharClass ||
                  ck == NodeKind::AnyChar || ck == NodeKind::AnyByte) {
                // outer min==1 and inner min>=1: just drop the outer.
                if (node.min_repeat == 1 && child.min_repeat >= 1) {
                  return childIdx;
                }
                // Otherwise flatten to a single Repeat.
                AstNode flat;
                flat.kind = NodeKind::Repeat;
                flat.min_repeat = child.min_repeat * node.min_repeat;
                flat.max_repeat = -1;
                flat.greedy = node.greedy;
                flat.child_begin = charIdx;
                flat.child_end = charIdx;
                return ast.addNode(flat);
              }
            }
          }
        }
      }
      return nodeIdx;
    }
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind: {
      int inner = optimizeNode(ast, node.child_begin);
      if (inner != node.child_begin) {
        node.child_begin = inner;
      }
      return nodeIdx;
    }
    case NodeKind::CharClass: {
      // Single-char CharClass to Literal: [a] → a
      // Enables better prefix extraction and alternation factoring.
      if (!node.negated && node.char_class_index >= 0) {
        const auto& cc = ast.char_classes[node.char_class_index];
        if (!cc.negated && cc.range_count == 1) {
          const auto& r = ast.ranges[cc.range_offset];
          if (r.lo == r.hi) {
            AstNode lit;
            lit.kind = NodeKind::Literal;
            lit.ch = static_cast<char>(r.lo);
            return ast.addNode(lit);
          }
        }
      }
      return nodeIdx;
    }
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return nodeIdx;
  }
}

// --------------------------------------------------------------------------
// simplifyEmptyAlternation: convert 2-branch alternations where one branch
// is Empty into optional (Repeat{0,1}) quantifiers.
//   Alternation(Empty, X) → Repeat{0,1, lazy}(X)    i.e. X??
//   Alternation(X, Empty) → Repeat{0,1, greedy}(X)  i.e. X?
// factorAlternationPrefix can create inner alternations like (?:|b) that
// are not revisited by optimizeNode; this pass catches them.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int simplifyEmptyAlternation(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_begin;
      int prevChild = -1;
      while (child >= 0) {
        int nextSib = ast.nodes[child].child_end;
        int simplified = simplifyEmptyAlternation(ast, child);
        if (simplified != child) {
          ast.nodes[simplified].child_end = nextSib;
          if (prevChild >= 0) {
            ast.nodes[prevChild].child_end = simplified;
          } else {
            node.child_begin = simplified;
          }
          child = simplified;
        }
        prevChild = child;
        child = nextSib;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind: {
      int inner = simplifyEmptyAlternation(ast, node.child_begin);
      if (inner != node.child_begin) {
        node.child_begin = inner;
      }
      break;
    }
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      break;
  }

  if (node.kind == NodeKind::Alternation) {
    int branch0 = node.child_begin;
    if (branch0 < 0) {
      return nodeIdx;
    }
    int branch1 = ast.nodes[branch0].child_end;
    if (branch1 < 0) {
      return nodeIdx;
    }
    if (ast.nodes[branch1].child_end >= 0) {
      return nodeIdx;
    }

    bool emptyFirst = ast.nodes[branch0].kind == NodeKind::Empty;
    bool emptySecond = ast.nodes[branch1].kind == NodeKind::Empty;
    if (!emptyFirst && !emptySecond) {
      return nodeIdx;
    }
    if (emptyFirst && emptySecond) {
      return nodeIdx;
    }

    int nonEmptyBranch = emptyFirst ? branch1 : branch0;

    AstNode repeat;
    repeat.kind = NodeKind::Repeat;
    repeat.min_repeat = 0;
    repeat.max_repeat = 1;
    repeat.greedy = !emptyFirst;
    repeat.child_begin = nonEmptyBranch;
    repeat.child_end = nonEmptyBranch;
    return ast.addNode(repeat);
  }

  if (node.kind == NodeKind::Group && !node.capturing) {
    auto childKind = ast.nodes[node.child_begin].kind;
    if (childKind != NodeKind::Sequence && childKind != NodeKind::Alternation) {
      return node.child_begin;
    }
  }

  return nodeIdx;
}

// --------------------------------------------------------------------------
// computeBacktrackSafe: determine if the pattern is provably linear-time
// for the backtracker (all Repeat nodes have CharTestTrait-eligible children).
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr bool computeBacktrackSafe(
    const ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Repeat: {
      int innerIdx = node.child_begin;
      if (innerIdx < 0) {
        return true;
      }
      auto innerKind = ast.nodes[innerIdx].kind;
      // Unwrap a Group wrapper if present (capturing or not).
      int effectiveIdx = innerIdx;
      auto effectiveKind = innerKind;
      if (innerKind == NodeKind::Group) {
        effectiveIdx = ast.nodes[innerIdx].child_begin;
        if (effectiveIdx < 0) {
          return false;
        }
        effectiveKind = ast.nodes[effectiveIdx].kind;
      }
      // Direct char test (or char test inside a Group).
      if (effectiveKind == NodeKind::Literal ||
          effectiveKind == NodeKind::CharClass ||
          effectiveKind == NodeKind::AnyChar ||
          effectiveKind == NodeKind::AnyByte) {
        return true;
      }
      // Fixed-length sequence of char tests (possibly inside a Group).
      // Each iteration consumes a fixed number of characters, so the
      // pattern is provably linear-time.
      if (effectiveKind == NodeKind::Sequence) {
        bool allCharTest = true;
        int sc = ast.nodes[effectiveIdx].child_begin;
        while (sc >= 0) {
          auto sk = ast.nodes[sc].kind;
          if (sk != NodeKind::Literal && sk != NodeKind::CharClass &&
              sk != NodeKind::AnyChar && sk != NodeKind::AnyByte) {
            allCharTest = false;
            break;
          }
          sc = ast.nodes[sc].child_end;
        }
        if (allCharTest) {
          return true;
        }
      }
      return false;
    }
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_begin;
      while (child >= 0) {
        if (!computeBacktrackSafe(ast, child)) {
          return false;
        }
        child = ast.nodes[child].child_end;
      }
      return true;
    }
    case NodeKind::Group:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return computeBacktrackSafe(ast, node.child_begin);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
      return true;
    case NodeKind::Backref:
      return false;
  }
  return true;
}

// --------------------------------------------------------------------------
// applyMultilineFlag: rewrite Begin anchors to BeginLine and End anchors
// to EndLine so that ^/$ match at line boundaries instead of string
// boundaries.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr void applyMultilineFlag(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Anchor) {
    if (node.anchor == AnchorKind::Begin) {
      node.anchor = AnchorKind::BeginLine;
    } else if (node.anchor == AnchorKind::End) {
      node.anchor = AnchorKind::EndLine;
    }
  }
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int child = node.child_begin;
    while (child >= 0) {
      applyMultilineFlag(ast, child);
      child = ast.nodes[child].child_end;
    }
  } else if (
      node.kind == NodeKind::Group || node.kind == NodeKind::Repeat ||
      node.kind == NodeKind::Lookahead || node.kind == NodeKind::NegLookahead ||
      node.kind == NodeKind::Lookbehind ||
      node.kind == NodeKind::NegLookbehind) {
    applyMultilineFlag(ast, node.child_begin);
  }
}

// --------------------------------------------------------------------------
// applyDotAllFlag: rewrite AnyChar nodes to AnyByte so that .
// matches newlines.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr void applyDotAllFlag(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::AnyChar) {
    node.kind = NodeKind::AnyByte;
  }
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int child = node.child_begin;
    while (child >= 0) {
      applyDotAllFlag(ast, child);
      child = ast.nodes[child].child_end;
    }
  } else if (
      node.kind == NodeKind::Group || node.kind == NodeKind::Repeat ||
      node.kind == NodeKind::Lookahead || node.kind == NodeKind::NegLookahead ||
      node.kind == NodeKind::Lookbehind ||
      node.kind == NodeKind::NegLookbehind) {
    applyDotAllFlag(ast, node.child_begin);
  }
}

// --------------------------------------------------------------------------
// optimizeAst: top-level entry point.  Applies all AST optimizations.
// --------------------------------------------------------------------------

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords>
constexpr int optimizeAst(
    ParseResult<MaxNodes, MaxRanges, MaxClasses, MaxBitmapWords>& ast,
    int rootIdx) noexcept {
  // First: alternation factoring (bottom-up recursive)
  int result = optimizeNode(ast, rootIdx);

  // Second pass: convert Alternation(Empty, X) → Repeat{0,1}(X).
  // factorAlternationPrefix can create inner alternations like (?:|b) that
  // are not revisited by optimizeNode; this pass catches them.
  result = simplifyEmptyAlternation(ast, result);

  // Then: strip root literal prefix (must come after alternation factoring
  // so that factored alternations can expose longer shared prefixes)
  ast.root = result;
  stripRootLiteralPrefix(ast);

  // Compute backtrack safety: true when all Repeat nodes wrap simple
  // character tests (Literal, CharClass, AnyChar) and the pattern has
  // no backreferences.  Safe patterns cannot cause exponential
  // backtracking, so the budget mechanism can be skipped.
  ast.backtrack_safe = computeBacktrackSafe(ast, ast.root);

  return ast.root;
}

} // namespace detail
} // namespace regex
} // namespace folly
