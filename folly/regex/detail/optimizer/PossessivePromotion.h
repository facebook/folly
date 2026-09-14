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

// Possessive promotion, demotion, and backtrack safety analysis for the regex
// optimizer.
//
// possessivePass: when mode is Promote, promotes greedy repeats to possessive
// when the repeat body is disjoint from its follow set, eliminating unnecessary
// backtracking.  When mode is Demote, demotes PossessiveProbed repeats back to
// Possessive when disjointness can be proven on the reversed AST, eliminating
// unnecessary forward probe evaluations.
//
// computeBacktrackSafe: determines if the pattern is provably linear-time
// (no exponential backtracking possible).

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/optimizer/AstHelpers.h>

namespace folly::regex::detail {

enum class PossessivePassMode { Promote, Demote };

constexpr void possessivePass(
    MutableAst auto& ast,
    int nodeIdx,
    FirstCharFilter outerFollow,
    PossessivePassMode mode,
    int repeatInnerRoot = -1) noexcept {
  if (nodeIdx < 0) {
    return;
  }

  auto& node = ast.nodes[nodeIdx];
  const bool demoting = mode == PossessivePassMode::Demote;

  switch (node.kind) {
    case NodeKind::Sequence: {
      int child = node.child_first;
      while (child >= 0) {
        int nextSibling = ast.nodes[child].next_sibling;

        // Compute effective follow set: siblings + outer context.
        // In Promote mode, walk forward (next_sibling) and extract
        // forward first-chars.  In Demote mode, walk backward
        // (prev_sibling) and extract reverse (last-char) filters.
        FirstCharFilter followFilter;
        followFilter.accepts_all = false;
        int sibling = demoting ? ast.nodes[child].prev_sibling : nextSibling;
        bool foundNonZeroWidth = false;
        while (sibling >= 0) {
          auto sibFilter = demoting
              ? extractFollowFilter(ast, sibling, -1, true, /*reverse=*/true)
              : extractFollowFilter(ast, sibling);
          if (sibFilter.accepts_all) {
            followFilter.accepts_all = true;
            break;
          }

          followFilter.mergeFrom(sibFilter.ranges, sibFilter.range_count);
          if (followFilter.accepts_all) {
            break;
          }

          if (!isPossiblyZeroWidth(ast, sibling)) {
            foundNonZeroWidth = true;
            break;
          }

          sibling = demoting
              ? ast.nodes[sibling].prev_sibling
              : ast.nodes[sibling].next_sibling;
        }

        // When no non-zero-width sibling terminates the follow set,
        // include the outer context (what follows the parent).
        if (!foundNonZeroWidth && !followFilter.accepts_all) {
          if (outerFollow.accepts_all) {
            followFilter.accepts_all = true;
          } else {
            followFilter.mergeFrom(outerFollow.ranges, outerFollow.range_count);
          }
          // Add next-iteration first chars from the parent repeat,
          // excluding the current child so it doesn't block its own
          // promotion/demotion.
          if (repeatInnerRoot >= 0 && !followFilter.accepts_all) {
            auto innerFirst = demoting
                ? extractFollowFilter(
                      ast,
                      repeatInnerRoot,
                      /*ignoreIdx=*/child,
                      true,
                      /*reverse=*/true)
                : extractFollowFilter(
                      ast, repeatInnerRoot, /*ignoreIdx=*/child);
            if (innerFirst.accepts_all) {
              followFilter.accepts_all = true;
            } else {
              followFilter.mergeFrom(innerFirst.ranges, innerFirst.range_count);
            }
          }
        }

        if (ast.nodes[child].kind == NodeKind::Repeat) {
          bool isCandidate = demoting
              ? ast.nodes[child].isPossessiveProbed()
              : !ast.nodes[child].isPossessive();
          if (isCandidate) {
            int inner = ast.nodes[child].child_first;
            bool canAct = false;
            if (ast.nodes[child].max_repeat == 1) {
              auto innerFirst = extractFollowFilter(ast, inner);
              canAct = !innerFirst.accepts_all && !followFilter.accepts_all &&
                  areFiltersDisjoint(innerFirst, followFilter);
            } else if (isSimpleCharTest(ast, inner)) {
              canAct = !followFilter.accepts_all &&
                  isDisjointFromFilter(ast, inner, followFilter);
            }
            if (canAct) {
              ast.nodes[child].repeat_mode = RepeatMode::Possessive;
            }
          }
        }

        possessivePass(ast, child, followFilter, mode, repeatInnerRoot);
        child = nextSibling;
      }
      break;
    }
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        possessivePass(ast, child, outerFollow, mode, repeatInnerRoot);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
      possessivePass(ast, node.child_first, outerFollow, mode, repeatInnerRoot);
      break;
    case NodeKind::Repeat: {
      // For Demote mode: check if this bare Repeat root (not inside a
      // Sequence) can be demoted using outerFollow.
      if (demoting && node.isPossessiveProbed()) {
        int inner = node.child_first;
        bool canDemote = false;
        if (node.max_repeat == 1) {
          auto innerFirst = extractFollowFilter(ast, inner);
          canDemote = !innerFirst.accepts_all && !outerFollow.accepts_all &&
              areFiltersDisjoint(innerFirst, outerFollow);
        } else if (isSimpleCharTest(ast, inner)) {
          canDemote = !outerFollow.accepts_all &&
              isDisjointFromFilter(ast, inner, outerFollow);
        }
        if (canDemote) {
          ast.nodes[nodeIdx].repeat_mode = RepeatMode::Possessive;
        }
      }
      // For possessive repeats (including atomic groups), the inner
      // content is committed — outer context doesn't constrain inner
      // promotions, so start with an empty follow.
      FirstCharFilter repeatChildFollow;
      repeatChildFollow.accepts_all = false;
      if (!node.isPossessive()) {
        repeatChildFollow = outerFollow;
      }
      // Pass the inner root so the Sequence case can compute
      // next-iteration first chars per-candidate (excluding the
      // candidate itself from the filter).
      int childRepeatRoot = (!node.isPossessive() && node.max_repeat != 1)
          ? node.child_first
          : -1;
      possessivePass(
          ast, node.child_first, repeatChildFollow, mode, childRepeatRoot);
      break;
    }
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      possessivePass(
          ast, node.child_first, FirstCharFilter{.accepts_all = false}, mode);
      break;
    case NodeKind::Literal:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
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
// computeBacktrackSafe: determine if the pattern is provably linear-time.
// --------------------------------------------------------------------------

constexpr bool computeBacktrackSafe(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Repeat: {
      if (node.isPossessive()) {
        return computeBacktrackSafe(ast, node.child_first);
      }
      int innerIdx = node.child_first;
      if (innerIdx < 0) {
        return true;
      }
      auto innerKind = ast.nodes[innerIdx].kind;
      int effectiveIdx = innerIdx;
      auto effectiveKind = innerKind;
      if (innerKind == NodeKind::Group) {
        effectiveIdx = ast.nodes[innerIdx].child_first;
        if (effectiveIdx < 0) {
          return false;
        }
        effectiveKind = ast.nodes[effectiveIdx].kind;
      }
      if (effectiveKind == NodeKind::Literal ||
          effectiveKind == NodeKind::CharClass ||
          effectiveKind == NodeKind::AnyByte) {
        return true;
      }
      if (effectiveKind == NodeKind::Sequence) {
        bool allCharTest = true;
        int sc = ast.nodes[effectiveIdx].child_first;
        while (sc >= 0) {
          auto sk = ast.nodes[sc].kind;
          if (sk != NodeKind::Literal && sk != NodeKind::CharClass &&
              sk != NodeKind::AnyByte) {
            allCharTest = false;
            break;
          }
          sc = ast.nodes[sc].next_sibling;
        }
        if (allCharTest) {
          return true;
        }
      }
      return false;
    }
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (!computeBacktrackSafe(ast, child)) {
          return false;
        }
        child = ast.nodes[child].next_sibling;
      }
      return true;
    }
    case NodeKind::Group:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return computeBacktrackSafe(ast, node.child_first);
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return true;
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
      return false;
  }
  return true;
}

} // namespace folly::regex::detail
