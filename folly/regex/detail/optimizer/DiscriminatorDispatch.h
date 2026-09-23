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

// Alternation discriminator dispatch system for the regex optimizer.
//
// Computes, propagates, and materializes discriminator character classes
// for alternation nodes. When all branches of an alternation have disjoint
// characters at a given offset, the executor can use a single character
// lookup to select the correct branch instead of trying each one.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/CharClass.h>

namespace folly::regex::detail {

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

// --------------------------------------------------------------------------
// mergeCharSets: union two BranchCharSets into one, sorting the result
// by lo bound for areRangeSetsDisjoint. Returns invalid if the combined
// range count exceeds kMaxRanges (to avoid incomplete sets that could
// cause false disjointness results).
// --------------------------------------------------------------------------

constexpr BranchCharSet mergeCharSets(
    const BranchCharSet& a, const BranchCharSet& b) noexcept {
  if (!a.valid) {
    return b;
  }
  if (!b.valid) {
    return a;
  }
  if (a.range_count + b.range_count > BranchCharSet::kMaxRanges) {
    return BranchCharSet{};
  }
  BranchCharSet result;
  result.valid = true;
  for (int i = 0; i < a.range_count; ++i) {
    result.ranges[result.range_count++] = a.ranges[i];
  }
  for (int i = 0; i < b.range_count; ++i) {
    result.ranges[result.range_count++] = b.ranges[i];
  }
  // Insertion sort by lo bound.
  for (int i = 1; i < result.range_count; ++i) {
    auto key = result.ranges[i];
    int j = i - 1;
    while (j >= 0 && result.ranges[j].lo > key.lo) {
      result.ranges[j + 1] = result.ranges[j];
      --j;
    }
    result.ranges[j + 1] = key;
  }
  return result;
}

// --------------------------------------------------------------------------
// getBranchCharSet: get the character set for a direct branch at offset.
// For Literal/CharClass, reads from the AST. For inner alternations,
// reads from the precomputed valid_discriminators ChunkedBuffer.
// --------------------------------------------------------------------------

constexpr BranchCharSet getBranchCharSet(
    const ReadOnlyAst auto& ast, int nodeIdx, int offset) noexcept {
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
      return getBranchCharSet(ast, n.child_first, offset);
    case NodeKind::Alternation: {
      auto* entry = findDiscriminatorEntry(n.valid_discriminators, offset);
      if (entry) {
        for (int i = 0; i < entry->range_count &&
             result.range_count < BranchCharSet::kMaxRanges;
             ++i) {
          result.ranges[result.range_count++] = entry->ranges[i];
        }
        result.valid = true;
      } else if (n.discriminator_offset == offset && n.char_class_index >= 0) {
        // Fallback for the serialized inner pipeline: valid_discriminators
        // doesn't exist but the char class was already materialized by a
        // prior recursive call to materializeDiscriminatorCharClassesFrom-
        // KnownOffsets. Read ranges from the materialized char class.
        const auto& cc = ast.char_classes[n.char_class_index];
        for (int i = 0; i < cc.range_count &&
             result.range_count < BranchCharSet::kMaxRanges;
             ++i) {
          result.ranges[result.range_count++] = ast.ranges[cc.range_offset + i];
        }
        result.valid = (result.range_count > 0);
      }
      break;
    }
    case NodeKind::Sequence: {
      // Walk sequence children, forking at variable-width Repeat nodes
      // with fixed-width inner to compute the union of char sets across
      // all possible repetition counts.
      auto walkSeq =
          [&](auto& self, int child, int accumulated) -> BranchCharSet {
        while (child >= 0) {
          int fw = computeFixedWidth(ast, child);
          if (fw >= 0) {
            if (offset < accumulated + fw) {
              return getBranchCharSet(ast, child, offset - accumulated);
            }
            accumulated += fw;
            child = ast.nodes[child].next_sibling;
            continue;
          }
          // Variable-width child. Unwrap Groups to find the underlying node.
          int innerNode = child;
          while (ast.nodes[innerNode].kind == NodeKind::Group) {
            innerNode = ast.nodes[innerNode].child_first;
          }
          const auto& rn = ast.nodes[innerNode];
          if (rn.kind == NodeKind::Repeat) {
            int innerFW = computeFixedWidth(ast, rn.child_first);
            if (innerFW > 0) {
              int localOff = offset - accumulated;
              int fixedPart = rn.min_repeat * innerFW;

              // Within guaranteed repetitions: char is deterministic.
              if (localOff < fixedPart) {
                return getBranchCharSet(
                    ast, rn.child_first, localOff % innerFW);
              }

              // Past guaranteed part: fork for each possible additional
              // repetition count. For Repeat{0,1} this is just 2 paths;
              // for Repeat{0,inf} the iteration is bounded by adjOff/W
              // and early-exits on kMaxRanges overflow.
              int adjOff = localOff - fixedPart;
              int optMax =
                  rn.max_repeat < 0 ? -1 : rn.max_repeat - rn.min_repeat;
              int floorK = adjOff / innerFW;
              int upperK = (optMax < 0 || optMax > floorK) ? floorK : optMax;

              BranchCharSet result;

              // Inner char covers all additional rep counts > floorK
              // (offset falls within the repeated content).
              if (optMax < 0 || optMax > floorK) {
                result =
                    getBranchCharSet(ast, rn.child_first, adjOff % innerFW);
              }

              // Each additional count k from 0..upperK maps to a
              // different position in the subsequent children.
              for (int k = 0; k <= upperK; ++k) {
                BranchCharSet remSet = self(
                    self,
                    ast.nodes[child].next_sibling,
                    accumulated + fixedPart + k * innerFW);
                if (!remSet.valid) {
                  continue;
                }
                if (result.valid &&
                    result.range_count + remSet.range_count >
                        BranchCharSet::kMaxRanges) {
                  return BranchCharSet{};
                }
                result = mergeCharSets(result, remSet);
              }

              return result;
            }
          }
          // Fallback for other variable-width children.
          int prefix = computeFixedWidthPrefix(ast, child);
          if (offset < accumulated + prefix) {
            return getBranchCharSet(ast, child, offset - accumulated);
          }
          return BranchCharSet{};
        }
        return BranchCharSet{};
      };
      return walkSeq(walkSeq, n.child_first, 0);
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_first);
        if (innerFW > 0) {
          if (offset < innerFW * n.min_repeat) {
            return getBranchCharSet(ast, n.child_first, offset % innerFW);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_first);
          if (offset < innerPrefix) {
            return getBranchCharSet(ast, n.child_first, offset);
          }
        }
      }
      break;
    }
    case NodeKind::Empty:
    case NodeKind::AnyByte:
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
      break;
  }
  return result;
}

