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

// Reverse AST transformation and dot-star pruning for the regex optimizer.
//
// isDotCharClass: checks if a CharClass matches the non-dotAll dot pattern.
// detectAndPruneTrailingDotStar: removes trailing .* / .+ from the pattern.
// detectAndPruneLeadingDotStar: removes leading .* / .+ from the pattern.
// reverseAstNode / reverseAst: reverses sequence child order and literal
// characters for reverse matching.

#pragma once

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/optimizer/AstHelpers.h>

namespace folly::regex::detail {

// Check if a CharClass node matches the non-dotAll dot pattern: [0,9],[11,255].
constexpr bool isDotCharClass(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind != NodeKind::CharClass || node.char_class_index < 0) {
    return false;
  }
  const auto& cc = ast.char_classes[node.char_class_index];
  if (cc.range_count != 2) {
    return false;
  }
  const auto& r0 = ast.ranges[cc.range_offset];
  const auto& r1 = ast.ranges[cc.range_offset + 1];
  return r0.lo == 0 && r0.hi == 9 && r1.lo == 11 && r1.hi == 255;
}

// Detect and prune trailing .* / .+ from the pattern. When detected,
// the repeat (and any adjacent trailing anchor) is removed from the AST
// and metadata flags are set on the builder for the executors to use.
constexpr void detectAndPruneTrailingDotStar(MutableAst auto& ast) noexcept {
  if (ast.root < 0) {
    return;
  }

  int rootIdx = ast.root;
  const auto& rootNode = ast.nodes[rootIdx];

  // Check if the root itself is a qualifying Repeat (standalone .* / .+).
  if (rootNode.kind == NodeKind::Repeat && rootNode.max_repeat == -1 &&
      (rootNode.min_repeat == 0 || rootNode.min_repeat == 1)) {
    int inner = rootNode.child_first;
    if (inner >= 0) {
      bool isDotAll = ast.nodes[inner].kind == NodeKind::AnyByte;
      bool isDot = isDotCharClass(ast, inner);
      if (isDotAll || isDot) {
        ast.trailing_dot_star_min = rootNode.min_repeat;
        ast.trailing_dot_star_dot_all = isDotAll;
        ast.trailing_dot_star_anchor = -1;

        if (rootNode.min_repeat == 0) {
          // .* → replace root with Empty
          ast.root = addEmptyNode(ast);
        } else {
          // .+ → mutate to single char test
          auto& rn = ast.nodes[rootIdx];
          if (isDotAll) {
            rn.kind = NodeKind::AnyByte;
          } else {
            rn.kind = NodeKind::CharClass;
            rn.char_class_index = ast.nodes[inner].char_class_index;
          }
          rn.child_first = kNoNode;
          rn.child_last = kNoNode;
          rn.min_repeat = 0;
          rn.max_repeat = -1;
          ast.markDead(inner);
        }
        return;
      }
    }
  }

  // Root must be a Sequence for trailing element detection.
  if (rootNode.kind != NodeKind::Sequence) {
    return;
  }

  // Always check trailing (last) children regardless of direction.
  // Each AST independently detects its own trailing dot-star.
  NodeIdx repeatNode = kNoNode;
  int anchorKind = -1;

  {
    // Walk from last to first, skipping Empty/Dead.
    int last = rootNode.child_last;
    NodeIdx candidate = last;
    while (candidate >= 0 &&
           (ast.nodes[candidate].kind == NodeKind::Empty ||
            ast.nodes[candidate].kind == NodeKind::Dead)) {
      candidate = ast.nodes[candidate].prev_sibling;
    }
    if (candidate < 0) {
      return;
    }
    // Check for trailing anchor.
    if (ast.nodes[candidate].kind == NodeKind::Anchor) {
      auto ak = ast.nodes[candidate].anchor;
      if (ak == AnchorKind::EndOfString ||
          ak == AnchorKind::EndOfStringOrNewline) {
        anchorKind = static_cast<int>(ak);
        candidate = ast.nodes[candidate].prev_sibling;
        while (candidate >= 0 &&
               (ast.nodes[candidate].kind == NodeKind::Empty ||
                ast.nodes[candidate].kind == NodeKind::Dead)) {
          candidate = ast.nodes[candidate].prev_sibling;
        }
        if (candidate < 0) {
          return;
        }
      }
    }
    repeatNode = candidate;
  }

  // Check if the candidate is a qualifying Repeat.
  if (repeatNode < 0) {
    return;
  }
  const auto& rn = ast.nodes[repeatNode];
  if (rn.kind != NodeKind::Repeat || rn.max_repeat != -1 ||
      (rn.min_repeat != 0 && rn.min_repeat != 1)) {
    return;
  }
  int inner = rn.child_first;
  if (inner < 0) {
    return;
  }
  bool isDotAll = ast.nodes[inner].kind == NodeKind::AnyByte;
  bool isDot = isDotCharClass(ast, inner);
  if (!isDotAll && !isDot) {
    return;
  }

  // Pattern qualifies. Set metadata flags.
  ast.trailing_dot_star_min = rn.min_repeat;
  ast.trailing_dot_star_dot_all = isDotAll;
  ast.trailing_dot_star_anchor = anchorKind;

  // Prune the repeat and any anchor from the sequence.
  if (rn.min_repeat == 0) {
    // .* → unlink from the sequence and mark as Dead.
    ast.nodes[rootIdx].unlinkChild(ast, repeatNode);
    ast.markDeadRecursive(repeatNode);
  } else {
    // .+ → mutate to single char test.
    auto& mutNode = ast.nodes[repeatNode];
    if (isDotAll) {
      mutNode.kind = NodeKind::AnyByte;
    } else {
      mutNode.kind = NodeKind::CharClass;
      mutNode.char_class_index = ast.nodes[inner].char_class_index;
    }
    mutNode.child_first = kNoNode;
    mutNode.child_last = kNoNode;
    mutNode.min_repeat = 0;
    mutNode.max_repeat = -1;
    ast.markDead(inner);
  }

  // Unlink adjacent trailing anchor if present.
  if (anchorKind >= 0) {
    int last = ast.nodes[rootIdx].child_last;
    NodeIdx cur = last;
    while (cur >= 0) {
      if (ast.nodes[cur].kind == NodeKind::Anchor) {
        auto ak = ast.nodes[cur].anchor;
        if (ak == AnchorKind::EndOfString ||
            ak == AnchorKind::EndOfStringOrNewline) {
          ast.nodes[rootIdx].unlinkChild(ast, cur);
          ast.markDead(cur);
          break;
        }
      }
      if (ast.nodes[cur].kind != NodeKind::Empty &&
          ast.nodes[cur].kind != NodeKind::Dead) {
        break;
      }
      cur = ast.nodes[cur].prev_sibling;
    }
  }
}

