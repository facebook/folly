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

// Alternation branch manipulation: factoring, deduplication, subsumption,
// char merging, anchor hoisting, and nested quantifier flattening checks.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/FixedBitset.h>
#include <folly/regex/detail/optimizer/AstHelpers.h>
#include <folly/regex/detail/optimizer/LiteralOptimizer.h>

namespace folly::regex::detail {

// --------------------------------------------------------------------------
// factorAlternationPrefix:
// Group branches by common literal prefix and factor each group.
// Recurses into factored groups for nested prefixes.
// Unified literal prefix/suffix factoring for alternations.
// When IsSuffix is false, factors common leading literals (prefix).
// When IsSuffix is true, factors common trailing literals (suffix).
// --------------------------------------------------------------------------

template <bool IsSuffix>
constexpr int factorAlternationLiteral(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  auto& altNode = ast.nodes[altNodeIdx];
  if (altNode.kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = altNode.child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].next_sibling;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  constexpr int kMaxExtractLen = 64;
  char targetChar[kMaxAltBranches] = {};
  int hasLen[kMaxAltBranches] = {};

  for (int i = 0; i < branchCount; ++i) {
    if constexpr (IsSuffix) {
      hasLen[i] =
          extractBranchLiteralSuffix(ast, branches[i], &targetChar[i], 1);
    } else {
      hasLen[i] =
          extractBranchLiteralPrefix(ast, branches[i], &targetChar[i], 1);
    }
  }

  FixedBitset<kMaxAltBranches> processed;
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed.test(i)) {
      continue;
    }

    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;

    if (hasLen[i] > 0) {
      for (int j = i + 1; j < branchCount; ++j) {
        if (processed.test(j)) {
          continue;
        }
        if (hasLen[j] > 0 && targetChar[j] == targetChar[i]) {
          group[groupCount++] = j;
        }
      }
    }

    if (groupCount < 2) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    char firstExtracted[kMaxExtractLen] = {};
    int firstLen;
    if constexpr (IsSuffix) {
      firstLen = extractBranchLiteralSuffix(
          ast, branches[group[0]], firstExtracted, kMaxExtractLen);
    } else {
      firstLen = extractBranchLiteralPrefix(
          ast, branches[group[0]], firstExtracted, kMaxExtractLen);
    }
    int commonLen = firstLen;
    for (int g = 1; g < groupCount; ++g) {
      char bExtracted[kMaxExtractLen] = {};
      int bLen;
      if constexpr (IsSuffix) {
        bLen = extractBranchLiteralSuffix(
            ast, branches[group[g]], bExtracted, kMaxExtractLen);
      } else {
        bLen = extractBranchLiteralPrefix(
            ast, branches[group[g]], bExtracted, kMaxExtractLen);
      }
      if (bLen < commonLen) {
        commonLen = bLen;
      }
      int matchLen = 0;
      while (matchLen < commonLen &&
             firstExtracted[matchLen] == bExtracted[matchLen]) {
        ++matchLen;
      }
      commonLen = matchLen;
      if (commonLen == 0) {
        break;
      }
    }

    if (commonLen == 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    for (int g = 0; g < groupCount; ++g) {
      processed.set(group[g]);
    }
    anyFactored = true;

    int strippedBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      if constexpr (IsSuffix) {
        strippedBranches[g] =
            stripLiteralSuffix(ast, branches[group[g]], commonLen);
      } else {
        strippedBranches[g] =
            stripLiteralPrefix(ast, branches[group[g]], commonLen);
      }
    }

    int innerAltIdx = makeAlternation(ast, strippedBranches, groupCount);

    innerAltIdx = factorAlternationLiteral<IsSuffix>(ast, innerAltIdx);

    int grpIdx = wrapInGroup(ast, innerAltIdx);

