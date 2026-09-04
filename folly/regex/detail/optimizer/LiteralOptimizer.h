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

// Literal prefix/suffix extraction, stripping, and root prefix management
// for the AST optimizer. These functions handle extracting leading/trailing
// literal characters from branches, stripping them from the AST, and
// managing root-level literal prefix extraction for memcmp-based matching.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/optimizer/AstHelpers.h>

namespace folly::regex::detail {

// --------------------------------------------------------------------------
// Helpers for walking AST nodes to extract leading/trailing literal chars.
// --------------------------------------------------------------------------

// Extract leading literal characters from a single branch of an alternation.
// Stops at non-Literal nodes (CharClass, Alternation, optional
// Repeat, etc.).  Returns the number of literal chars extracted.
constexpr int extractBranchLiteralPrefix(
    const ReadOnlyAst auto& ast, int nodeIdx, char* out, int maxLen) noexcept {
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
    case NodeKind::Group:
      if (node.capturing && ast.has_backref) {
        return 0;
      }
      return extractBranchLiteralPrefix(ast, node.child_first, out, maxLen);
    case NodeKind::Sequence: {
      int total = 0;
      int child = node.child_first;
      while (child >= 0 && total < maxLen) {
        int got =
            extractBranchLiteralPrefix(ast, child, out + total, maxLen - total);
        if (got == 0) {
          break;
        }
        total += got;
        int fw = computeFixedWidth(ast, child);
        if (fw < 0 || fw != got) {
          break;
        }
        child = ast.nodes[child].next_sibling;
      }
      return total;
    }
    case NodeKind::Repeat:
    case NodeKind::Anchor:
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return 0;
  }
  return 0;
}

// Extract trailing literal characters from a single branch.
// Returns chars in reverse order (caller must reverse).
constexpr int extractBranchLiteralSuffix(
    const ReadOnlyAst auto& ast, int nodeIdx, char* out, int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int totalLen = static_cast<int>(node.literal.size());
      int len = totalLen;
      if (len > maxLen) {
        len = maxLen;
      }
      for (int i = 0; i < len; ++i) {
        out[i] = node.literal[totalLen - 1 - i];
      }
      return len;
    }
    case NodeKind::Sequence: {
      // Traverse backward from last child via prev_sibling.
      int last = node.child_last;
      int total = 0;
      for (int c = last; c >= 0 && total < maxLen;
           c = ast.nodes[c].prev_sibling) {
        int got =
            extractBranchLiteralSuffix(ast, c, out + total, maxLen - total);
        if (got == 0) {
          break;
        }
        total += got;
        int fw = computeFixedWidth(ast, c);
        if (fw < 0 || fw != got) {
          break;
        }
      }
      return total;
    }
    case NodeKind::Group:
      if (node.capturing && ast.has_backref) {
        return 0;
      }
      return extractBranchLiteralSuffix(ast, node.child_first, out, maxLen);
    case NodeKind::Repeat: {
      if (node.min_repeat <= 0) {
        return 0;
      }
      int innerWidth = computeFixedWidth(ast, node.child_first);
      if (innerWidth <= 0) {
        return 0;
      }
      int total = 0;
      for (int rep = 0; rep < node.min_repeat && total < maxLen; ++rep) {
        int remaining = maxLen - total;
        int need = innerWidth < remaining ? innerWidth : remaining;
        int got = extractBranchLiteralSuffix(
            ast, node.child_first, out + total, need);
        if (got != need) {
          break;
        }
        total += got;
      }
      return total;
    }
    case NodeKind::Anchor:
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return 0;
  }
  return 0;
}

// --------------------------------------------------------------------------
// hasOnlyLiteralPrefixStructure: true when a subtree is composed entirely of
// Literal / Sequence / non-capturing Group nodes. This is stricter than what
// stripLiteralPrefix() can mechanically mutate, but it matches the cases where
// stripping deeper into a group remains semantically safe.
// --------------------------------------------------------------------------

constexpr bool hasOnlyLiteralPrefixStructure(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }

  const auto& node = ast.nodes[nodeIdx];
  switch (node.kind) {
    case NodeKind::Literal:
    case NodeKind::Empty:
      return true;
    case NodeKind::Sequence: {
      for (int c = node.child_first; c >= 0; c = ast.nodes[c].next_sibling) {
        if (!hasOnlyLiteralPrefixStructure(ast, c)) {
          return false;
        }
      }
      return true;
    }
    case NodeKind::Group:
      return !node.capturing &&
          hasOnlyLiteralPrefixStructure(ast, node.child_first);
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return false;
  }
  return false;
}

