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

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Direction.h>

namespace folly::regex::detail {

struct GroupSpan {
  std::size_t offset = std::string_view::npos;
  std::size_t length = 0;
};

template <int NumGroups>
struct MatchState {
  std::array<GroupSpan, NumGroups + 1> groups = {};
};

enum class MatchStatus {
  Matched,
  NoMatch,
  BudgetExhausted,
};

template <int NumGroups>
struct MatchOutcome {
  MatchStatus status = MatchStatus::NoMatch;
  MatchState<NumGroups> state;
};

constexpr bool hasAnchorBegin(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Anchor &&
      (node.anchor == AnchorKind::Begin ||
       node.anchor == AnchorKind::StartOfString)) {
    return true;
  }
  if (node.kind == NodeKind::Sequence) {
    int child = node.child_first;
    if (child >= 0) {
      return hasAnchorBegin(ast, child);
    }
  }
  if (node.kind == NodeKind::Group) {
    return hasAnchorBegin(ast, node.child_first);
  }
  return false;
}

// Check if the AST contains any anchor node. Used to skip building the
// unanchored DFA, because it is expensive. Covers all AnchorKind
// values: ^, $, \A, \z, \Z, and multiline ^/$ (BeginLine/EndLine).
constexpr bool hasAnyAnchor(const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return false;
  }
  const auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Anchor) {
    return true;
  }
  if (node.kind == NodeKind::Sequence || node.kind == NodeKind::Alternation) {
    int child = node.child_first;
    while (child >= 0) {
      if (hasAnyAnchor(ast, child)) {
        return true;
      }
      child = ast.nodes[child].next_sibling;
    }
  }
  if (node.kind == NodeKind::Group || node.kind == NodeKind::Repeat) {
    return hasAnyAnchor(ast, node.child_first);
  }
  return false;
}

// --------------------------------------------------------------------------
// Trailing dot-star extension helpers. Used by all executors to compute
// the match boundary analytically after the pruned dot-star.
// --------------------------------------------------------------------------

// Given a relative match boundary position within the InputView's active
// region, extend it to account for the pruned trailing dot-star. Returns
// a relative position, or npos to signal "anchor condition not met".
// The extension scans original_input beyond the active region.
template <Direction Dir>
std::size_t computeDotStarExtension(
    InputView<Dir> input,
    std::size_t relPos,
    bool dotAll,
    int anchorKind) noexcept {
  constexpr auto npos = std::string_view::npos;
  auto absPos = input.posInFull(relPos);
  auto absBase = input.posInFull(0);
  auto fullStr = input.original_input;

  if constexpr (Dir == Direction::Forward) {
    // Extend match END rightward.
    if (dotAll) {
      return fullStr.size() - absBase;
    }
    // Non-dotAll: scan for \n.
    const char* start = fullStr.data() + absPos;
    std::size_t remaining = fullStr.size() - absPos;
    const void* found = std::memchr(start, '\n', remaining);
    if (!found) {
      return fullStr.size() - absBase;
    }
    std::size_t nlPos = static_cast<std::size_t>(
        static_cast<const char*>(found) - fullStr.data());
    if (anchorKind < 0) {
      return nlPos - absBase;
    }
    if (anchorKind == static_cast<int>(AnchorKind::EndOfString)) {
      return npos;
    }
    // \Z: accept if \n is the last character.
    if (nlPos + 1 == fullStr.size()) {
      return nlPos - absBase;
    }
    return npos;
  } else {
    // Extend match START leftward.
    if (dotAll) {
      return 0 - absBase; // will be 0 when absBase is 0
    }
    // Non-dotAll: reverse-scan for \n from absPos toward 0.
    std::size_t nlPos = npos;
    for (std::size_t i = absPos; i > 0; --i) {
      if (fullStr[i - 1] == '\n') {
        nlPos = i;
        break;
      }
    }
    if (nlPos == npos) {
      return 0 - absBase;
    }
    if (anchorKind < 0) {
      return nlPos - absBase;
    }
    if (anchorKind == static_cast<int>(AnchorKind::EndOfString)) {
      return npos;
    }
    // \Z: accept if \n is at position 0.
    if (nlPos == 1 && fullStr[0] == '\n') {
      return 1 - absBase;
    }
    return npos;
  }
}
constexpr bool hasTrailingDotStarAnchors(const ReadOnlyAst auto& ast) noexcept {
  return ast.trailing_dot_star_min >= 0 && !ast.trailing_dot_star_dot_all &&
      ast.trailing_dot_star_anchor >= 0;
}
template <Direction Dir>
std::size_t computeLeadingDotStarExtension(
    InputView<Dir> input,
    std::size_t relPos,
    bool dotAll,
    int anchorKind) noexcept {
  constexpr auto npos = std::string_view::npos;
  auto absPos = input.posInFull(relPos);
  auto absBase = input.posInFull(0);
  auto fullStr = input.original_input;

  if constexpr (Dir == Direction::Forward) {
    // Extend match START leftward toward 0.
    if (dotAll) {
      return 0 - absBase;
    }
    // Non-dotAll: reverse-scan for \n from absPos toward 0.
    for (std::size_t i = absPos; i > 0; --i) {
      if (fullStr[i - 1] == '\n') {
        // Found \n — match start is after the \n.
        if (anchorKind < 0) {
          return i - absBase;
        }
        if (anchorKind == static_cast<int>(AnchorKind::StartOfString)) {
          return npos; // \A requires position 0, \n blocks it
        }
        // ^ (Begin) with multiline: position after \n is valid
        return i - absBase;
      }
    }
    // No \n found — start extends to 0.
    return 0 - absBase;
  } else {
    // Reverse direction: extend match END rightward.
    if (dotAll) {
      return fullStr.size() - absBase;
    }
    // Non-dotAll: scan for \n from absPos rightward.
    const char* start = fullStr.data() + absPos;
    std::size_t remaining = fullStr.size() - absPos;
    const void* found = std::memchr(start, '\n', remaining);
    if (!found) {
      return fullStr.size() - absBase;
    }
    std::size_t nlPos = static_cast<std::size_t>(
        static_cast<const char*>(found) - fullStr.data());
    if (anchorKind < 0) {
      return nlPos - absBase;
    }
    if (anchorKind == static_cast<int>(AnchorKind::StartOfString)) {
      return npos;
    }
    return nlPos - absBase;
  }
}

