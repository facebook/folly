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
//   - Generalized alternation prefix/suffix factoring: extends the above to
//     handle non-literal node types (CharClass, Repeat, etc.) using structural
//     equality comparison (nodesEqual). E.g. \w+x|\w+y -> \w+[xy].
//   - Non-capturing group flattening: (?:(?:a)) -> a
//   - Trivial repeat simplification: a{1} -> a, a{0,0} -> epsilon
//   - Duplicate branch elimination: (foo|foo|bar) -> (foo|bar)
//   - Branch subsumption elimination: (\w+|\d+) -> (\w+) when one
//     branch's character set is a subset of another's
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
//   - Automatic possessive promotion: greedy quantifiers whose character
//     set is disjoint from the following content are promoted to possessive,
//     eliminating futile backtracking in the backtracker engine.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/FixedBitset.h>

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
constexpr int extractBranchLiteralPrefix(
    const auto& ast, int nodeIdx, char* out, int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int len = static_cast<int>(node.literal.size());
      if (len > maxLen) {
        len = maxLen;
      }
      for (int i = 0; i < len; ++i) {
        out[i] = node.literal[i];
      }
      return len;
    }
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
    case NodeKind::Dead:
      return 0;
  }
  return 0;
}

// Extract trailing literal characters from a single branch.
// Returns chars in reverse order (caller must reverse).
constexpr int extractBranchLiteralSuffix(
    const auto& ast, int nodeIdx, char* out, int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int len = static_cast<int>(node.literal.size());
      if (len > maxLen) {
        len = maxLen;
      }
      for (int i = 0; i < len; ++i) {
        out[i] = node.literal[len - 1 - i];
      }
      return len;
    }
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
    case NodeKind::Dead:
      return 0;
  }
  return 0;
}

// --------------------------------------------------------------------------
// stripLiteralPrefix:
// Remove N leading literal characters from a branch.
// Returns the new node index for the branch (may create new nodes).
// Handles multi-char Literal nodes by splitting when partially stripped.
// --------------------------------------------------------------------------

constexpr int stripLiteralPrefix(auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      if (count >= litLen) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }
      AstNode newLit;
      newLit.kind = NodeKind::Literal;
      newLit.literal = node.literal.substr(count);
      return ast.addNode(newLit);
    }
    case NodeKind::Sequence: {
      int children[kMaxAltBranches] = {};
      int childCount = 0;
      int child = node.child_begin;
      while (child >= 0 && childCount < kMaxAltBranches) {
        children[childCount++] = child;
        child = ast.nodes[child].child_end;
      }

      int remaining = count;
      int firstKeptIdx = 0;
      int replacementNode = -1;

      for (int i = 0; i < childCount && remaining > 0; ++i) {
        const auto& cnode = ast.nodes[children[i]];
        if (cnode.kind == NodeKind::Literal) {
          int litLen = static_cast<int>(cnode.literal.size());
          if (remaining >= litLen) {
            remaining -= litLen;
            firstKeptIdx = i + 1;
          } else {
            AstNode newLit;
            newLit.kind = NodeKind::Literal;
            newLit.literal = cnode.literal.substr(remaining);
            replacementNode = ast.addNode(newLit);
            firstKeptIdx = i;
            remaining = 0;
          }
        } else if (cnode.kind == NodeKind::Group && !cnode.capturing) {
          int stripped = stripLiteralPrefix(ast, children[i], remaining);
          if (stripped != children[i]) {
            children[i] = stripped;
          }
          remaining = 0;
          firstKeptIdx = i;
        } else {
          break;
        }
      }

      if (firstKeptIdx >= childCount) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }

      if (firstKeptIdx == 0 && replacementNode < 0 && remaining == count) {
        return nodeIdx;
      }

      // Build new child list
      int newChildren[kMaxAltBranches] = {};
      int newChildCount = 0;
      if (replacementNode >= 0) {
        newChildren[newChildCount++] = replacementNode;
        for (int i = firstKeptIdx + 1; i < childCount; ++i) {
          newChildren[newChildCount++] = children[i];
        }
      } else {
        for (int i = firstKeptIdx; i < childCount; ++i) {
          newChildren[newChildCount++] = children[i];
        }
      }

      if (newChildCount == 0) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }

      if (newChildCount == 1) {
        ast.nodes[newChildren[0]].child_end = -1;
        return newChildren[0];
      }

      for (int i = 0; i < newChildCount - 1; ++i) {
        ast.nodes[newChildren[i]].child_end = newChildren[i + 1];
      }
      ast.nodes[newChildren[newChildCount - 1]].child_end = -1;

      AstNode seq;
      seq.kind = NodeKind::Sequence;
      seq.child_begin = newChildren[0];
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
    case NodeKind::Dead:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// stripLiteralSuffix:
// Remove N trailing literal characters from a branch.
// Handles multi-char Literal nodes by splitting when partially stripped.
// --------------------------------------------------------------------------