// --------------------------------------------------------------------------
// countStrippableLiteralPrefixChars: determine how many leading literal
// characters stripLiteralPrefix() can actually remove from this subtree while
// staying within literal-only non-capturing group structure.
// --------------------------------------------------------------------------

constexpr int countStrippableLiteralPrefixChars(
    const ReadOnlyAst auto& ast, int nodeIdx, int maxLen) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      return litLen < maxLen ? litLen : maxLen;
    }
    case NodeKind::Sequence: {
      int total = 0;
      for (int c = node.child_first; c >= 0 && total < maxLen;
           c = ast.nodes[c].next_sibling) {
        const auto& child = ast.nodes[c];
        if (child.kind == NodeKind::Literal) {
          int litLen = static_cast<int>(child.literal.size());
          int take = litLen < (maxLen - total) ? litLen : (maxLen - total);
          total += take;
          if (take < litLen) {
            return total;
          }
          continue;
        }
        if (child.kind == NodeKind::Group && !child.capturing &&
            hasOnlyLiteralPrefixStructure(ast, c)) {
          total += countStrippableLiteralPrefixChars(ast, c, maxLen - total);
        }
        return total;
      }
      return total;
    }
    case NodeKind::Group:
      if (node.capturing || !hasOnlyLiteralPrefixStructure(ast, nodeIdx)) {
        return 0;
      }
      return countStrippableLiteralPrefixChars(ast, node.child_first, maxLen);
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
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

constexpr int stripLiteralPrefix(
    MutableAst auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      if (count >= litLen) {
        return addEmptyNode(ast);
      }
      AstNode newLit;
      newLit.kind = NodeKind::Literal;
      newLit.literal = node.literal.substr(count);
      return ast.addNode(newLit);
    }
    case NodeKind::Sequence: {
      int children[kMaxAltBranches] = {};
      int childCount = 0;
      int child = node.child_first;
      while (child >= 0 && childCount < kMaxAltBranches) {
        children[childCount++] = child;
        child = ast.nodes[child].next_sibling;
      }

      int remaining = count;
      bool consumed[kMaxAltBranches] = {};
      bool anyChange = false;

      for (int i = 0; i < childCount && remaining > 0; ++i) {
        const auto& cnode = ast.nodes[children[i]];
        if (cnode.kind == NodeKind::Literal) {
          int litLen = static_cast<int>(cnode.literal.size());
          if (remaining >= litLen) {
            remaining -= litLen;
            consumed[i] = true;
            anyChange = true;
          } else {
            AstNode newLit;
            newLit.kind = NodeKind::Literal;
            newLit.literal = cnode.literal.substr(remaining);
            children[i] = ast.addNode(newLit);
            remaining = 0;
            anyChange = true;
          }
        } else if (
            cnode.kind == NodeKind::Group || cnode.kind == NodeKind::Sequence) {
          char tmp[256] = {};
          int extractable =
              extractBranchLiteralPrefix(ast, children[i], tmp, remaining);
          if (extractable == 0) {
            break;
          }
          int toStrip = extractable < remaining ? extractable : remaining;
          int stripped = stripLiteralPrefix(ast, children[i], toStrip);
          if (stripped != children[i]) {
            children[i] = stripped;
            anyChange = true;
          }
          remaining -= toStrip;
        } else {
          break;
        }
      }

      if (!anyChange) {
        return nodeIdx;
      }

      int newChildren[kMaxAltBranches] = {};
      int newChildCount = 0;
      for (int i = 0; i < childCount; ++i) {
        if (!consumed[i]) {
          newChildren[newChildCount++] = children[i];
        }
      }

      return makeSequence(ast, newChildren, newChildCount);
    }
    case NodeKind::Group: {
      char tmp[256] = {};
      int cap = count < 256 ? count : 256;
      int extractable =
          extractBranchLiteralPrefix(ast, node.child_first, tmp, cap);
      int toStrip = extractable < count ? extractable : count;
      if (toStrip == 0) {
        return nodeIdx;
      }
      int inner = stripLiteralPrefix(ast, node.child_first, toStrip);
      if (inner == node.child_first) {
        return nodeIdx;
      }
      if (!node.capturing && ast.nodes[inner].kind == NodeKind::Empty) {
        return inner;
      }
      return wrapInGroup(ast, inner, node.capturing, node.group_id);
    }
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
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