// --------------------------------------------------------------------------
// Pass 1 (bottom-up): compute valid_discriminators ChunkedBuffer for each
// alternation. Each entry stores [offset, union_char_set] for offsets
// where all branches have pairwise disjoint characters. Recurses into
// children first (post-order) so inner alternations' entries are available
// when the parent queries them via getBranchCharSet.
// --------------------------------------------------------------------------

constexpr void computeDiscriminatorsBottomUp(
    MutableAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        computeDiscriminatorsBottomUp(ast, child);
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
      computeDiscriminatorsBottomUp(ast, node.child_first);
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      break;
  }

  if (node.kind != NodeKind::Alternation) {
    return;
  }

  int branchCount = 0;
  int branchIndices[kMaxAltBranches] = {};
  int minScanBound = 2147483647;
  int child = node.child_first;
  while (child >= 0 && branchCount < kMaxAltBranches) {
    branchIndices[branchCount] = child;
    int bound = computeMinWidth(ast, child);
    if (bound < minScanBound) {
      minScanBound = bound;
    }
    ++branchCount;
    child = ast.nodes[child].next_sibling;
  }

  if (branchCount < 2 || minScanBound <= 0) {
    return;
  }

  // Reused across offsets — avoids 2.3KB of stack reinitialization per offset.
  BranchCharSet branchSets[kMaxAltBranches];

  auto* buf = new ChunkedBuffer<DiscriminatorEntry>();
  for (int offset = 0; offset < minScanBound; ++offset) {
    // Build branch sets and verify pairwise disjointness incrementally.
    // Bail as soon as any branch is invalid or overlaps a prior branch —
    // this saves the rest of the getBranchCharSet calls (which can each
    // trigger recursive computeFixedWidth walks) on bad offsets.
    bool valid = true;
    for (int i = 0; i < branchCount; ++i) {
      branchSets[i] = getBranchCharSet(ast, branchIndices[i], offset);
      if (!branchSets[i].valid) {
        valid = false;
        break;
      }
      for (int j = 0; j < i; ++j) {
        if (!areRangeSetsDisjoint(
                branchSets[i].ranges,
                branchSets[i].range_count,
                branchSets[j].ranges,
                branchSets[j].range_count)) {
          valid = false;
          break;
        }
      }
      if (!valid) {
        break;
      }
    }
    if (!valid) {
      continue;
    }

    // Materialize the entry from the branch sets we already built.
    DiscriminatorEntry entry;
    entry.offset = offset;
    for (int i = 0; i < branchCount; ++i) {
      for (int r = 0; r < branchSets[i].range_count &&
           entry.range_count < DiscriminatorEntry::kMaxRanges;
           ++r) {
        entry.ranges[entry.range_count++] = branchSets[i].ranges[r];
      }
    }
    // Insertion sort by lo bound (small N, ≤ kMaxRanges = 16).
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
    MutableAst auto& ast, int nodeIdx, int forced_offset) noexcept {
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
        int child = node.child_first;
        while (child >= 0) {
          ++branchCount;
          child = ast.nodes[child].next_sibling;
        }
        if (branchCount >= 2) {
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
      int child = node.child_first;
      while (child >= 0) {
        propagateDiscriminatorsTopDown(ast, child, chosenOffset);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Sequence: {
      int child = node.child_first;
      int accumulated = 0;
      while (child >= 0) {
        int adjusted = forced_offset >= 0 ? forced_offset - accumulated : -1;
        if (adjusted < 0) {
          adjusted = -1;
        }
        propagateDiscriminatorsTopDown(ast, child, adjusted);
        int fw = computeFixedWidth(ast, child);
        if (fw < 0) {
          child = ast.nodes[child].next_sibling;
          while (child >= 0) {
            propagateDiscriminatorsTopDown(ast, child, -1);
            child = ast.nodes[child].next_sibling;
          }
          break;
        }
        accumulated += fw;
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
      propagateDiscriminatorsTopDown(ast, node.child_first, forced_offset);
      break;
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      propagateDiscriminatorsTopDown(ast, node.child_first, -1);
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      break;
  }
}

// --------------------------------------------------------------------------
// Pass 3: materialize merged char classes for alternations with
// discriminator_offset. For each alternation that chose a discriminator,
// build a char class from the union of all branch char sets at that offset
// and store the char_class_index on the alternation node.
//
// Works in two modes:
// - After computeDiscriminatorsBottomUp/propagateDiscriminatorsTopDown
//   (outer pipeline): getBranchCharSet reads from valid_discriminators.
// - After (?~DN:...) annotation parsing (inner pipeline): getBranchCharSet
//   falls back to already-materialized char_class_index on inner
//   alternations since valid_discriminators doesn't exist.
//
// precomputeFixedWidths must have been called first.
// --------------------------------------------------------------------------

constexpr void materializeDiscriminatorCharClasses(
    MutableAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        materializeDiscriminatorCharClasses(ast, child);
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
      materializeDiscriminatorCharClasses(ast, node.child_first);
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      break;
  }

  if (node.kind != NodeKind::Alternation || node.discriminator_offset < 0) {
    return;
  }

  int discOffset = node.discriminator_offset;

  // Build merged char class from all branches at the discriminator offset.
  CharRangeSet rs;
  int child = node.child_first;
  while (child >= 0) {
    BranchCharSet bcs = getBranchCharSet(ast, child, discOffset);
    if (bcs.valid) {
      for (int i = 0; i < bcs.range_count; ++i) {
        rs.addRange(bcs.ranges[i].lo, bcs.ranges[i].hi);
      }
    }
    child = ast.nodes[child].next_sibling;
  }
  node.char_class_index = ast.addCharClass(rs);

  // Materialize per-branch char classes for branches where
  // resolveCharAtOffset can't resolve (e.g. sequences with optional
  // prefixes). This lets the NFA's packDiscriminatorChar return a
  // proper char class instead of -1 for those branches.
  child = node.child_first;
  while (child >= 0) {
    auto charInfo = resolveCharAtOffset(ast, child, discOffset);
    if (!charInfo.valid) {
      BranchCharSet bcs = getBranchCharSet(ast, child, discOffset);
      if (bcs.valid && bcs.range_count > 0) {
        CharRangeSet brs;
        for (int i = 0; i < bcs.range_count; ++i) {
          brs.addRange(bcs.ranges[i].lo, bcs.ranges[i].hi);
        }
        ast.nodes[child].char_class_index = ast.addCharClass(brs);
      }
    }
    child = ast.nodes[child].next_sibling;
  }
}

// --------------------------------------------------------------------------
// freeDiscriminatorSets: walk the AST and delete all valid_discriminators
// ChunkedBuffers. Must be called after the top-down pass and before
// compact(), since heap allocations cannot persist across constexpr
// contexts.
// --------------------------------------------------------------------------

constexpr void freeDiscriminatorSets(
    MutableAst auto& ast, int nodeIdx) noexcept {
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
      int child = node.child_first;
      while (child >= 0) {
        freeDiscriminatorSets(ast, child);
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
      freeDiscriminatorSets(ast, node.child_first);
      break;
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      break;
  }
}

} // namespace folly::regex::detail