constexpr FirstCharFilter extractFirstCharFilter(
    const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return {.accepts_all = true};
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal: {
      FirstCharFilter f;
      f.accepts_all = false;
      f.addChar(static_cast<unsigned char>(node.literal[0]));
      return f;
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
    case NodeKind::Empty:
      return {.accepts_all = true};
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return {.accepts_all = true};
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return {.accepts_all = true};
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
      return {.accepts_all = true};
    case NodeKind::Group:
      return extractFirstCharFilter(ast, node.child_first);
    case NodeKind::Sequence: {
      int child = node.child_first;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f = extractFirstCharFilter(ast, child);
      if (f.accepts_all && ast.nodes[child].kind == NodeKind::Anchor) {
        int next = ast.nodes[child].next_sibling;
        if (next >= 0) {
          return extractFirstCharFilter(ast, next);
        }
      }
      return f;
    }
    case NodeKind::Alternation: {
      int child = node.child_first;
      if (child < 0) {
        return {.accepts_all = true};
      }
      auto f = extractFirstCharFilter(ast, child);
      if (f.accepts_all) {
        return f;
      }
      int next = ast.nodes[child].next_sibling;
      while (next >= 0) {
        auto alt = extractFirstCharFilter(ast, next);
        if (alt.accepts_all) {
          return alt;
        }
        f.mergeFrom(alt.ranges, alt.range_count);
        next = ast.nodes[next].next_sibling;
      }
      return f;
    }
    case NodeKind::Repeat: {
      if (node.min_repeat > 0) {
        return extractFirstCharFilter(ast, node.child_first);
      }
      return {.accepts_all = true};
    }
  }
  return {.accepts_all = true};
}

struct RequiredLiteral {
  char ch = 0;
  bool found = false;
};

constexpr RequiredLiteral extractRequiredLiteral(
    const ReadOnlyAst auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return {};
  }

  const auto& node = ast.nodes[nodeIdx];

  switch (node.kind) {
    case NodeKind::Literal:
      return {node.literal[0], true};
    case NodeKind::Sequence: {
      int child = node.child_first;
      while (child >= 0) {
        auto r = extractRequiredLiteral(ast, child);
        if (r.found) {
          return r;
        }
        // Stop at the first input-consuming element that has no required
        // literal. The memchr search path back-walks from the literal
        // position through first-char-filter-matching characters to find
        // the match start. If a consuming element (like an alternation
        // or char class) sits between the match start and the literal,
        // the back-walk cannot bridge the gap because intermediate
        // characters may not pass the first-char filter.
        if (!ast.nodes[child].isZeroWidth()) {
          return {};
        }
        child = ast.nodes[child].next_sibling;
      }
      return {};
    }
    case NodeKind::Group:
      return extractRequiredLiteral(ast, node.child_first);
    case NodeKind::Repeat:
      if (node.min_repeat > 0) {
        return extractRequiredLiteral(ast, node.child_first);
      }
      return {};
    case NodeKind::Alternation:
    case NodeKind::CharClass:
    case NodeKind::AnyByte:
    case NodeKind::Empty:
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
      return {};
  }
  return {};
}

// Extract a single first character from a FirstCharFilter when the bitmap
// has exactly one bit set.  Used to enable memchr skip in the non-memchr
// search path (when the required-literal memchr condition doesn't trigger
// because the required literal IS the first char).
constexpr RequiredLiteral extractSingleFirstChar(
    const FirstCharFilter& filter) noexcept {
  if (filter.accepts_all) {
    return {};
  }
  if (filter.range_count == 1 && filter.ranges[0].lo == filter.ranges[0].hi) {
    return {static_cast<char>(filter.ranges[0].lo), true};
  }
  return {};
}

} // namespace folly::regex::detail
