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
// This header is the top-level orchestrator that includes all optimizer
// sub-passes from the optimizer/ subdirectory and ties them together in
// optimizeAst(). Individual passes are split into focused headers:
//
//   optimizer/AstHelpers.h           — AST construction & structural utilities
//   optimizer/LiteralOptimizer.h     — Literal prefix/suffix extraction &
//   stripping optimizer/AlternationFactoring.h — Alternation branch
//   manipulation passes optimizer/PossessivePromotion.h  — Possessive promotion
//   & backtrack safety optimizer/DiscriminatorDispatch.h— Alternation
//   discriminator dispatch system optimizer/ReverseAst.h           — Reverse
//   AST transformation & dot-star pruning
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
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/FixedBitset.h>
#include <folly/regex/detail/optimizer/AlternationFactoring.h>
#include <folly/regex/detail/optimizer/AstHelpers.h>
#include <folly/regex/detail/optimizer/DiscriminatorDispatch.h>
#include <folly/regex/detail/optimizer/LiteralOptimizer.h>
#include <folly/regex/detail/optimizer/PossessivePromotion.h>
#include <folly/regex/detail/optimizer/ReverseAst.h>

namespace folly::regex::detail {

// --------------------------------------------------------------------------
// optimizeNode: recursively optimize a single AST node.
// --------------------------------------------------------------------------

constexpr int optimizeNode(MutableAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  struct OptCtx {
    decltype(ast)& ast;
    int nodeIdx;
  };
  OptCtx ctx{ast, nodeIdx};

  return NodeKindSwitch<
      Case<
          NodeKind::Sequence,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            // Optimize each child
            int child = node.child_first;
            while (child >= 0) {
              int optimized = optimizeNode(ast, child);
              if (optimized != child) {
                ast.nodes[nodeIdx].replaceChild(ast, child, optimized);
                child = optimized;
              }
              child = ast.nodes[child].next_sibling;
            }
            // Flatten nested Sequences: Seq(a, Seq(b, c), d) → Seq(a, b, c, d)
            {
              int cur = ast.nodes[nodeIdx].child_first;
              while (cur >= 0) {
                int next = ast.nodes[cur].next_sibling;
                if (ast.nodes[cur].kind == NodeKind::Sequence) {
                  // Splice inner Sequence's children into the parent.
                  int innerFirst = ast.nodes[cur].child_first;
                  int innerLast = ast.nodes[cur].child_last;
                  if (innerFirst >= 0) {
                    // Link inner children into parent's child list
                    // in place of the nested Sequence node.
                    int prevSib = ast.nodes[cur].prev_sibling;
                    int nextSib = ast.nodes[cur].next_sibling;
                    // Connect prev → innerFirst
                    if (prevSib >= 0) {
                      ast.nodes[prevSib].next_sibling = innerFirst;
                    } else {
                      ast.nodes[nodeIdx].child_first = innerFirst;
                    }
                    ast.nodes[innerFirst].prev_sibling = prevSib;
                    // Connect innerLast → next
                    ast.nodes[innerLast].next_sibling = nextSib;
                    if (nextSib >= 0) {
                      ast.nodes[nextSib].prev_sibling = innerLast;
                    } else {
                      ast.nodes[nodeIdx].child_last = innerLast;
                    }
                    cur = innerLast;
                  } else {
                    // Empty inner Sequence — just remove it
                    ast.nodes[nodeIdx].unlinkChild(ast, cur);
                  }
                }
                cur = (cur >= 0) ? ast.nodes[cur].next_sibling : next;
              }
            }
            // Adjacent repeat merging: x+x+ → x{2,∞}
            {
              int cur = ast.nodes[nodeIdx].child_first;
              while (cur >= 0) {
                int next = ast.nodes[cur].next_sibling;
                if (next >= 0 && ast.nodes[cur].kind == NodeKind::Repeat &&
                    ast.nodes[next].kind == NodeKind::Repeat &&
                    ast.nodes[cur].repeat_mode == ast.nodes[next].repeat_mode &&
                    !ast.nodes[cur].isPossessive() &&
                    nodesEqual(
                        ast,
                        ast.nodes[cur].child_first,
                        ast.nodes[next].child_first)) {
                  int minC =
                      ast.nodes[cur].min_repeat + ast.nodes[next].min_repeat;
                  int maxC = (ast.nodes[cur].max_repeat == -1 ||
                              ast.nodes[next].max_repeat == -1)
                      ? -1
                      : ast.nodes[cur].max_repeat + ast.nodes[next].max_repeat;
                  AstNode merged;
                  merged.kind = NodeKind::Repeat;
                  merged.min_repeat = minC;
                  merged.max_repeat = maxC;
                  merged.repeat_mode = ast.nodes[cur].repeat_mode;
                  merged.child_first = ast.nodes[cur].child_first;
                  merged.child_last = ast.nodes[cur].child_first;
                  int mergedIdx = ast.addNode(merged);
                  ast.nodes[nodeIdx].replaceChild(ast, cur, mergedIdx);
                  ast.nodes[nodeIdx].unlinkChild(ast, next);
                  cur = mergedIdx;
                } else {
                  cur = next;
                }
              }
            }
            return nodeIdx;
          }>,
      Case<
          NodeKind::Alternation,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            // Optimize each branch first
            int child = node.child_first;
            while (child >= 0) {
              int optimized = optimizeNode(ast, child);
              if (optimized != child) {
                ast.nodes[nodeIdx].replaceChild(ast, child, optimized);
                child = optimized;
              }
              child = ast.nodes[child].next_sibling;
            }