constexpr int stripLiteralSuffix(
    MutableAst auto& ast, int nodeIdx, int count) noexcept {
  if (nodeIdx < 0 || count <= 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int litLen = static_cast<int>(node.literal.size());
      if (count >= litLen) {
        return addEmptyNode(ast);
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
      int child = node.child_first;
      while (child >= 0 && childCount < kMaxAltBranches) {
        children[childCount++] = child;
        child = ast.nodes[child].next_sibling;
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

      if (keepCount == childCount && remaining == count) {
        return nodeIdx;
      }

      return makeSequence(ast, children, keepCount);
    }
    case NodeKind::Group: {
      if (node.capturing) {
        return nodeIdx;
      }
      int inner = stripLiteralSuffix(ast, node.child_first, count);
      if (inner == node.child_first) {
        return nodeIdx;
      }
      if (ast.nodes[inner].kind == NodeKind::Empty) {
        return inner;
      }
      return wrapInGroup(ast, inner);
    }
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return nodeIdx;
  }
  return nodeIdx;
}

// --------------------------------------------------------------------------
// recordPrefixGroupAdjustments: walk the AST to find capturing groups within
// the literal prefix region and record their offsets and stripped char counts.
// Returns the number of prefix chars consumed from this subtree.
// --------------------------------------------------------------------------

constexpr int recordPrefixGroupAdjustments(
    MutableAst auto& ast, int nodeIdx, int maxLen, int currentOffset) noexcept {
  if (nodeIdx < 0 || maxLen <= 0) {
    return 0;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      int len = static_cast<int>(node.literal.size());
      return len < maxLen ? len : maxLen;
    }
    case NodeKind::Sequence: {
      int total = 0;
      int child = node.child_first;
      while (child >= 0 && total < maxLen) {
        int got = recordPrefixGroupAdjustments(
            ast, child, maxLen - total, currentOffset + total);
        if (got == 0) {
          break;
        }
        total += got;
        int fw = computeFixedWidth(ast, child);
        if (fw < 0 || fw != got) {
          break;
        }
        child = ast.nodes[child].next_sibling;
      }
      return total;
    }
    case NodeKind::Group: {
      int got = recordPrefixGroupAdjustments(
          ast, node.child_first, maxLen, currentOffset);
      if (node.capturing && got > 0) {
        if (ast.prefix_group_adjustment_count < ast.group_count) {
          auto& adj =
              ast.prefix_group_adjustments[ast.prefix_group_adjustment_count++];
          adj.group_id = node.group_id;
          adj.chars_stripped = got;
          adj.prefix_offset = currentOffset;
        }
      }
      return got;
    }
    case NodeKind::Empty:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Alternation:
    case NodeKind::Repeat:
    case NodeKind::Anchor:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      return 0;
  }
  return 0;
}

// --------------------------------------------------------------------------
// extractRootLiteralPrefix: extract the literal prefix from the AST root
// and store it in literal_buf. Does NOT modify the AST.
// --------------------------------------------------------------------------

constexpr void extractRootLiteralPrefix(MutableAst auto& ast) noexcept {
  if (ast.root < 0) {
    return;
  }

  // Maximum prefix length: full buffer size
  int maxPrefix = ast.literal_buf_size;
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

  // Record prefix group adjustments before stripping
  ast.prefix_group_adjustment_count = 0;
  if (ast.group_count > 0) {
    ast.ensurePrefixGroupAdjustments();
    recordPrefixGroupAdjustments(ast, ast.root, prefixLen, 0);
  }

  // Store prefix at front of literal_buf
  for (int i = 0; i < prefixLen; ++i) {
    ast.literal_buf[i] = prefix[i];
  }
  ast.prefix_len = prefixLen;
  ast.prefix_strip_len = prefixLen;
}

// --------------------------------------------------------------------------
// computePrefixStripLen: for reverse execution, determine how many prefix
// characters can be safely stripped from the AST based on disjointness
// with the following node the node that follows the prefix.
//
// In reverse execution, the engine processes children in reverse order,
// so the node AFTER the prefix runs BEFORE the prefix. If that node can
// consume the same characters as the prefix's trailing chars, stripping
// those chars would bypass possessive/atomic constraints.
// --------------------------------------------------------------------------