// Detect and prune a leading .* / .+ from the pattern. Mirror of
// detectAndPruneTrailingDotStar but for the front of the root Sequence.
// For forward AST: check first children.
// For reverse AST: check last children (original leading became trailing).
constexpr void detectAndPruneLeadingDotStar(MutableAst auto& ast) noexcept {
  if (ast.root < 0) {
    return;
  }
  // Skip if a literal prefix was stripped — the dot-star isn't truly leading.
  if (ast.prefix_strip_len > 0) {
    return;
  }

  int rootIdx = ast.root;
  const auto& rootNode = ast.nodes[rootIdx];

  // Standalone root Repeat is already handled by trailing detection.
  if (rootNode.kind != NodeKind::Sequence) {
    return;
  }

  NodeIdx repeatNode = kNoNode;
  int anchorKind = -1;

  {
    // Always check leading (first) children regardless of direction.
    NodeIdx candidate = rootNode.child_first;
    while (candidate >= 0 &&
           (ast.nodes[candidate].kind == NodeKind::Empty ||
            ast.nodes[candidate].kind == NodeKind::Dead)) {
      candidate = ast.nodes[candidate].next_sibling;
    }
    if (candidate < 0) {
      return;
    }
    // Check for leading anchor (\A or ^).
    if (ast.nodes[candidate].kind == NodeKind::Anchor) {
      auto ak = ast.nodes[candidate].anchor;
      if (ak == AnchorKind::StartOfString || ak == AnchorKind::Begin) {
        anchorKind = static_cast<int>(ak);
        candidate = ast.nodes[candidate].next_sibling;
        while (candidate >= 0 &&
               (ast.nodes[candidate].kind == NodeKind::Empty ||
                ast.nodes[candidate].kind == NodeKind::Dead)) {
          candidate = ast.nodes[candidate].next_sibling;
        }
        if (candidate < 0) {
          return;
        }
      }
    }
    repeatNode = candidate;
  }

  if (repeatNode < 0) {
    return;
  }
  const auto& rn = ast.nodes[repeatNode];
  if (rn.kind != NodeKind::Repeat || rn.max_repeat != -1 ||
      (rn.min_repeat != 0 && rn.min_repeat != 1)) {
    return;
  }
  int inner = rn.child_first;
  if (inner < 0) {
    return;
  }
  bool isDotAll = ast.nodes[inner].kind == NodeKind::AnyByte;
  bool isDot = isDotCharClass(ast, inner);
  if (!isDotAll && !isDot) {
    return;
  }

  // Pattern qualifies. Set metadata flags.
  ast.leading_dot_star_min = rn.min_repeat;
  ast.leading_dot_star_dot_all = isDotAll;
  ast.leading_dot_star_anchor = anchorKind;

  // Prune the repeat.
  if (rn.min_repeat == 0) {
    // .* → unlink from the sequence and mark as Dead.
    ast.nodes[rootIdx].unlinkChild(ast, repeatNode);
    ast.markDeadRecursive(repeatNode);
  } else {
    // .+ → mutate to single char test (still requires one char).
    auto& mutNode = ast.nodes[repeatNode];
    if (isDotAll) {
      mutNode.kind = NodeKind::AnyByte;
    } else {
      mutNode.kind = NodeKind::CharClass;
      mutNode.char_class_index = ast.nodes[inner].char_class_index;
    }
    mutNode.child_first = kNoNode;
    mutNode.child_last = kNoNode;
    mutNode.min_repeat = 0;
    mutNode.max_repeat = -1;
    ast.markDead(inner);
  }

  // Unlink adjacent leading anchor if present.
  if (anchorKind >= 0) {
    NodeIdx cur = ast.nodes[rootIdx].child_first;
    while (cur >= 0) {
      if (ast.nodes[cur].kind == NodeKind::Anchor) {
        auto ak = ast.nodes[cur].anchor;
        if (ak == AnchorKind::StartOfString || ak == AnchorKind::Begin) {
          ast.nodes[rootIdx].unlinkChild(ast, cur);
          ast.markDead(cur);
          break;
        }
      }
      if (ast.nodes[cur].kind != NodeKind::Empty &&
          ast.nodes[cur].kind != NodeKind::Dead) {
        break;
      }
      cur = ast.nodes[cur].next_sibling;
    }
  }
}