            // Duplicate elimination, subsumption, char merging, anchor
            // hoisting, then factoring
            int result = removeDuplicateBranches(ast, nodeIdx);
            result = eliminateSubsumedBranches(ast, result);
            result = mergeCharBranches(ast, result);
            result = hoistCommonAnchor(ast, result);
            result = factorGeneralizedPrefix(ast, result);
            result = factorGeneralizedSuffix(ast, result);
            result = factorAlternationPrefix(ast, result);
            result = factorAlternationSuffix(ast, result);
            return result;
          }>,
      Case<
          NodeKind::Group,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            int childFirst = node.child_first;
            int inner = optimizeNode(ast, childFirst);
            // Re-fetch after potential reallocation in optimizeNode.
            auto& grpNode = ast.nodes[nodeIdx];
            if (inner != childFirst) {
              grpNode.setOnlyChild(inner);
            }
            if (!grpNode.capturing) {
              auto childKind = ast.nodes[grpNode.child_first].kind;
              switch (childKind) {
                case NodeKind::Group:
                case NodeKind::Literal:
                case NodeKind::CharClass:
                case NodeKind::AnyByte:
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
                case NodeKind::Repeat:
                  return grpNode.child_first;
                case NodeKind::Sequence:
                case NodeKind::Alternation:
                  break;
              }
            }
            return nodeIdx;
          }>,
      Case<
          NodeKind::Repeat,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            int repChildFirst = node.child_first;
            int inner = optimizeNode(ast, repChildFirst);
            // Re-fetch after potential reallocation in optimizeNode.
            auto& repNode = ast.nodes[nodeIdx];
            if (inner != repChildFirst) {
              repNode.setOnlyChild(inner);
            }
            if (repNode.min_repeat == 0 && repNode.max_repeat == 0) {
              return addEmptyNode(ast);
            }
            if (repNode.min_repeat == 1 && repNode.max_repeat == 1) {
              if (repNode.isPossessive()) {
                // Atomic group desugared as {1,1}+ — propagate possessive
                // inward
                int child = repNode.child_first;
                // Unwrap non-capturing Group if present
                if (ast.nodes[child].kind == NodeKind::Group &&
                    !ast.nodes[child].capturing) {
                  child = ast.nodes[child].child_first;
                }
                if (ast.nodes[child].kind == NodeKind::Repeat) {
                  // (?>X{n,m}) → X{n,m}+
                  // Preserve the exact mode (Possessive vs PossessiveProbed for
                  // reverse AST) and transfer the probe ID.
                  ast.nodes[child].repeat_mode = repNode.repeat_mode;
                  ast.nodes[child].probe_id = repNode.probe_id;
                  return child;
                }
                if (isNonBacktracking(ast, child)) {
                  // (?>non-backtracking) — atomic is meaningless, unwrap
                  return child;
                }
                // (?>backtracking-non-repeat) — keep the {1,1}+ wrapper.
              } else {
                return repNode.child_first;
              }
            }
            // Quantified zero-width simplification: repeating a zero-width
            // match doesn't advance the position.
            if (repNode.child_first >= 0 &&
                ast.nodes[repNode.child_first].isZeroWidth()) {
              if (repNode.min_repeat == 0) {
                // \b* → \b?, ^{0,n} → ^?
                repNode.max_repeat = 1;
              } else {
                // \b+ → \b, ^{2,5} → ^
                return repNode.child_first;
              }
            }
            // Nested quantifier flattening.
            if (!repNode.isPossessive()) {
              int childIdx = repNode.child_first;
              const auto& child = ast.nodes[childIdx];
              if (child.kind == NodeKind::Group) {
                int gcIdx = child.child_first;
                if (gcIdx >= 0) {
                  const auto& gc = ast.nodes[gcIdx];
                  if (gc.kind == NodeKind::Repeat &&
                      gc.repeat_mode == repNode.repeat_mode) {
                    int charIdx = gc.child_first;
                    if (charIdx >= 0) {
                      auto ck = ast.nodes[charIdx].kind;
                      if (ck == NodeKind::Literal ||
                          ck == NodeKind::CharClass ||
                          ck == NodeKind::AnyByte) {
                        if (gc.max_repeat == -1 && repNode.max_repeat == -1) {
                          int flatMin = gc.min_repeat * repNode.min_repeat;
                          if (flatMin != gc.min_repeat) {
                            ast.nodes[gcIdx].min_repeat = flatMin;
                          }
                          return repNode.child_first;
                        }
                        if (!child.capturing &&
                            canFlattenNestedRepeats(
                                gc.min_repeat,
                                gc.max_repeat,
                                repNode.min_repeat,
                                repNode.max_repeat)) {
                          int flatMin = gc.min_repeat * repNode.min_repeat;
                          int flatMax =
                              (gc.max_repeat == -1 || repNode.max_repeat == -1)
                              ? -1
                              : gc.max_repeat * repNode.max_repeat;
                          return wrapInRepeat(
                              ast,
                              charIdx,
                              flatMin,
                              flatMax,
                              repNode.repeat_mode);
                        }
                      }
                    }
                  }
                }
              } else if (child.kind == NodeKind::Repeat) {
                if (child.repeat_mode == repNode.repeat_mode) {
                  int charIdx = child.child_first;
                  if (charIdx >= 0) {
                    auto ck = ast.nodes[charIdx].kind;
                    if (ck == NodeKind::Literal || ck == NodeKind::CharClass ||
                        ck == NodeKind::AnyByte) {
                      if (child.max_repeat == -1 && repNode.max_repeat == -1) {
                        int flatMin = child.min_repeat * repNode.min_repeat;
                        if (flatMin != child.min_repeat) {
                          ast.nodes[childIdx].min_repeat = flatMin;
                        }
                        return childIdx;
                      }
                      if (canFlattenNestedRepeats(
                              child.min_repeat,
                              child.max_repeat,
                              repNode.min_repeat,
                              repNode.max_repeat)) {
                        int flatMin = child.min_repeat * repNode.min_repeat;
                        int flatMax =
                            (child.max_repeat == -1 || repNode.max_repeat == -1)
                            ? -1
                            : child.max_repeat * repNode.max_repeat;
                        return wrapInRepeat(
                            ast,
                            charIdx,
                            flatMin,
                            flatMax,
                            repNode.repeat_mode);
                      }
                    }
                  }
                }
              }
            }
            return nodeIdx;
          }>,
      Case<NodeKind::Lookahead, Fallthrough>,
      Case<NodeKind::NegLookahead, Fallthrough>,
      Case<NodeKind::Lookbehind, Fallthrough>,
      Case<
          NodeKind::NegLookbehind,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            int laChildFirst = node.child_first;
            int inner = optimizeNode(ast, laChildFirst);
            if (inner != laChildFirst) {
              ast.nodes[nodeIdx].setOnlyChild(inner);
            }
            return nodeIdx;
          }>,
      Case<
          NodeKind::CharClass,
          [](auto& c) -> int {
            MutableAst auto& ast = c.ast;
            int nodeIdx = c.nodeIdx;
            auto& node = ast.nodes[nodeIdx];
            // Single-char CharClass to Literal: [a] → a
            if (node.char_class_index >= 0) {
              const auto& cc = ast.char_classes[node.char_class_index];
              if (cc.range_count == 1) {
                const auto& r = ast.ranges[cc.range_offset];
                if (r.lo == r.hi) {
                  AstNode lit;
                  lit.kind = NodeKind::Literal;
                  char ch = static_cast<char>(r.lo);
                  lit.literal = ast.appendLiteralChar(ch);
                  return ast.addNode(lit);
                }
              }
            }
            return nodeIdx;
          }>,
      Case<NodeKind::Empty, Fallthrough>,
      Case<NodeKind::Literal, Fallthrough>,
      Case<NodeKind::AnyByte, Fallthrough>,
      Case<NodeKind::Anchor, Fallthrough>,
      Case<NodeKind::WordBoundary, Fallthrough>,
      Case<NodeKind::NegWordBoundary, Fallthrough>,
      Case<NodeKind::Backref, Fallthrough>,
      Case<NodeKind::CaseInsensitiveBackref, Fallthrough>,
      Case<NodeKind::Dead, [](auto& c) -> int { return c.nodeIdx; }>>::
      dispatch(node.kind, ctx);
}