constexpr void computePrefixStripLen(MutableAst auto& ast) noexcept {
  if (ast.prefix_len <= 0 || ast.root < 0) {
    return;
  }

  ast.prefix_strip_len = 0;

  int rootIdx = ast.root;
  while (rootIdx >= 0 && ast.nodes[rootIdx].kind == NodeKind::Group &&
         !ast.nodes[rootIdx].capturing) {
    rootIdx = ast.nodes[rootIdx].child_first;
  }

  if (rootIdx < 0) {
    return;
  }

  if (ast.nodes[rootIdx].kind != NodeKind::Sequence) {
    // Root is not a sequence — prefix covers the entire node.
    ast.prefix_strip_len = ast.prefix_len;
  } else {
    // Walk forward through children, consuming prefix chars to find the
    // following node (the first child not consumed by the prefix).
    int prefixRemaining = ast.prefix_len;
    NodeIdx followingNode = kNoNode;

    for (int c = ast.nodes[rootIdx].child_first; c >= 0 && prefixRemaining > 0;
         c = ast.nodes[c].next_sibling) {
      char tmp[256] = {};
      int cap = prefixRemaining < 256 ? prefixRemaining : 256;
      int extracted = extractBranchLiteralPrefix(ast, c, tmp, cap);

      if (extracted <= 0) {
        followingNode = c;
        break;
      }

      prefixRemaining -= extracted;
      if (prefixRemaining <= 0) {
        followingNode = ast.nodes[c].next_sibling;
        break;
      }
    }

    if (followingNode < 0) {
      ast.prefix_strip_len = ast.prefix_len;
    } else {
      // Build the character filter from the following node(s).
      FirstCharFilter followingFilter;
      followingFilter.accepts_all = false;

      bool foundRequiredChild = false;
      for (int c = followingNode; c >= 0; c = ast.nodes[c].next_sibling) {
        auto f = extractFollowFilter(
            ast, c, /*ignoreIdx=*/-1, /*firstCharOnly=*/false);
        if (f.accepts_all) {
          followingFilter.accepts_all = true;
          break;
        }
        followingFilter.mergeFrom(f.ranges, f.range_count);
        if (followingFilter.accepts_all) {
          break;
        }
        if (!isPossiblyZeroWidth(ast, c)) {
          foundRequiredChild = true;
          break;
        }
      }

      // If all following children are possibly zero-width, the entire following
      // content can match empty — stripping is unsafe.
      if (!foundRequiredChild && !followingFilter.accepts_all) {
        followingFilter.accepts_all = true;
      }

      if (followingFilter.accepts_all) {
        ast.prefix_strip_len = 0;
      } else if (followingFilter.range_count == 0) {
        ast.prefix_strip_len = ast.prefix_len;
      } else {
        // Scan prefix chars RIGHT-TO-LEFT (from the boundary with the following
        // node toward the start) looking for the first disjoint character.
        // Everything from position 0 up to and including that barrier is safe.
        for (int i = ast.prefix_len - 1; i >= 0; --i) {
          auto c = static_cast<unsigned char>(ast.literal_buf[i]);
          bool overlaps = false;
          for (int r = 0; r < followingFilter.range_count; ++r) {
            if (c >= followingFilter.ranges[r].lo &&
                c <= followingFilter.ranges[r].hi) {
              overlaps = true;
              break;
            }
          }
          if (!overlaps) {
            ast.prefix_strip_len = i + 1;
            break;
          }
        }
      }
    }
  }

  int structuralStripLen =
      countStrippableLiteralPrefixChars(ast, ast.root, ast.prefix_strip_len);
  if (ast.prefix_strip_len > structuralStripLen) {
    ast.prefix_strip_len = structuralStripLen;
  }
}

// --------------------------------------------------------------------------
// stripRootLiteralPrefix: strip prefix_strip_len leading literal characters
// from the AST root.
// --------------------------------------------------------------------------

constexpr void stripRootLiteralPrefix(MutableAst auto& ast) noexcept {
  if (ast.prefix_strip_len <= 0 || ast.root < 0) {
    return;
  }
  ast.root = stripLiteralPrefix(ast, ast.root, ast.prefix_strip_len);
}

} // namespace folly::regex::detail