    if constexpr (IsSuffix) {
      // Reverse the reversed suffix chars for the Literal node
      char suffChars[kMaxExtractLen] = {};
      for (int c = 0; c < commonLen; ++c) {
        suffChars[c] = firstExtracted[commonLen - 1 - c];
      }
      int litIdx = addLiteralNode(ast, suffChars, commonLen);

      // Sequence: group(alternation) -> suffix literal
      int seqChildren[2] = {grpIdx, litIdx};
      int seqIdx = makeSequence(ast, seqChildren, 2);
      newBranches[newBranchCount++] = seqIdx;
    } else {
      int litIdx = addLiteralNode(ast, firstExtracted, commonLen);

      // Sequence: prefix literal -> group(alternation)
      int seqChildren[2] = {litIdx, grpIdx};
      int seqIdx = makeSequence(ast, seqChildren, 2);
      newBranches[newBranchCount++] = seqIdx;
    }
  }

  if (!anyFactored) {
    return altNodeIdx;
  }

  return makeAlternation(ast, newBranches, newBranchCount);
}

constexpr int factorAlternationPrefix(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  return factorAlternationLiteral<false>(ast, altNodeIdx);
}

constexpr int factorAlternationSuffix(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  return factorAlternationLiteral<true>(ast, altNodeIdx);
}

// --------------------------------------------------------------------------
// removeDuplicateBranches: eliminate duplicate alternation branches.
// --------------------------------------------------------------------------

constexpr int removeDuplicateBranches(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].next_sibling;
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

  return makeAlternation(ast, kept, keptCount);
}

// --------------------------------------------------------------------------
// Branch subsumption elimination: remove alternation branches whose match
// set is a strict subset of another branch. E.g. \w+|\d+ → \w+ because
// every string matching \d+ also matches \w+.
// --------------------------------------------------------------------------

struct BranchCharInfo {
  int char_class_index = -1;
  int literal_char = -1;
  bool is_any_byte = false;
  int min_repeat = 1;
  int max_repeat = 1;
  bool valid = false;
};

constexpr BranchCharInfo extractBranchCharInfo(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  BranchCharInfo info;
  if (nodeIdx < 0) {
    return info;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal:
      if (node.literal.size() == 1) {
        info.literal_char = static_cast<unsigned char>(node.literal[0]);
        info.min_repeat = 1;
        info.max_repeat = 1;
        info.valid = true;
      }
      return info;

    case NodeKind::CharClass:
      info.char_class_index = node.char_class_index;
      info.min_repeat = 1;
      info.max_repeat = 1;
      info.valid = true;
      return info;

    case NodeKind::AnyByte:
      info.is_any_byte = true;
      info.min_repeat = 1;
      info.max_repeat = 1;
      info.valid = true;
      return info;

    case NodeKind::Repeat: {
      auto inner = extractBranchCharInfo(ast, node.child_first);
      if (inner.valid && inner.min_repeat == 1 && inner.max_repeat == 1) {
        inner.min_repeat = node.min_repeat;
        inner.max_repeat = node.max_repeat;
        return inner;
      }
      return info;
    }

    case NodeKind::Group:
      return extractBranchCharInfo(ast, node.child_first);

    case NodeKind::Sequence:
    case NodeKind::Alternation:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Empty:
    case NodeKind::Dead:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
      return info;
  }
}

// Check if branch A's character set is a subset of branch B's.
constexpr bool branchCharSetSubsumes(
    const ReadOnlyAst auto& ast,
    const BranchCharInfo& a,
    const BranchCharInfo& b) noexcept {
  // AnyByte subsumes everything
  if (b.is_any_byte) {
    return true;
  }
  if (a.is_any_byte) {
    return false;
  }

  // Both are concrete char sets — compare ranges
  if (b.char_class_index >= 0) {
    const auto& bcc = ast.char_classes[b.char_class_index];
    if (a.char_class_index >= 0) {
      return charClassIsSubsetOf(ast, a.char_class_index, b.char_class_index);
    }
    if (a.literal_char >= 0) {
      return charClassTest(
          ast.ranges + bcc.range_offset,
          bcc.range_count,
          static_cast<char>(a.literal_char));
    }
    return false;
  }

  if (b.literal_char >= 0) {
    if (a.literal_char >= 0) {
      return a.literal_char == b.literal_char;
    }
    return false;
  }

  return false;
}