// --------------------------------------------------------------------------
// simplifyEmptyAlternation: convert alternations containing Empty branches
// (or branches that are bare Repeat{min_repeat=0}) into optional
// (Repeat{0,1}) quantifiers.
// --------------------------------------------------------------------------

constexpr int simplifyEmptyAlternation(
    MutableAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return nodeIdx;
  }

  auto& node = ast.nodes[nodeIdx];

  struct SEACtx {
    decltype(ast)& ast;
    int nodeIdx;
    decltype(node)& node;
  };
  SEACtx ctx{ast, nodeIdx, node};

  NodeKindSwitch<
      Case<NodeKind::Sequence, Fallthrough>,
      Case<
          NodeKind::Alternation,
          [](auto& c) {
            int child = c.node.child_first;
            while (child >= 0) {
              int nextSib = c.ast.nodes[child].next_sibling;
              int simplified = simplifyEmptyAlternation(c.ast, child);
              if (simplified != child) {
                c.ast.nodes[c.nodeIdx].replaceChild(c.ast, child, simplified);
                child = simplified;
              }
              child = nextSib;
            }
          }>,
      Case<NodeKind::Group, Fallthrough>,
      Case<
          NodeKind::Repeat,
          [](auto& c) {
            int inner = simplifyEmptyAlternation(c.ast, c.node.child_first);
            if (inner != c.node.child_first) {
              c.node.setOnlyChild(inner);
            }
          }>,
      Case<NodeKind::Lookahead, Fallthrough>,
      Case<NodeKind::NegLookahead, Fallthrough>,
      Case<NodeKind::Lookbehind, Fallthrough>,
      Case<
          NodeKind::NegLookbehind,
          [](auto& c) {
            int inner = simplifyEmptyAlternation(c.ast, c.node.child_first);
            if (inner != c.node.child_first) {
              c.node.setOnlyChild(inner);
            }
          }>,
      Case<NodeKind::Empty, Fallthrough>,
      Case<NodeKind::Literal, Fallthrough>,
      Case<NodeKind::AnyByte, Fallthrough>,
      Case<NodeKind::CharClass, Fallthrough>,
      Case<NodeKind::Anchor, Fallthrough>,
      Case<NodeKind::WordBoundary, Fallthrough>,
      Case<NodeKind::NegWordBoundary, Fallthrough>,
      Case<NodeKind::Backref, Fallthrough>,
      Case<NodeKind::CaseInsensitiveBackref, Fallthrough>,
      Case<NodeKind::Dead, [](auto&) {}>>::dispatch(node.kind, ctx);

  if (node.kind == NodeKind::Alternation) {
    int branch = node.child_first;
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
        b = ast.nodes[b].next_sibling;
      }
    }

    if (explicitEmptyCount == 0 && implicitEmptyCount == 0) {
      return nodeIdx;
    }

    int nonEmptyBranchCount = totalBranches - explicitEmptyCount;

    // All branches are explicit Empty → single Empty node.
    if (nonEmptyBranchCount == 0 && implicitEmptyCount == 0) {
      return addEmptyNode(ast);
    }

    // Promote implicit-empty branches and re-link, skipping explicit Empties.
    int newFirst = -1;
    int newLast = -1;
    int keptCount = 0;

    {
      int b = branch;
      int prevKept = -1;
      while (b >= 0) {
        int next = ast.nodes[b].next_sibling;
        if (ast.nodes[b].kind == NodeKind::Empty) {
          // Skip explicit Empty branches.
        } else {
          if (ast.nodes[b].kind == NodeKind::Repeat &&
              ast.nodes[b].min_repeat == 0) {
            ast.nodes[b].min_repeat = 1;
          }
          ast.nodes[b].prev_sibling = prevKept >= 0 ? prevKept : kNoNode;
          if (prevKept >= 0) {
            ast.nodes[prevKept].next_sibling = b;
          } else {
            newFirst = b;
          }
          prevKept = b;
          newLast = b;
          keptCount++;
        }
        b = next;
      }
      if (newLast >= 0) {
        ast.nodes[newLast].next_sibling = kNoNode;
      }
    }

    int innerNode;
    if (keptCount == 1) {
      innerNode = newFirst;
    } else {
      // Reuse the alternation node with updated children.
      node.child_first = newFirst;
      node.child_last = newLast;
      innerNode = nodeIdx;
    }

    return wrapInRepeat(
        ast,
        innerNode,
        0,
        1,
        firstIsExplicitEmpty ? RepeatMode::Lazy : RepeatMode::Greedy);
  }

  if (node.kind == NodeKind::Group && !node.capturing) {
    auto childKind = ast.nodes[node.child_first].kind;
    if (childKind != NodeKind::Sequence && childKind != NodeKind::Alternation) {
      return node.child_first;
    }
  }

  return nodeIdx;
}