constexpr void reverseAstNode(MutableAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }

  auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Sequence: {
      auto tmp = node.child_first;
      node.child_first = node.child_last;
      node.child_last = tmp;
      int child = node.child_first;
      while (child >= 0) {
        auto& cn = ast.nodes[child];
        auto tmpSib = cn.next_sibling;
        cn.next_sibling = cn.prev_sibling;
        cn.prev_sibling = tmpSib;
        reverseAstNode(ast, child);
        child = cn.next_sibling;
      }
      break;
    }
    case NodeKind::Literal: {
      // Reverse literal characters so that the optimizer sees the
      // pattern in true reverse order, enabling correct prefix/suffix
      // factoring without direction-specific workarounds.
      auto lit = node.literal;
      int len = static_cast<int>(lit.size());
      if (len > 1) {
        char buf[256] = {};
        for (int i = 0; i < len; ++i) {
          buf[i] = lit[len - 1 - i];
        }
        node.literal = ast.appendLiteral(buf, len);
      }
      break;
    }
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        reverseAstNode(ast, child);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Repeat:
      if (node.repeat_mode == RepeatMode::Possessive) {
        node.repeat_mode = RepeatMode::PossessiveProbed;
      }
      reverseAstNode(ast, node.child_first);
      break;
    case NodeKind::Group:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      reverseAstNode(ast, node.child_first);
      break;
    case NodeKind::Empty:
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

constexpr void reverseAst(MutableAst auto& ast, int rootIdx) noexcept {
  reverseAstNode(ast, rootIdx);
}

} // namespace folly::regex::detail