// Check if repeat range A is subsumed by repeat range B:
// [minA, maxA] ⊆ [minB, maxB]
constexpr bool repeatRangeSubsumed(
    int minA, int maxA, int minB, int maxB) noexcept {
  if (minB > minA) {
    return false;
  }
  if (maxB == -1) {
    return true;
  }
  if (maxA == -1) {
    return false;
  }
  return maxA <= maxB;
}

constexpr int eliminateSubsumedBranches(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].next_sibling;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  // Extract char info for each branch
  BranchCharInfo infos[kMaxAltBranches] = {};
  for (int i = 0; i < branchCount; ++i) {
    infos[i] = extractBranchCharInfo(ast, branches[i]);
  }

  // Skip branches that contain capturing groups (conservative)
  FixedBitset<kMaxAltBranches> hasCapture;
  for (int i = 0; i < branchCount; ++i) {
    if (containsCapturingGroup(ast, branches[i])) {
      hasCapture.set(i);
    }
  }

  // Mark subsumed branches
  FixedBitset<kMaxAltBranches> subsumed;
  for (int i = 0; i < branchCount; ++i) {
    if (!infos[i].valid || hasCapture.test(i) || subsumed.test(i)) {
      continue;
    }
    for (int j = 0; j < branchCount; ++j) {
      if (i == j || !infos[j].valid || hasCapture.test(j) || subsumed.test(j)) {
        continue;
      }
      // Check if branch i is subsumed by branch j
      if (branchCharSetSubsumes(ast, infos[i], infos[j]) &&
          repeatRangeSubsumed(
              infos[i].min_repeat,
              infos[i].max_repeat,
              infos[j].min_repeat,
              infos[j].max_repeat)) {
        subsumed.set(i);
        break;
      }
    }
  }

  // Count remaining branches
  int keptCount = 0;
  for (int i = 0; i < branchCount; ++i) {
    if (!subsumed.test(i)) {
      ++keptCount;
    }
  }

  if (keptCount == branchCount) {
    return altNodeIdx;
  }

  // Build new branch list
  int kept[kMaxAltBranches] = {};
  int ki = 0;
  for (int i = 0; i < branchCount; ++i) {
    if (!subsumed.test(i)) {
      kept[ki++] = branches[i];
    }
  }

  return makeAlternation(ast, kept, ki);
}

// --------------------------------------------------------------------------
// mergeCharBranches: merge single-char Literal and non-negated CharClass
// branches in an alternation into a single CharClass node.
// Multi-char Literals are not merged (they represent strings, not chars).
// --------------------------------------------------------------------------

constexpr int mergeCharBranches(MutableAst auto& ast, int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].next_sibling;
  }

  if (branchCount < 2) {
    return altNodeIdx;
  }

  FixedBitset<kMaxAltBranches> mergeable;
  int mergeCount = 0;
  for (int i = 0; i < branchCount; ++i) {
    const auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Literal && bnode.literal.size() == 1) {
      mergeable.set(i);
      ++mergeCount;
    } else if (bnode.kind == NodeKind::CharClass) {
      mergeable.set(i);
      ++mergeCount;
    }
  }

  if (mergeCount < 2) {
    return altNodeIdx;
  }

  CharRangeSet merged;
  for (int i = 0; i < branchCount; ++i) {
    if (!mergeable.test(i)) {
      continue;
    }
    const auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Literal) {
      merged.addChar(static_cast<unsigned char>(bnode.literal[0]));
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
    if (!mergeable.test(i)) {
      newBranches[newBranchCount++] = branches[i];
    }
  }

  return makeAlternation(ast, newBranches, newBranchCount);
}

// --------------------------------------------------------------------------
// hoistCommonAnchor: factor a common leading anchor out of all alternation
// branches.  (^foo|^bar) -> ^(foo|bar)
// --------------------------------------------------------------------------

