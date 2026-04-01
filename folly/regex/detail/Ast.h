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

#include <cstddef>
#include <cstdint>

#include <folly/regex/detail/CharClass.h>

namespace folly {
namespace regex {
namespace detail {

enum class NodeKind : uint8_t {
  Empty,
  Literal,
  AnyChar,
  AnyByte,
  CharClass,
  Sequence,
  Alternation,
  Repeat,
  Group,
  Anchor,
  WordBoundary,
  NegWordBoundary,
  Lookahead,
  NegLookahead,
  Lookbehind,
  NegLookbehind,
  Backref,
};

enum class AnchorKind : uint8_t {
  Begin,
  End,
  StartOfString,
  EndOfString,
  EndOfStringOrNewline,
  BeginLine,
  EndLine,
};

struct AstNode {
  NodeKind kind = NodeKind::Empty;
  char ch = '\0';
  bool negated = false;
  bool capturing = false;
  bool greedy = true;
  bool possessive = false;
  AnchorKind anchor = AnchorKind::Begin;
  int group_id = 0;
  int min_repeat = 0;
  int max_repeat = -1; // -1 = unlimited
  int child_begin = 0;
  int child_end = 0;
  int char_class_index = -1;
};

// Threshold: character classes with >= this many ranges get a compact bitmap.
inline constexpr int kBitmapRangeThreshold = 5;

struct CharClassEntry {
  int range_offset = 0;
  int range_count = 0;
  bool negated = false;
  int bitmap_offset = -1;
  int bitmap_word_count = 0;
  unsigned char bitmap_lo = 0;
};

template <
    std::size_t MaxNodes,
    std::size_t MaxRanges,
    std::size_t MaxClasses,
    std::size_t MaxBitmapWords = 0>
struct ParseResult {
  AstNode nodes[MaxNodes] = {};
  CharRange ranges[MaxRanges] = {};
  CharClassEntry char_classes[MaxClasses] = {};
  uint64_t bitmap_words[MaxBitmapWords > 0 ? MaxBitmapWords : 1] = {};
  int node_count = 0;
  int group_count = 0;
  int total_ranges = 0;
  int char_class_count = 0;
  int total_bitmap_words = 0;
  int root = -1;
  bool valid = false;
  bool nfa_compatible = true;
  bool has_lookbehind = false;
  bool backtrack_safe = false;
  char stripped_prefix[64] = {};
  int stripped_prefix_len = 0;
  std::size_t error_pos = 0;
  char error_message[128] = {};

  constexpr int addNode(AstNode node) noexcept {
    int idx = node_count++;
    nodes[idx] = node;
    return idx;
  }

  constexpr int addCharClass(const CharRangeSet& rs) noexcept {
    int ccIdx = char_class_count++;
    auto& cc = char_classes[ccIdx];
    cc.range_offset = total_ranges;
    cc.negated = rs.negated();
    int count = 0;
    rs.forEach([&](CharRange r) {
      ranges[total_ranges++] = r;
      ++count;
    });
    cc.range_count = count;

    // Materialize compact bitmap for classes with enough ranges
    if (count >= kBitmapRangeThreshold && count > 0) {
      unsigned char lo = ranges[cc.range_offset].lo;
      unsigned char hi = ranges[cc.range_offset + count - 1].hi;
      int span = static_cast<int>(hi) - static_cast<int>(lo) + 1;
      int wordCount = (span + 63) / 64;

      if (total_bitmap_words + wordCount <=
          static_cast<int>(MaxBitmapWords > 0 ? MaxBitmapWords : 1)) {
        cc.bitmap_offset = total_bitmap_words;
        cc.bitmap_word_count = wordCount;
        cc.bitmap_lo = lo;

        // Build bitmap using O(ranges) word-mask operations
        for (int i = 0; i < count; ++i) {
          setRangeBits(
              bitmap_words + cc.bitmap_offset,
              lo,
              ranges[cc.range_offset + i].lo,
              ranges[cc.range_offset + i].hi);
        }
        total_bitmap_words += wordCount;
      }
    }

    return ccIdx;
  }

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    if (cc.bitmap_offset >= 0) {
      return compactBitmapTest(
          bitmap_words + cc.bitmap_offset,
          cc.bitmap_lo,
          cc.bitmap_word_count,
          cc.negated,
          c);
    }
    return charClassTest(
        ranges + cc.range_offset, cc.range_count, cc.negated, c);
  }

  constexpr void setError(std::size_t pos, const char* msg) noexcept {
    valid = false;
    error_pos = pos;
    std::size_t i = 0;
    while (msg[i] != '\0' && i < 127) {
      error_message[i] = msg[i];
      ++i;
    }
    error_message[i] = '\0';
  }
};

template <std::size_t PatLen>
using ParseResultFor = ParseResult<
    (PatLen < 4 ? 8 : PatLen * 2),
    (PatLen < 4 ? 16 : PatLen * 4),
    (PatLen < 4 ? 4 : PatLen),
    (PatLen < 4 ? 8 : PatLen * 2)>;

} // namespace detail
} // namespace regex
} // namespace folly