// --------------------------------------------------------------------------
// optimizeTransformations: structural AST transformations (optimizeNode,
// simplifyEmptyAlternation). Does NOT include possessivePass —
// that runs separately after serialization in the inner pipeline.
// --------------------------------------------------------------------------

constexpr int optimizeTransformations(
    MutableAst auto& ast, int rootIdx, bool /*isReversed*/ = false) noexcept {
  int result = optimizeNode(ast, rootIdx);
  result = simplifyEmptyAlternation(ast, result);

  // Second pass: alternation factoring may create inner alternations with
  // single-char branches that weren't present during the first
  // mergeCharBranches pass, and non-capturing Groups around non-Sequence
  // nodes that need flattening.
  result = optimizeNode(ast, result);
  result = simplifyEmptyAlternation(ast, result);

  ast.root = result;
  return ast.root;
}

// --------------------------------------------------------------------------
// optimizeAnalyses: analysis passes (fixed widths, discriminators, prefix
// extraction, dot-star pruning, backtrack safety, min width).
// Does NOT include possessivePass.
//
// When skipSideChannelPasses is true: skips extractRootLiteralPrefix,
// computePrefixStripLen, stripRootLiteralPrefix, root non-capturing group
// flattening, detectAndPruneTrailingDotStar, detectAndPruneLeadingDotStar.
// The caller pre-populates prefix and dot-star fields on the builder.
// --------------------------------------------------------------------------