constexpr int stripLiteralSuffix(auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      if (count >= litLen) {
        AstNode empty;
        empty.kind = NodeKind::Empty;
        return ast.addNode(empty);
      }
      AstNode newLit;
      newLit.kind = NodeKind::Literal;
      newLit.literal = node.literal.substr(0, litLen - count);
      return ast.addNode(newLit);
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
          int litLen = static_cast<int>(ast.nodes[lastIdx].literal.size());
          if (remaining >= litLen) {
            --keepCount;
            remaining -= litLen;
          } else {
            AstNode newLit;
            newLit.kind = NodeKind::Literal;
            newLit.literal =
                ast.nodes[lastIdx].literal.substr(0, litLen - remaining);
            children[keepCount - 1] = ast.addNode(newLit);
            remaining = 0;
          }
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

      if (keepCount == childCount && remaining == count) {
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
    case NodeKind::Dead:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// factorAlternationPrefix:
// Group branches by common literal prefix and factor each group.
// Recurses into factored groups for nested prefixes.
// Creates multi-char Literal nodes for factored prefixes.
// --------------------------------------------------------------------------

constexpr int factorAlternationPrefix(auto& ast, int altNodeIdx) noexcept {
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

    if (prefixLens[i] > 0) {
      for (int j = i + 1; j < branchCount; ++j) {
        if (processed.test(j)) {
          continue;
        }
        if (prefixLens[j] > 0 && prefixes[j][0] == prefixes[i][0]) {
          group[groupCount++] = j;
        }
      }
    }

    if (groupCount < 2) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

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
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    for (int g = 0; g < groupCount; ++g) {
      processed.set(group[g]);
    }
    anyFactored = true;

    // 4. Strip prefix from each branch in the group
    int strippedBranches[kMaxAltBranches] = {};
    for (int g = 0; g < groupCount; ++g) {
      strippedBranches[g] =
          stripLiteralPrefix(ast, branches[group[g]], commonLen);
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

    innerAltIdx = factorAlternationPrefix(ast, innerAltIdx);

    AstNode grp;
    grp.kind = NodeKind::Group;
    grp.capturing = false;
    grp.child_begin = innerAltIdx;
    grp.child_end = innerAltIdx;
    int grpIdx = ast.addNode(grp);

    // Create one multi-char Literal for the factored prefix
    AstNode lit;
    lit.kind = NodeKind::Literal;
    lit.literal = ast.appendLiteral(prefixes[group[0]], commonLen);
    int litIdx = ast.addNode(lit);

    ast.nodes[litIdx].child_end = grpIdx;
    ast.nodes[grpIdx].child_end = -1;

    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = litIdx;
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
// factorAlternationSuffix: same as prefix but for trailing literals.
// Creates multi-char Literal nodes for factored suffixes.
// --------------------------------------------------------------------------

constexpr int factorAlternationSuffix(auto& ast, int altNodeIdx) noexcept {
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

    if (suffixLens[i] > 0) {
      for (int j = i + 1; j < branchCount; ++j) {
        if (processed.test(j)) {
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

    for (int g = 0; g < groupCount; ++g) {
      processed.set(group[g]);
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

    // Create one multi-char Literal for the factored suffix
    // (reverse the reversed suffix)
    char suffChars[kMaxSuffixLen] = {};
    for (int c = 0; c < commonLen; ++c) {
      suffChars[c] = suffixes[group[0]][commonLen - 1 - c];
    }
    AstNode lit;
    lit.kind = NodeKind::Literal;
    lit.literal = ast.appendLiteral(suffChars, commonLen);
    int litIdx = ast.addNode(lit);
    ast.nodes[litIdx].child_end = -1;

    // Sequence: group(alternation) -> suffix literal
    ast.nodes[grpIdx].child_end = litIdx;

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

constexpr void stripRootLiteralPrefix(auto& ast) noexcept {
  if (ast.has_lookbehind || ast.root < 0) {
    return;
  }

  // Maximum prefix length: buffer size minus suffix length
  int maxPrefix = ast.literal_buf_size - ast.suffix_len;
  if (maxPrefix <= 0) {
    return;
  }

  // Determine prefix length (capped by available buffer)
  char prefix[256] = {};
  int cap = maxPrefix < 256 ? maxPrefix : 256;
  int prefixLen = extractBranchLiteralPrefix(ast, ast.root, prefix, cap);

  if (prefixLen == 0) {
    return;
  }

  // Store prefix at front of literal_buf
  for (int i = 0; i < prefixLen; ++i) {
    ast.literal_buf[i] = prefix[i];
  }
  ast.prefix_len = prefixLen;

  // Strip from AST
  ast.root = stripLiteralPrefix(ast, ast.root, prefixLen);
}

// --------------------------------------------------------------------------
// extractSuffixChars: helper for extractRootLiteralSuffix.
// Walks the AST right-to-left, collecting trailing literal characters
// in reverse order. Handles Group and Repeat (with min_repeat > 0)
// in addition to Literal and Sequence nodes.
// --------------------------------------------------------------------------

constexpr int extractSuffixChars(
    const auto& ast, int nodeIdx, char* out, int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      if (litLen > maxLen) {
        litLen = maxLen;
      }
      for (int i = 0; i < litLen; ++i) {
        out[i] = node.literal[litLen - 1 - i];
      }
      return litLen;
    }
    case NodeKind::Sequence: {
      int children[256] = {};
      int count = 0;
      int child = node.child_begin;
      while (child >= 0 && count < 256) {
        children[count++] = child;
        child = ast.nodes[child].child_end;
      }
      int total = 0;
      for (int i = count - 1; i >= 0 && total < maxLen; --i) {
        int got =
            extractSuffixChars(ast, children[i], out + total, maxLen - total);
        if (got == 0) {
          break;
        }
        total += got;
        if (ast.nodes[children[i]].kind != NodeKind::Literal) {
          break;
        }
      }
      return total;
    }
    case NodeKind::Group:
      return extractSuffixChars(ast, node.child_begin, out, maxLen);
    case NodeKind::Repeat:
      if (node.min_repeat > 0) {
        return extractSuffixChars(ast, node.child_begin, out, maxLen);
      }
      return 0;
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
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
      return 0;
  }
  return 0;
}

// --------------------------------------------------------------------------
// extractRootLiteralSuffix: extract trailing literal characters from the
// AST root and store them at the back of literal_buf.  Must be called
// after stripRootLiteralPrefix so prefix_len is already set.
// --------------------------------------------------------------------------

constexpr void extractRootLiteralSuffix(auto& ast) noexcept {
  if (ast.root < 0 || ast.literal_buf_size <= 0) {
    return;
  }

  int maxSuffix = ast.literal_buf_size - ast.prefix_len;
  if (maxSuffix <= 0) {
    return;
  }

  // Extract suffix chars in reverse order
  char reversed[256] = {};
  int cap = maxSuffix < 256 ? maxSuffix : 256;
  int suffLen = extractSuffixChars(ast, ast.root, reversed, cap);

  if (suffLen == 0) {
    return;
  }

  // Reverse (chars were collected right-to-left)
  for (int i = 0; i < suffLen / 2; ++i) {
    char tmp = reversed[i];
    reversed[i] = reversed[suffLen - 1 - i];
    reversed[suffLen - 1 - i] = tmp;
  }

  // Write suffix at back of literal_buf
  ast.suffix_len = suffLen;
  for (int i = 0; i < suffLen; ++i) {
    ast.literal_buf[ast.literal_buf_size - suffLen + i] = reversed[i];
  }
}

// --------------------------------------------------------------------------
// nodesEqual: recursive structural equality check for AST subtrees.
// --------------------------------------------------------------------------

constexpr bool nodesEqual(const auto& ast, int a, int b) noexcept {
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
    case NodeKind::Dead:
      return true;
    case NodeKind::Literal:
      return na.literal == nb.literal;
    case NodeKind::Anchor:
      return na.anchor == nb.anchor;
    case NodeKind::Backref:
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

constexpr int removeDuplicateBranches(auto& ast, int altNodeIdx) noexcept {
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
// Branch subsumption elimination: remove alternation branches whose match
// set is a strict subset of another branch. E.g. \w+|\d+ → \w+ because
// every string matching \d+ also matches \w+.
// --------------------------------------------------------------------------

struct BranchCharInfo {
  int char_class_index = -1;
  int literal_char = -1;
  bool is_any_char = false;
  bool is_any_byte = false;
  int min_repeat = 1;
  int max_repeat = 1;
  bool valid = false;
};

constexpr BranchCharInfo extractBranchCharInfo(
    const auto& ast, int nodeIdx) noexcept {
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

    case NodeKind::AnyChar:
      info.is_any_char = true;
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
      auto inner = extractBranchCharInfo(ast, node.child_begin);
      if (inner.valid && inner.min_repeat == 1 && inner.max_repeat == 1) {
        inner.min_repeat = node.min_repeat;
        inner.max_repeat = node.max_repeat;
        return inner;
      }
      return info;
    }

    case NodeKind::Group:
      return extractBranchCharInfo(ast, node.child_begin);

    case NodeKind::Sequence:
    case NodeKind::Alternation:
    case NodeKind::Anchor:
    case NodeKind::Empty:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Dead:
      return info;
  }
}

// Check if branch A's character set is a subset of branch B's.
constexpr bool branchCharSetSubsumes(
    const auto& ast,
    const BranchCharInfo& a,
    const BranchCharInfo& b) noexcept {
  // AnyByte subsumes everything
  if (b.is_any_byte) {
    return true;
  }
  // AnyChar subsumes everything except newline-matching sets
  if (b.is_any_char) {
    if (a.is_any_byte) {
      return false;
    }
    if (a.is_any_char) {
      return true;
    }
    // Check that A's char set doesn't include \n
    if (a.literal_char >= 0) {
      return a.literal_char != '\n';
    }
    if (a.char_class_index >= 0) {
      return !charClassTest(
          ast.ranges + ast.char_classes[a.char_class_index].range_offset,
          ast.char_classes[a.char_class_index].range_count,
          '\n');
    }
    return false;
  }

  if (a.is_any_char || a.is_any_byte) {
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

// Check if a subtree contains any capturing groups.
constexpr bool containsCapturingGroup(const auto& ast, int nodeIdx) noexcept {
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
      int child = node.child_begin;
      while (child >= 0) {
        if (containsCapturingGroup(ast, child)) {
          return true;
        }
        child = ast.nodes[child].child_end;
      }
      return false;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return containsCapturingGroup(ast, node.child_begin);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return false;
  }
}

constexpr int eliminateSubsumedBranches(auto& ast, int altNodeIdx) noexcept {
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

  if (ki == 1) {
    ast.nodes[kept[0]].child_end = -1;
    return kept[0];
  }

  for (int i = 0; i < ki - 1; ++i) {
    ast.nodes[kept[i]].child_end = kept[i + 1];
  }
  ast.nodes[kept[ki - 1]].child_end = -1;

  AstNode newAlt;
  newAlt.kind = NodeKind::Alternation;
  newAlt.child_begin = kept[0];
  newAlt.child_end = -1;
  return ast.addNode(newAlt);
}

// --------------------------------------------------------------------------
// mergeCharBranches: merge single-char Literal and non-negated CharClass
// branches in an alternation into a single CharClass node.
// Multi-char Literals are not merged (they represent strings, not chars).
// --------------------------------------------------------------------------

constexpr int mergeCharBranches(auto& ast, int altNodeIdx) noexcept {
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

constexpr int hoistCommonAnchor(auto& ast, int altNodeIdx) noexcept {
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
// Generalized alternation prefix/suffix factoring: extends literal prefix/
// suffix factoring to handle arbitrary AST node types (CharClass, Repeat,
// etc.) using structural equality comparison (nodesEqual).
// --------------------------------------------------------------------------

constexpr int collectBranchChildren(
    const auto& ast, int branchIdx, int* out, int maxCount) noexcept {
  if (branchIdx < 0 || maxCount <= 0) {
    return 0;
  }
  const auto& node = ast.nodes[branchIdx];
  if (node.kind == NodeKind::Sequence) {
    int count = 0;
    int child = node.child_begin;
    while (child >= 0 && count < maxCount) {
      out[count++] = child;
      child = ast.nodes[child].child_end;
    }
    return count;
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    return collectBranchChildren(ast, node.child_begin, out, maxCount);
  }
  out[0] = branchIdx;
  return 1;
}

constexpr int extractCommonNodePrefix(
    const auto& ast, const int* branches, int branchCount) noexcept {
  if (branchCount < 2) {
    return 0;
  }
  int firstChildren[kMaxAltBranches] = {};
  int firstChildCount =
      collectBranchChildren(ast, branches[0], firstChildren, kMaxAltBranches);
  if (firstChildCount == 0) {
    return 0;
  }
  int commonLen = 0;
  for (int pos = 0; pos < firstChildCount; ++pos) {
    bool allEqual = true;
    for (int b = 1; b < branchCount; ++b) {
      int bChildren[kMaxAltBranches] = {};
      int bChildCount =
          collectBranchChildren(ast, branches[b], bChildren, kMaxAltBranches);
      if (pos >= bChildCount ||
          !nodesEqual(ast, firstChildren[pos], bChildren[pos])) {
        allEqual = false;
        break;
      }
    }
    if (!allEqual) {
      break;
    }
    ++commonLen;
  }
  return commonLen;
}

constexpr int extractCommonNodeSuffix(
    const auto& ast, const int* branches, int branchCount) noexcept {
  if (branchCount < 2) {
    return 0;
  }
  int firstChildren[kMaxAltBranches] = {};
  int firstChildCount =
      collectBranchChildren(ast, branches[0], firstChildren, kMaxAltBranches);
  if (firstChildCount == 0) {
    return 0;
  }
  int commonLen = 0;
  for (int rpos = 0; rpos < firstChildCount; ++rpos) {
    int firstPos = firstChildCount - 1 - rpos;
    bool allEqual = true;
    for (int b = 1; b < branchCount; ++b) {
      int bChildren[kMaxAltBranches] = {};
      int bChildCount =
          collectBranchChildren(ast, branches[b], bChildren, kMaxAltBranches);
      int bPos = bChildCount - 1 - rpos;
      if (bPos < 0 ||
          !nodesEqual(ast, firstChildren[firstPos], bChildren[bPos])) {
        allEqual = false;
        break;
      }
    }
    if (!allEqual) {
      break;
    }
    ++commonLen;
  }
  return commonLen;
}

constexpr int stripNodePrefix(auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Sequence) {
    int children[kMaxAltBranches] = {};
    int childCount = 0;
    int child = node.child_begin;
    while (child >= 0 && childCount < kMaxAltBranches) {
      children[childCount++] = child;
      child = ast.nodes[child].child_end;
    }
    if (count >= childCount) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      return ast.addNode(empty);
    }
    int remaining = childCount - count;
    if (remaining == 1) {
      ast.nodes[children[count]].child_end = -1;
      return children[count];
    }
    for (int i = count; i < childCount - 1; ++i) {
      ast.nodes[children[i]].child_end = children[i + 1];
    }
    ast.nodes[children[childCount - 1]].child_end = -1;
    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = children[count];
    return ast.addNode(seq);
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    int inner = stripNodePrefix(ast, node.child_begin, count);
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
  AstNode empty;
  empty.kind = NodeKind::Empty;
  return ast.addNode(empty);
}

constexpr int stripNodeSuffix(auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Sequence) {
    int children[kMaxAltBranches] = {};
    int childCount = 0;
    int child = node.child_begin;
    while (child >= 0 && childCount < kMaxAltBranches) {
      children[childCount++] = child;
      child = ast.nodes[child].child_end;
    }
    int keepCount = childCount - count;
    if (keepCount <= 0) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      return ast.addNode(empty);
    }
    if (keepCount == 1) {
      ast.nodes[children[0]].child_end = -1;
      return children[0];
    }
    for (int i = 0; i < keepCount - 1; ++i) {
      ast.nodes[children[i]].child_end = children[i + 1];
    }
    ast.nodes[children[keepCount - 1]].child_end = -1;
    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = children[0];
    return ast.addNode(seq);
  }
  if (node.kind == NodeKind::Group && !node.capturing) {
    int inner = stripNodeSuffix(ast, node.child_begin, count);
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
  AstNode empty;
  empty.kind = NodeKind::Empty;
  return ast.addNode(empty);
}

constexpr int factorGeneralizedPrefix(auto& ast, int altNodeIdx) noexcept {
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

  int firstChild[kMaxAltBranches] = {};
  for (int i = 0; i < branchCount; ++i) {
    int children[kMaxAltBranches] = {};
    int childCount =
        collectBranchChildren(ast, branches[i], children, kMaxAltBranches);
    firstChild[i] = childCount > 0 ? children[0] : -1;
  }

  FixedBitset<kMaxAltBranches> processed;
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed.test(i)) {
      continue;
    }
    if (firstChild[i] < 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;
    for (int j = i + 1; j < branchCount; ++j) {
      if (processed.test(j) || firstChild[j] < 0) {
        continue;
      }
      if (nodesEqual(ast, firstChild[i], firstChild[j])) {
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
    int commonLen = extractCommonNodePrefix(ast, groupBranches, groupCount);
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
      strippedBranches[g] = stripNodePrefix(ast, branches[group[g]], commonLen);
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

    innerAltIdx = mergeCharBranches(ast, innerAltIdx);
    innerAltIdx = factorAlternationPrefix(ast, innerAltIdx);
    innerAltIdx = factorAlternationSuffix(ast, innerAltIdx);
    innerAltIdx = factorGeneralizedPrefix(ast, innerAltIdx);

    AstNode grp;
    grp.kind = NodeKind::Group;
    grp.capturing = false;
    grp.child_begin = innerAltIdx;
    grp.child_end = innerAltIdx;
    int grpIdx = ast.addNode(grp);

    int prefixChildren[kMaxAltBranches] = {};
    collectBranchChildren(
        ast, branches[group[0]], prefixChildren, kMaxAltBranches);

    int prevCopyIdx = -1;
    int firstCopyIdx = -1;
    for (int p = 0; p < commonLen; ++p) {
      AstNode copy = ast.nodes[prefixChildren[p]];
      int copyIdx = ast.addNode(copy);
      if (p == 0) {
        firstCopyIdx = copyIdx;
      }
      if (prevCopyIdx >= 0) {
        ast.nodes[prevCopyIdx].child_end = copyIdx;
      }
      prevCopyIdx = copyIdx;
    }
    ast.nodes[prevCopyIdx].child_end = grpIdx;
    ast.nodes[grpIdx].child_end = -1;

    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = firstCopyIdx;
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

constexpr int factorGeneralizedSuffix(auto& ast, int altNodeIdx) noexcept {
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

  int lastChild[kMaxAltBranches] = {};
  for (int i = 0; i < branchCount; ++i) {
    int children[kMaxAltBranches] = {};
    int childCount =
        collectBranchChildren(ast, branches[i], children, kMaxAltBranches);
    lastChild[i] = childCount > 0 ? children[childCount - 1] : -1;
  }

  FixedBitset<kMaxAltBranches> processed;
  int newBranches[kMaxAltBranches] = {};
  int newBranchCount = 0;
  bool anyFactored = false;

  for (int i = 0; i < branchCount; ++i) {
    if (processed.test(i)) {
      continue;
    }
    if (lastChild[i] < 0) {
      newBranches[newBranchCount++] = branches[i];
      continue;
    }

    int group[kMaxAltBranches] = {};
    int groupCount = 0;
    group[groupCount++] = i;
    for (int j = i + 1; j < branchCount; ++j) {
      if (processed.test(j) || lastChild[j] < 0) {
        continue;
      }
      if (nodesEqual(ast, lastChild[i], lastChild[j])) {
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
    int commonLen = extractCommonNodeSuffix(ast, groupBranches, groupCount);
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
      strippedBranches[g] = stripNodeSuffix(ast, branches[group[g]], commonLen);
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

    innerAltIdx = mergeCharBranches(ast, innerAltIdx);
    innerAltIdx = factorAlternationPrefix(ast, innerAltIdx);
    innerAltIdx = factorAlternationSuffix(ast, innerAltIdx);
    innerAltIdx = factorGeneralizedSuffix(ast, innerAltIdx);

    AstNode grp;
    grp.kind = NodeKind::Group;
    grp.capturing = false;
    grp.child_begin = innerAltIdx;
    grp.child_end = innerAltIdx;
    int grpIdx = ast.addNode(grp);

    int firstBranchChildren[kMaxAltBranches] = {};
    int firstBranchChildCount = collectBranchChildren(
        ast, branches[group[0]], firstBranchChildren, kMaxAltBranches);
    int suffixStart = firstBranchChildCount - commonLen;

    int prevIdx = grpIdx;
    for (int p = 0; p < commonLen; ++p) {
      AstNode copy = ast.nodes[firstBranchChildren[suffixStart + p]];
      int copyIdx = ast.addNode(copy);
      ast.nodes[prevIdx].child_end = copyIdx;
      prevIdx = copyIdx;
    }
    ast.nodes[prevIdx].child_end = -1;

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
// optimizeNode: recursively optimize a single AST node.
// --------------------------------------------------------------------------

constexpr int optimizeNode(auto& ast, int nodeIdx) noexcept {
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

      // Duplicate elimination, subsumption, char merging, anchor hoisting,
      // then factoring
      int result = removeDuplicateBranches(ast, nodeIdx);
      result = eliminateSubsumedBranches(ast, result);
      result = mergeCharBranches(ast, result);
      result = hoistCommonAnchor(ast, result);
      result = factorGeneralizedPrefix(ast, result);
      result = factorGeneralizedSuffix(ast, result);
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
          case NodeKind::Dead:
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
      // Quantified zero-width simplification: repeating a zero-width
      // match doesn't advance the position.
      if (node.child_begin >= 0 && ast.nodes[node.child_begin].isZeroWidth()) {
        if (node.min_repeat == 0) {
          // \b* → \b?, ^{0,n} → ^?
          node.max_repeat = 1;
        } else {
          // \b+ → \b, ^{2,5} → ^
          return node.child_begin;
        }
      }
      // Nested quantifier flattening.
      if (!node.possessive) {
        int childIdx = node.child_begin;
        const auto& child = ast.nodes[childIdx];
        if (child.kind == NodeKind::Group) {
          int gcIdx = child.child_begin;
          if (gcIdx >= 0) {
            const auto& gc = ast.nodes[gcIdx];
            if (gc.kind == NodeKind::Repeat && gc.greedy == node.greedy &&
                !gc.possessive) {
              int charIdx = gc.child_begin;
              if (charIdx >= 0) {
                auto ck = ast.nodes[charIdx].kind;
                if (ck == NodeKind::Literal || ck == NodeKind::CharClass ||
                    ck == NodeKind::AnyChar || ck == NodeKind::AnyByte) {
                  if (gc.max_repeat == -1 && node.min_repeat == 1 &&
                      gc.min_repeat >= 1) {
                    return node.child_begin;
                  }
                  if (!child.capturing) {
                    AstNode flat;
                    flat.kind = NodeKind::Repeat;
                    flat.min_repeat = gc.min_repeat * node.min_repeat;
                    flat.max_repeat =
                        (gc.max_repeat == -1 || node.max_repeat == -1)
                        ? -1
                        : gc.max_repeat * node.max_repeat;
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
          if (child.greedy == node.greedy && !child.possessive) {
            int charIdx = child.child_begin;
            if (charIdx >= 0) {
              auto ck = ast.nodes[charIdx].kind;
              if (ck == NodeKind::Literal || ck == NodeKind::CharClass ||
                  ck == NodeKind::AnyChar || ck == NodeKind::AnyByte) {
                if (child.max_repeat == -1 && node.min_repeat == 1 &&
                    child.min_repeat >= 1) {
                  return childIdx;
                }
                AstNode flat;
                flat.kind = NodeKind::Repeat;
                flat.min_repeat = child.min_repeat * node.min_repeat;
                flat.max_repeat =
                    (child.max_repeat == -1 || node.max_repeat == -1)
                    ? -1
                    : child.max_repeat * node.max_repeat;
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
      if (node.char_class_index >= 0) {
        const auto& cc = ast.char_classes[node.char_class_index];
        if (cc.range_count == 1) {
          const auto& r = ast.ranges[cc.range_offset];
          if (r.lo == r.hi) {
            AstNode lit;
            lit.kind = NodeKind::Literal;
            char c = static_cast<char>(r.lo);
            lit.literal = ast.appendLiteralChar(c);
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
    case NodeKind::Dead:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// simplifyEmptyAlternation: convert alternations containing Empty branches
// (or branches that are bare Repeat{min_repeat=0}) into optional
// (Repeat{0,1}) quantifiers.
// --------------------------------------------------------------------------

constexpr int simplifyEmptyAlternation(auto& ast, int nodeIdx) noexcept {
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
    case NodeKind::Dead:
      break;
  }

  if (node.kind == NodeKind::Alternation) {
    int branch = node.child_begin;
    if (branch < 0) {
      return nodeIdx;
    }

    // Count explicit Empty branches and bare Repeat{min_repeat=0} branches.
    int totalBranches = 0;
    int explicitEmptyCount = 0;
    int implicitEmptyCount = 0;
    bool firstIsExplicitEmpty = (ast.nodes[branch].kind == NodeKind::Empty);

    {
      int b = branch;
      while (b >= 0) {
        if (ast.nodes[b].kind == NodeKind::Empty) {
          explicitEmptyCount++;
        } else if (
            ast.nodes[b].kind == NodeKind::Repeat &&
            ast.nodes[b].min_repeat == 0) {
          implicitEmptyCount++;
        }
        totalBranches++;
        b = ast.nodes[b].child_end;
      }
    }

    if (explicitEmptyCount == 0 && implicitEmptyCount == 0) {
      return nodeIdx;
    }

    int nonEmptyBranchCount = totalBranches - explicitEmptyCount;

    // All branches are explicit Empty → single Empty node.
    if (nonEmptyBranchCount == 0 && implicitEmptyCount == 0) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      return ast.addNode(empty);
    }

    // Promote implicit-empty branches and re-link, skipping explicit Empties.
    int newFirst = -1;
    int newLast = -1;
    int keptCount = 0;

    {
      int b = branch;
      while (b >= 0) {
        int next = ast.nodes[b].child_end;
        if (ast.nodes[b].kind == NodeKind::Empty) {
          // Skip explicit Empty branches.
        } else {
          if (ast.nodes[b].kind == NodeKind::Repeat &&
              ast.nodes[b].min_repeat == 0) {
            ast.nodes[b].min_repeat = 1;
          }
          if (newLast >= 0) {
            ast.nodes[newLast].child_end = b;
          } else {
            newFirst = b;
          }
          newLast = b;
          keptCount++;
        }
        b = next;
      }
      if (newLast >= 0) {
        ast.nodes[newLast].child_end = -1;
      }
    }

    // First explicit Empty → lazy; otherwise → greedy.
    bool greedy = !firstIsExplicitEmpty;

    int innerNode;
    if (keptCount == 1) {
      innerNode = newFirst;
    } else {
      // Reuse the alternation node with updated children.
      node.child_begin = newFirst;
      innerNode = nodeIdx;
    }

    AstNode repeat;
    repeat.kind = NodeKind::Repeat;
    repeat.min_repeat = 0;
    repeat.max_repeat = 1;
    repeat.greedy = greedy;
    repeat.child_begin = innerNode;
    repeat.child_end = innerNode;
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
// Automatic possessive promotion: promote greedy quantifiers to possessive
// when backtracking into them can never lead to a successful match.
// A greedy quantifier can be safely promoted when its character set is
// disjoint from the first-char set of the following content.
// --------------------------------------------------------------------------

// Extract the set of characters that could be the first character matched
// by the subtree rooted at nodeIdx. Returns accepts_all=true when the
// set cannot be determined precisely. Uses the existing FirstCharFilter
// from CharClass.h.
constexpr FirstCharFilter extractFollowFilter(
    const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return {.accepts_all = true};
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      if (!node.literal.empty()) {
        FirstCharFilter f;
        f.accepts_all = false;
        f.addChar(static_cast<unsigned char>(node.literal[0]));
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

    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
      return {.accepts_all = true};

    case NodeKind::Group:
      return extractFollowFilter(ast, node.child_begin);

    case NodeKind::Sequence: {
      int child = node.child_begin;
      while (child >= 0) {
        if (ast.nodes[child].isZeroWidth()) {
          child = ast.nodes[child].child_end;
          continue;
        }
        return extractFollowFilter(ast, child);
      }
      return {.accepts_all = true};
    }

    case NodeKind::Alternation: {
      int child = node.child_begin;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f = extractFollowFilter(ast, child);
      if (f.accepts_all) {
        return f;
      }
      int next = ast.nodes[child].child_end;
      while (next >= 0) {
        auto alt = extractFollowFilter(ast, next);
        if (alt.accepts_all) {
          return alt;
        }
        f.mergeFrom(alt.ranges, alt.range_count);
        if (f.accepts_all) {
          return f;
        }
        next = ast.nodes[next].child_end;
      }
      return f;
    }

    case NodeKind::Repeat:
      if (node.min_repeat > 0) {
        return extractFollowFilter(ast, node.child_begin);
      }
      return {.accepts_all = true};

    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::Dead:
      return {.accepts_all = true};
  }
  return {.accepts_all = true};
}

constexpr bool isSimpleCharTest(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  auto kind = ast.nodes[nodeIdx].kind;
  if (kind == NodeKind::Literal) {
    return ast.nodes[nodeIdx].literal.size() == 1;
  }
  return kind == NodeKind::CharClass || kind == NodeKind::AnyChar ||
      kind == NodeKind::AnyByte;
}

constexpr bool isDisjointFromFilter(
    const auto& ast,
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

constexpr void promoteToPossessive(auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence: {
      int child = node.child_begin;
      while (child >= 0) {
        int nextSibling = ast.nodes[child].child_end;

        if (ast.nodes[child].kind == NodeKind::Repeat &&
            !ast.nodes[child].possessive) {
          int inner = ast.nodes[child].child_begin;
          if (isSimpleCharTest(ast, inner)) {
            FirstCharFilter followFilter;
            followFilter.accepts_all = false;
            int sibling = nextSibling;
            while (sibling >= 0) {
              auto sibFilter = extractFollowFilter(ast, sibling);
              if (sibFilter.accepts_all) {
                followFilter.accepts_all = true;
                break;
              }

              followFilter.mergeFrom(sibFilter.ranges, sibFilter.range_count);
              if (followFilter.accepts_all) {
                break;
              }

              if (!isPossiblyZeroWidth(ast, sibling)) {
                break;
              }

              sibling = ast.nodes[sibling].child_end;
            }

            if (!followFilter.accepts_all &&
                isDisjointFromFilter(ast, inner, followFilter)) {
              ast.nodes[child].possessive = true;
            }
          }
        }

        promoteToPossessive(ast, child);
        child = nextSibling;
      }
      break;
    }
    case NodeKind::Alternation: {
      int child = node.child_begin;
      while (child >= 0) {
        promoteToPossessive(ast, child);
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      promoteToPossessive(ast, node.child_begin);
      break;
    case NodeKind::Literal:
    case NodeKind::CharClass:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      break;
  }
}

// --------------------------------------------------------------------------
// computeBacktrackSafe: determine if the pattern is provably linear-time.
// --------------------------------------------------------------------------

constexpr bool computeBacktrackSafe(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Repeat: {
      if (node.possessive) {
        return computeBacktrackSafe(ast, node.child_begin);
      }
      int innerIdx = node.child_begin;
      if (innerIdx < 0) {
        return true;
      }
      auto innerKind = ast.nodes[innerIdx].kind;
      int effectiveIdx = innerIdx;
      auto effectiveKind = innerKind;
      if (innerKind == NodeKind::Group) {
        effectiveIdx = ast.nodes[innerIdx].child_begin;
        if (effectiveIdx < 0) {
          return false;
        }
        effectiveKind = ast.nodes[effectiveIdx].kind;
      }
      if (effectiveKind == NodeKind::Literal ||
          effectiveKind == NodeKind::CharClass ||
          effectiveKind == NodeKind::AnyChar ||
          effectiveKind == NodeKind::AnyByte) {
        return true;
      }
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
    case NodeKind::Dead:
      return true;
    case NodeKind::Backref:
      return false;
  }
  return true;
}

// --------------------------------------------------------------------------
// applyMultilineFlag / applyDotAllFlag — unchanged from before.
// --------------------------------------------------------------------------

constexpr void applyMultilineFlag(auto& ast, int nodeIdx) noexcept {
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

constexpr void applyDotAllFlag(auto& ast, int nodeIdx, bool dotAll) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::AnyChar) {
    if (dotAll) {
      node.kind = NodeKind::AnyByte;
    } else {
      auto rs = makeDotRanges();
      node.char_class_index = ast.addCharClass(rs);
      node.kind = NodeKind::CharClass;
    }
  }
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int child = node.child_begin;
    while (child >= 0) {
      applyDotAllFlag(ast, child, dotAll);
      child = ast.nodes[child].child_end;
    }
  } else if (
      node.kind == NodeKind::Group || node.kind == NodeKind::Repeat ||
      node.kind == NodeKind::Lookahead || node.kind == NodeKind::NegLookahead ||
      node.kind == NodeKind::Lookbehind ||
      node.kind == NodeKind::NegLookbehind) {
    applyDotAllFlag(ast, node.child_begin, dotAll);
  }
}

// --------------------------------------------------------------------------
// findDiscriminatorEntry: search a ChunkedBuffer for an entry at the
// given offset. Entries are sorted, so we can stop early.
// --------------------------------------------------------------------------

constexpr const DiscriminatorEntry* findDiscriminatorEntry(
    const ChunkedBuffer<DiscriminatorEntry>* buf, int offset) noexcept {
  if (!buf) {
    return nullptr;
  }
  const auto* block = &buf->first_;
  while (block) {
    for (int i = 0; i < block->count; ++i) {
      if (block->data[i].offset == offset) {
        return &block->data[i];
      }
      if (block->data[i].offset > offset) {
        return nullptr;
      }
    }
    block = block->next;
  }
  return nullptr;
}

// --------------------------------------------------------------------------
// BranchCharSet: character set for a single direct branch at a given
// offset. Built from a Literal (single char), CharClass (AST ranges), or
// an inner alternation's precomputed DiscriminatorEntry.
// --------------------------------------------------------------------------

struct BranchCharSet {
  static constexpr int kMaxRanges = DiscriminatorEntry::kMaxRanges;
  CharRange ranges[kMaxRanges] = {};
  int range_count = 0;
  bool valid = false;
};

constexpr bool areRangeSetsDisjoint(
    const CharRange* a, int aCount, const CharRange* b, int bCount) noexcept {
  for (int i = 0; i < aCount; ++i) {
    for (int j = 0; j < bCount; ++j) {
      if (a[i].lo <= b[j].hi && b[j].lo <= a[i].hi) {
        return false;
      }
    }
  }
  return true;
}

// --------------------------------------------------------------------------
// getBranchCharSet: get the character set for a direct branch at offset.
// For Literal/CharClass, reads from the AST. For inner alternations,
// reads from the precomputed valid_discriminators ChunkedBuffer.
// --------------------------------------------------------------------------

constexpr BranchCharSet getBranchCharSet(
    const auto& ast, int nodeIdx, int offset) noexcept {
  BranchCharSet result;
  if (nodeIdx < 0 || offset < 0) {
    return result;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Literal:
      if (offset < static_cast<int>(n.literal.size())) {
        auto c = static_cast<unsigned char>(n.literal[offset]);
        result.ranges[0] = {c, c};
        result.range_count = 1;
        result.valid = true;
      }
      break;
    case NodeKind::CharClass:
      if (offset == 0) {
        const auto& cc = ast.char_classes[n.char_class_index];
        for (int i = 0; i < cc.range_count &&
             result.range_count < BranchCharSet::kMaxRanges;
             ++i) {
          result.ranges[result.range_count++] = ast.ranges[cc.range_offset + i];
        }
        result.valid = (result.range_count > 0);
      }
      break;
    case NodeKind::Group:
      return getBranchCharSet(ast, n.child_begin, offset);
    case NodeKind::Alternation: {
      auto* entry = findDiscriminatorEntry(n.valid_discriminators, offset);
      if (entry) {
        for (int i = 0; i < entry->range_count &&
             result.range_count < BranchCharSet::kMaxRanges;
             ++i) {
          result.ranges[result.range_count++] = entry->ranges[i];
        }
        result.valid = true;
      }
      break;
    }
    case NodeKind::Sequence: {
      int child = n.child_begin;
      int accumulated = 0;
      while (child >= 0) {
        int fw = computeFixedWidth(ast, child);
        if (fw < 0) {
          int prefix = computeFixedWidthPrefix(ast, child);
          if (offset < accumulated + prefix) {
            return getBranchCharSet(ast, child, offset - accumulated);
          }
          break;
        }
        if (offset < accumulated + fw) {
          return getBranchCharSet(ast, child, offset - accumulated);
        }
        accumulated += fw;
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_begin);
        if (innerFW > 0) {
          if (offset < innerFW * n.min_repeat) {
            return getBranchCharSet(ast, n.child_begin, offset % innerFW);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_begin);
          if (offset < innerPrefix) {
            return getBranchCharSet(ast, n.child_begin, offset);
          }
        }
      }
      break;
    }
    case NodeKind::Empty:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::Dead:
      break;
  }
  return result;
}

// --------------------------------------------------------------------------
// isValidDiscriminatorAt: check if offset produces pairwise disjoint
// character sets across all direct branches. Uses getBranchCharSet which
// reads from inner alternations' precomputed entries (no re-flattening).
// --------------------------------------------------------------------------

constexpr bool isValidDiscriminatorAt(
    const auto& ast, int nodeIdx, int offset) noexcept {
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind != NodeKind::Alternation) {
    return false;
  }
  BranchCharSet branchSets[kMaxAltBranches];
  int branchCount = 0;
  int child = node.child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branchSets[branchCount] = getBranchCharSet(ast, child, offset);
    if (!branchSets[branchCount].valid) {
      return false;
    }
    ++branchCount;
    child = ast.nodes[child].child_end;
  }
  for (int i = 0; i < branchCount; ++i) {
    for (int j = i + 1; j < branchCount; ++j) {
      if (!areRangeSetsDisjoint(
              branchSets[i].ranges,
              branchSets[i].range_count,
              branchSets[j].ranges,
              branchSets[j].range_count)) {
        return false;
      }
    }
  }
  return true;
}

// --------------------------------------------------------------------------
// Pass 1 (bottom-up): compute valid_discriminators ChunkedBuffer for each
// alternation. Each entry stores [offset, union_char_set] for offsets
// where all branches have pairwise disjoint characters. Recurses into
// children first (post-order) so inner alternations' entries are available
// when the parent queries them via getBranchCharSet.
// --------------------------------------------------------------------------

constexpr void computeDiscriminatorsBottomUp(auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_begin;
      while (child >= 0) {
        computeDiscriminatorsBottomUp(ast, child);
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      computeDiscriminatorsBottomUp(ast, node.child_begin);
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

  if (node.kind != NodeKind::Alternation) {
    return;
  }

  int branchCount = 0;
  int branchIndices[kMaxAltBranches] = {};
  int minFixedPrefix = 2147483647;
  int child = node.child_begin;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branchIndices[branchCount] = child;
    int prefix = computeFixedWidthPrefix(ast, child);
    if (prefix < minFixedPrefix) {
      minFixedPrefix = prefix;
    }
    ++branchCount;
    child = ast.nodes[child].child_end;
  }

  if (branchCount < 2 || minFixedPrefix <= 0) {
    return;
  }

  auto* buf = new ChunkedBuffer<DiscriminatorEntry>();
  for (int offset = 0; offset < minFixedPrefix; ++offset) {
    if (!isValidDiscriminatorAt(ast, nodeIdx, offset)) {
      continue;
    }
    DiscriminatorEntry entry;
    entry.offset = offset;
    for (int i = 0; i < branchCount; ++i) {
      auto bs = getBranchCharSet(ast, branchIndices[i], offset);
      for (int r = 0; r < bs.range_count &&
           entry.range_count < DiscriminatorEntry::kMaxRanges;
           ++r) {
        entry.ranges[entry.range_count++] = bs.ranges[r];
      }
    }
    for (int i = 1; i < entry.range_count; ++i) {
      auto key = entry.ranges[i];
      int j = i - 1;
      while (j >= 0 && entry.ranges[j].lo > key.lo) {
        entry.ranges[j + 1] = entry.ranges[j];
        --j;
      }
      entry.ranges[j + 1] = key;
    }
    buf->append(&entry, 1);
  }

  if (buf->total_count_ > 0) {
    node.valid_discriminators = buf;
  } else {
    delete buf;
  }
}

// --------------------------------------------------------------------------
// Pass 2 (top-down): select discriminator_offset for each alternation.
// Alternations with ≥3 branches pick the earliest offset from their
// valid_discriminators. A forced offset from an outer alternation is
// accepted if it appears in the inner's entries (ChunkedBuffer lookup).
// Forced offsets allow ≥2 branch alternations to benefit. The forced
// offset is adjusted through sequences by subtracting accumulated fixed
// widths of preceding children.
// --------------------------------------------------------------------------

constexpr void propagateDiscriminatorsTopDown(
    auto& ast, int nodeIdx, int forced_offset) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Alternation: {
      if (forced_offset >= 0 &&
          findDiscriminatorEntry(node.valid_discriminators, forced_offset)) {
        node.discriminator_offset = forced_offset;
      } else if (node.valid_discriminators) {
        int branchCount = 0;
        int child = node.child_begin;
        while (child >= 0) {
          ++branchCount;
          child = ast.nodes[child].child_end;
        }
        if (branchCount >= 3) {
          const auto* block = &node.valid_discriminators->first_;
          while (block) {
            if (block->count > 0) {
              node.discriminator_offset = block->data[0].offset;
              break;
            }
            block = block->next;
          }
        }
      }
      int chosenOffset = node.discriminator_offset;
      int child = node.child_begin;
      while (child >= 0) {
        propagateDiscriminatorsTopDown(ast, child, chosenOffset);
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Sequence: {
      int child = node.child_begin;
      int accumulated = 0;
      while (child >= 0) {
        int adjusted = forced_offset >= 0 ? forced_offset - accumulated : -1;
        if (adjusted < 0) {
          adjusted = -1;
        }
        propagateDiscriminatorsTopDown(ast, child, adjusted);
        int fw = computeFixedWidth(ast, child);
        if (fw < 0) {
          child = ast.nodes[child].child_end;
          while (child >= 0) {
            propagateDiscriminatorsTopDown(ast, child, -1);
            child = ast.nodes[child].child_end;
          }
          break;
        }
        accumulated += fw;
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Group:
      propagateDiscriminatorsTopDown(ast, node.child_begin, forced_offset);
      break;
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      propagateDiscriminatorsTopDown(ast, node.child_begin, -1);
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
}

// --------------------------------------------------------------------------
// freeDiscriminatorSets: walk the AST and delete all valid_discriminators
// ChunkedBuffers. Must be called after the top-down pass and before
// compact(), since heap allocations cannot persist across constexpr
// contexts.
// --------------------------------------------------------------------------

constexpr void freeDiscriminatorSets(auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];

  if (node.valid_discriminators) {
    delete node.valid_discriminators;
    node.valid_discriminators = nullptr;
  }

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_begin;
      while (child >= 0) {
        freeDiscriminatorSets(ast, child);
        child = ast.nodes[child].child_end;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      freeDiscriminatorSets(ast, node.child_begin);
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
}

// --------------------------------------------------------------------------
// optimizeAst: top-level entry point.
// --------------------------------------------------------------------------

constexpr int optimizeAst(auto& ast, int rootIdx) noexcept {
  int result = optimizeNode(ast, rootIdx);
  result = simplifyEmptyAlternation(ast, result);

  ast.root = result;

  promoteToPossessive(ast, ast.root);

  computeDiscriminatorsBottomUp(ast, ast.root);
  propagateDiscriminatorsTopDown(ast, ast.root, -1);
  freeDiscriminatorSets(ast, ast.root);

  stripRootLiteralPrefix(ast);
  extractRootLiteralSuffix(ast);

  ast.backtrack_safe = computeBacktrackSafe(ast, ast.root);

  return ast.root;
}

} // namespace detail
} // namespace regex
} // namespace folly