constexpr int hoistCommonAnchor(MutableAst auto& ast, int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  {
    int child = ast.nodes[altNodeIdx].child_first;
    while (child >= 0 && branchCount < kMaxAltBranches) {
      branches[branchCount++] = child;
      child = ast.nodes[child].next_sibling;
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
        bnode.kind == NodeKind::Sequence && bnode.child_first >= 0 &&
        ast.nodes[bnode.child_first].kind == NodeKind::Anchor) {
      hasAnchor = true;
      thisAnchor = ast.nodes[bnode.child_first].anchor;
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

  for (int i = 0; i < branchCount; ++i) {
    auto& bnode = ast.nodes[branches[i]];
    if (bnode.kind == NodeKind::Anchor) {
      branches[i] = addEmptyNode(ast);
    } else {
      int firstChild = bnode.child_first;
      int secondChild = ast.nodes[firstChild].next_sibling;
      if (secondChild < 0) {
        branches[i] = addEmptyNode(ast);
      } else {
        int thirdChild = ast.nodes[secondChild].next_sibling;
        if (thirdChild < 0) {
          ast.nodes[secondChild].isolate();
          branches[i] = secondChild;
        } else {
          bnode.child_first = secondChild;
        }
      }
    }
  }

  int innerAltIdx = makeAlternation(ast, branches, branchCount);

  int grpIdx = wrapInGroup(ast, innerAltIdx);

  AstNode anchorNode;
  anchorNode.kind = NodeKind::Anchor;
  anchorNode.anchor = commonAnchor;
  int anchorIdx = ast.addNode(anchorNode);

  int seqChildren[2] = {anchorIdx, grpIdx};
  return makeSequence(ast, seqChildren, 2);
}

// --------------------------------------------------------------------------
// Generalized alternation prefix/suffix factoring: extends literal prefix/
// suffix factoring to handle arbitrary AST node types (CharClass, Repeat,
// etc.) using structural equality comparison (nodesEqual).
// --------------------------------------------------------------------------

constexpr int collectBranchChildren(
    const ReadOnlyAst auto& ast,
    int branchIdx,
    int* out,
    int maxCount) noexcept {
  if (branchIdx < 0 || maxCount <= 0) {
    return 0;
  }
  const auto& node = ast.nodes[branchIdx];
  if (node.kind == NodeKind::Sequence) {
    int count = 0;
    int child = node.child_first;
    while (child >= 0 && count < maxCount) {
      out[count++] = child;
      child = ast.nodes[child].next_sibling;
    }
    return count;
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    return collectBranchChildren(ast, node.child_first, out, maxCount);
  }
  out[0] = branchIdx;
  return 1;
}

template <bool IsSuffix>
constexpr int extractCommonNodes(
    const ReadOnlyAst auto& ast,
    const int* branches,
    int branchCount) noexcept {
  if (branchCount < 2) {
    return 0;
  }
  int firstChildren[kMaxAltBranches] = {};
  int firstChildCount =
      collectBranchChildren(ast, branches[0], firstChildren, kMaxAltBranches);
  if (firstChildCount == 0) {
    return 0;
  }
  int commonLen = firstChildCount;
  for (int b = 1; b < branchCount; ++b) {
    int bChildren[kMaxAltBranches] = {};
    int bChildCount =
        collectBranchChildren(ast, branches[b], bChildren, kMaxAltBranches);
    if (bChildCount < commonLen) {
      commonLen = bChildCount;
    }
    int matchLen = 0;
    while (matchLen < commonLen) {
      int firstPos;
      int bPos;
      if constexpr (IsSuffix) {
        firstPos = firstChildCount - 1 - matchLen;
        bPos = bChildCount - 1 - matchLen;
      } else {
        firstPos = matchLen;
        bPos = matchLen;
      }
      if (!nodesEqual(ast, firstChildren[firstPos], bChildren[bPos])) {
        break;
      }
      ++matchLen;
    }
    commonLen = matchLen;
    if (commonLen == 0) {
      break;
    }
  }
  return commonLen;
}

constexpr int extractCommonNodePrefix(
    const ReadOnlyAst auto& ast,
    const int* branches,
    int branchCount) noexcept {
  return extractCommonNodes<false>(ast, branches, branchCount);
}

constexpr int extractCommonNodeSuffix(
    const ReadOnlyAst auto& ast,
    const int* branches,
    int branchCount) noexcept {
  return extractCommonNodes<true>(ast, branches, branchCount);
}

template <bool IsSuffix>
constexpr int stripNodes(
    MutableAst auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Sequence) {
    int children[kMaxAltBranches] = {};
    int childCount = 0;
    int child = node.child_first;
    while (child >= 0 && childCount < kMaxAltBranches) {
      children[childCount++] = child;
      child = ast.nodes[child].next_sibling;
    }
    int keepCount;
    int keepStart;
    if constexpr (IsSuffix) {
      keepCount = childCount - count;
      keepStart = 0;
    } else {
      keepCount = childCount - count;
      keepStart = count;
    }
    return makeSequence(ast, children + keepStart, keepCount);
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    int inner = stripNodes<IsSuffix>(ast, node.child_first, count);
    if (inner == node.child_first) {
      return nodeIdx;
    }
    if (ast.nodes[inner].kind == NodeKind::Empty) {
      return inner;
    }
    return wrapInGroup(ast, inner);
  }
  return addEmptyNode(ast);
}

constexpr int stripNodePrefix(
    MutableAst auto& ast, int nodeIdx, int count) noexcept {
  return stripNodes<false>(ast, nodeIdx, count);
}

constexpr int stripNodeSuffix(
    MutableAst auto& ast, int nodeIdx, int count) noexcept {
  return stripNodes<true>(ast, nodeIdx, count);
}

template <bool IsSuffix>
constexpr int getBranchTargetChild(
    const ReadOnlyAst auto& ast, int branchIdx) noexcept {
  if (branchIdx < 0) {
    return -1;
  }
  const auto& node = ast.nodes[branchIdx];
  if (node.kind == NodeKind::Sequence) {
    if constexpr (IsSuffix) {
      return node.child_last;
    } else {
      return node.child_first;
    }
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    return getBranchTargetChild<IsSuffix>(ast, node.child_first);
  }
  return branchIdx;
}

template <bool IsSuffix>
constexpr int factorGeneralized(MutableAst auto& ast, int altNodeIdx) noexcept {
  if (ast.nodes[altNodeIdx].kind != NodeKind::Alternation) {
    return altNodeIdx;
  }

  int branches[kMaxAltBranches] = {};
  int branchCount = 0;
  int child = ast.nodes[altNodeIdx].child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branches[branchCount++] = child;
    child = ast.nodes[child].next_sibling;
  }
  if (branchCount < 2) {
    return altNodeIdx;
  }

  int targetChild[kMaxAltBranches] = {};
  for (int i = 0; i < branchCount; ++i) {
    targetChild[i] = getBranchTargetChild<IsSuffix>(ast, branches[i]);
  }

  FixedBitset<kMaxAltBranches> processed;
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed.test(i)) {
      continue;
    }
    if (targetChild[i] < 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;
    for (int j = i + 1; j < branchCount; ++j) {
      if (processed.test(j) || targetChild[j] < 0) {
        continue;
      }
      if (nodesEqual(ast, targetChild[i], targetChild[j])) {
        group[groupCount++] = j;
      }
    }

    if (groupCount < 2) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int groupBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      groupBranches[g] = branches[group[g]];
    }
    int commonLen;
    if constexpr (IsSuffix) {
      commonLen = extractCommonNodeSuffix(ast, groupBranches, groupCount);
    } else {
      commonLen = extractCommonNodePrefix(ast, groupBranches, groupCount);
    }
    if (commonLen == 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    for (int g = 0; g < groupCount; ++g) {
      processed.set(group[g]);
    }
    anyFactored = true;

    // For suffix: save suffix node indices BEFORE stripping, since
    // stripNodeSuffix modifies child links and makes the suffix nodes
    // unreachable from the original branch.
    int savedNodes[kMaxAltBranches] = {};
    if constexpr (IsSuffix) {
      int firstBranchChildren[kMaxAltBranches] = {};
      int firstBranchChildCount = collectBranchChildren(
          ast, branches[group[0]], firstBranchChildren, kMaxAltBranches);
      int suffixStart = firstBranchChildCount - commonLen;
      for (int p = 0; p < commonLen; ++p) {
        savedNodes[p] = firstBranchChildren[suffixStart + p];
      }
    }

    int strippedBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      if constexpr (IsSuffix) {
        strippedBranches[g] =
            stripNodeSuffix(ast, branches[group[g]], commonLen);
      } else {
        strippedBranches[g] =
            stripNodePrefix(ast, branches[group[g]], commonLen);
      }
    }

    int innerAltIdx = makeAlternation(ast, strippedBranches, groupCount);

    innerAltIdx = mergeCharBranches(ast, innerAltIdx);
    innerAltIdx = factorAlternationPrefix(ast, innerAltIdx);
    innerAltIdx = factorAlternationSuffix(ast, innerAltIdx);
    innerAltIdx = factorGeneralized<IsSuffix>(ast, innerAltIdx);

    int grpIdx = wrapInGroup(ast, innerAltIdx);

    if constexpr (IsSuffix) {
      // Sequence: group -> suffix copies
      int seqChildren[kMaxAltBranches] = {};
      int seqCount = 0;
      seqChildren[seqCount++] = grpIdx;
      for (int p = 0; p < commonLen; ++p) {
        AstNode copy = ast.nodes[savedNodes[p]];
        seqChildren[seqCount++] = ast.addNode(copy);
      }
      newBranches[newBranchCount++] = makeSequence(ast, seqChildren, seqCount);
    } else {
      // Sequence: prefix copies -> group
      int prefixChildren[kMaxAltBranches] = {};
      collectBranchChildren(
          ast, branches[group[0]], prefixChildren, kMaxAltBranches);

      int seqChildren[kMaxAltBranches] = {};
      int seqCount = 0;
      for (int p = 0; p < commonLen; ++p) {
        AstNode copy = ast.nodes[prefixChildren[p]];
        seqChildren[seqCount++] = ast.addNode(copy);
      }
      seqChildren[seqCount++] = grpIdx;
      newBranches[newBranchCount++] = makeSequence(ast, seqChildren, seqCount);
    }
  }

  if (!anyFactored) {
    return altNodeIdx;
  }

  return makeAlternation(ast, newBranches, newBranchCount);
}

constexpr int factorGeneralizedPrefix(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  return factorGeneralized<false>(ast, altNodeIdx);
}

constexpr int factorGeneralizedSuffix(
    MutableAst auto& ast, int altNodeIdx) noexcept {
  return factorGeneralized<true>(ast, altNodeIdx);
}

// --------------------------------------------------------------------------
// canFlattenNestedRepeats: check if flattening (?:x{a,b}){c,d} → x{a*c,b*d}
// is safe. The flattened range [a*c, b*d] must equal the union of intervals
// [k*a, k*b] for k from c to d. Adjacent intervals must overlap:
// (k+1)*a ≤ k*b + 1. The worst case at k=c gives: a ≤ c*(b-a) + 1.
// --------------------------------------------------------------------------

constexpr bool canFlattenNestedRepeats(
    int innerMin, int innerMax, int outerMin, int outerMax) noexcept {
  // Outer is exact — single interval, always safe.
  if (outerMin == outerMax) {
    return true;
  }
  // Inner is unbounded — each interval is [k*innerMin, ∞), always contiguous.
  if (innerMax == -1) {
    return true;
  }
  // General overlap check.
  return innerMin <= (outerMin * (innerMax - innerMin) + 1);
}

} // namespace folly::regex::detail