constexpr int optimizeAnalyses(
    MutableAst auto& ast,
    int /*rootIdx*/,
    bool isReversed = false,
    bool skipSideChannelPasses = false) noexcept {
  // Populate the fixed_width / fixed_width_prefix cache on every node so
  // the discriminator passes can do O(1) cache hits instead of recursive
  // subtree walks per (offset, branch) pair.
  precomputeFixedWidths(ast, ast.root);

  computeDiscriminatorsBottomUp(ast, ast.root);
  propagateDiscriminatorsTopDown(ast, ast.root, -1);
  materializeDiscriminatorCharClasses(ast, ast.root);
  freeDiscriminatorSets(ast, ast.root);

  if (!skipSideChannelPasses) {
    extractRootLiteralPrefix(ast);
    if (isReversed) {
      computePrefixStripLen(ast);
    }
    stripRootLiteralPrefix(ast);

    // Flatten again — prefix stripping may expose a non-capturing
    // Group at the root (e.g., after stripping a literal prefix from
    // Sequence(Literal, Group(nc, Alt(...)))).
    while (ast.root >= 0 && ast.nodes[ast.root].kind == NodeKind::Group &&
           !ast.nodes[ast.root].capturing) {
      ast.root = ast.nodes[ast.root].child_first;
    }

    detectAndPruneTrailingDotStar(ast);
    detectAndPruneLeadingDotStar(ast);
  }

  // The post-discriminator passes above only mutate (in place) the root
  // node and its direct children — strip/extract create fresh root nodes,
  // and the dot-star pruners markDead / overwrite the kind of root or
  // root's immediate children. All deeper subtrees are structurally
  // unchanged, so their cached widths remain valid. Just invalidate the
  // possibly-stale entries; subsequent computeFixedWidth calls will
  // recompute these by reading the still-valid grandchild caches.
  if (ast.root >= 0) {
    auto& rn = ast.nodes[ast.root];
    rn.fixed_width = -2;
    rn.fixed_width_prefix = -1;
    for (int c = rn.child_first; c >= 0; c = ast.nodes[c].next_sibling) {
      auto& cn = ast.nodes[c];
      cn.fixed_width = -2;
      cn.fixed_width_prefix = -1;
    }
  }

  ast.backtrack_safe = computeBacktrackSafe(ast, ast.root);

  ast.min_width = computeMinWidth(ast, ast.root) + ast.prefix_strip_len;

  return ast.root;
}

// --------------------------------------------------------------------------
// optimizeAst: top-level entry point (thin wrapper).
// Calls optimizeTransformations + possessivePass + optimizeAnalyses.
// --------------------------------------------------------------------------

constexpr int optimizeAst(
    MutableAst auto& ast, int rootIdx, bool isReversed = false) noexcept {
  optimizeTransformations(ast, rootIdx, isReversed);
  possessivePass(
      ast,
      ast.root,
      FirstCharFilter{.accepts_all = false},
      PossessivePassMode::Promote);
  return optimizeAnalyses(ast, ast.root, isReversed);
}

} // namespace folly::regex::detail
