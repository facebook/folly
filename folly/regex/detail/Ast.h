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
#include <string_view>

#include <folly/regex/detail/CharClass.h>

namespace folly {
namespace regex {
namespace detail {

using NodeIdx = int;
constexpr NodeIdx kNoNode = -1;

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
  Dead,
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

struct CharClassEntry {
  int range_offset = 0;
  int range_count = 0;
};

// Chain of fixed-size blocks for stable-pointer storage.
// Pointers into earlier blocks remain valid when new blocks are added.
// Non-copyable/non-movable to prevent pointer invalidation.
template <typename T, int BlockSize = 64>
struct ChunkedBuffer {
  struct Block {
    T data[BlockSize] = {};
    int count = 0;
    Block* next = nullptr;
  };

  Block first_;
  Block* tail_ = &first_;
  int total_count_ = 0;

  // Append n items contiguously. Returns pointer to first appended item.
  // All n items are placed in a single block for contiguity.
  constexpr T* append(T* items, int n) noexcept {
    if (n <= 0) {
      return nullptr;
    }
    if (tail_->count + n > BlockSize) {
      auto* newBlock = new Block{};
      tail_->next = newBlock;
      tail_ = newBlock;
    }
    T* result = &tail_->data[tail_->count];
    for (int i = 0; i < n; ++i) {
      tail_->data[tail_->count++] = static_cast<T&&>(items[i]);
    }
    total_count_ += n;
    return result;
  }

  // Find the linear offset of a pointer into the buffer.
  // Uses equality comparison only (constexpr-safe across allocations).
  constexpr int findOffset(const T* ptr) const noexcept {
    int cumOffset = 0;
    const Block* b = &first_;
    while (b) {
      for (int j = 0; j < b->count; ++j) {
        if (ptr == &b->data[j]) {
          return cumOffset + j;
        }
      }
      cumOffset += b->count;
      b = b->next;
    }
    return -1;
  }

  // Copy all data into a contiguous output array.
  constexpr void linearize(T* out) const noexcept {
    int offset = 0;
    const Block* b = &first_;
    while (b) {
      for (int j = 0; j < b->count; ++j) {
        out[offset++] = b->data[j];
      }
      b = b->next;
    }
  }

  constexpr ~ChunkedBuffer() {
    Block* b = first_.next;
    while (b) {
      Block* next = b->next;
      delete b;
      b = next;
    }
  }

  ChunkedBuffer(const ChunkedBuffer&) = delete;
  ChunkedBuffer& operator=(const ChunkedBuffer&) = delete;
  ChunkedBuffer(ChunkedBuffer&&) = delete;
  ChunkedBuffer& operator=(ChunkedBuffer&&) = delete;
  constexpr ChunkedBuffer() noexcept = default;
};

// Transient discriminator data for a single valid offset within an
// alternation. Stores the union of all leaf characters at that offset
// as a sorted CharRange array. Heap-allocated during optimization via
// ChunkedBuffer<DiscriminatorEntry>, freed before compact().
struct DiscriminatorEntry {
  int offset = -1;
  static constexpr int kMaxRanges = 16;
  CharRange ranges[kMaxRanges] = {};
  int range_count = 0;
};

struct AstNode {
  NodeKind kind = NodeKind::Empty;
  std::string_view literal;
  bool capturing = false;
  bool greedy = true;
  bool possessive = false;
  AnchorKind anchor = AnchorKind::Begin;
  int group_id = 0;
  int min_repeat = 0;
  int max_repeat = -1; // -1 = unlimited
  NodeIdx child_begin = kNoNode;
  NodeIdx child_end = kNoNode;
  int char_class_index = -1;
  int discriminator_offset = -1;
  ChunkedBuffer<DiscriminatorEntry>* valid_discriminators = nullptr;

  /// Returns true if this node is inherently zero-width (consumes no input).
  constexpr bool isZeroWidth() const noexcept {
    switch (kind) {
      case NodeKind::Empty:
      case NodeKind::Anchor:
      case NodeKind::WordBoundary:
      case NodeKind::NegWordBoundary:
      case NodeKind::Lookahead:
      case NodeKind::NegLookahead:
      case NodeKind::Lookbehind:
      case NodeKind::NegLookbehind:
      case NodeKind::Dead:
        return true;
      case NodeKind::Literal:
      case NodeKind::AnyChar:
      case NodeKind::AnyByte:
      case NodeKind::CharClass:
      case NodeKind::Sequence:
      case NodeKind::Alternation:
      case NodeKind::Repeat:
      case NodeKind::Group:
      case NodeKind::Backref:
        return false;
    }
  }
};

struct LiteralEntry {
  char* data = nullptr;
  int len = 0;

  constexpr LiteralEntry() noexcept = default;
  constexpr LiteralEntry(char* d, int l) noexcept : data(d), len(l) {}

  constexpr ~LiteralEntry() { delete[] data; }

  LiteralEntry(const LiteralEntry&) = delete;
  LiteralEntry& operator=(const LiteralEntry&) = delete;

  constexpr LiteralEntry(LiteralEntry&& o) noexcept : data(o.data), len(o.len) {
    o.data = nullptr;
    o.len = 0;
  }
  constexpr LiteralEntry& operator=(LiteralEntry&& o) noexcept {
    if (this != &o) {
      delete[] data;
      data = o.data;
      len = o.len;
      o.data = nullptr;
      o.len = 0;
    }
    return *this;
  }
};

struct AstBuilder {
  AstNode* nodes = nullptr;
  int node_count = 0;
  int node_capacity = 0;

  CharRange* ranges = nullptr;
  int total_ranges = 0;
  int range_capacity = 0;

  CharClassEntry* char_classes = nullptr;
  int char_class_count = 0;
  int char_class_capacity = 0;

  ChunkedBuffer<LiteralEntry> literal_store;

  int group_count = 0;
  NodeIdx root = kNoNode;
  bool valid = false;
  bool nfa_compatible = true;
  bool has_lookbehind = false;
  bool backtrack_safe = false;

  // Unified literal buffer: prefix occupies [0..prefix_len-1],
  // suffix occupies [literal_buf_size-suffix_len..literal_buf_size-1].
  char* literal_buf = nullptr;
  int literal_buf_size = 0;
  int prefix_len = 0;
  int suffix_len = 0;

  std::size_t error_pos = 0;
  char error_message[128] = {};

  constexpr AstBuilder(
      int nodesCap = 32, int rangesCap = 32, int classesCap = 8, int patLen = 0)
      : nodes(new AstNode[nodesCap]),
        node_capacity(nodesCap),
        ranges(new CharRange[rangesCap]),
        range_capacity(rangesCap),
        char_classes(new CharClassEntry[classesCap]),
        char_class_capacity(classesCap),
        literal_buf(patLen > 0 ? new char[patLen]{} : nullptr),
        literal_buf_size(patLen) {}

  constexpr ~AstBuilder() {
    delete[] nodes;
    delete[] ranges;
    delete[] char_classes;
    delete[] literal_buf;
  }

  AstBuilder(const AstBuilder&) = delete;
  AstBuilder& operator=(const AstBuilder&) = delete;
  AstBuilder(AstBuilder&&) = delete;
  AstBuilder& operator=(AstBuilder&&) = delete;

  constexpr std::string_view appendLiteral(const char* data, int len) noexcept {
    auto* str = new char[len];
    for (int i = 0; i < len; ++i) {
      str[i] = data[i];
    }
    LiteralEntry entry{str, len};
    literal_store.append(&entry, 1);
    return std::string_view(str, static_cast<std::size_t>(len));
  }

  constexpr std::string_view adoptLiteral(char* data, int len) noexcept {
    LiteralEntry entry{data, len};
    literal_store.append(&entry, 1);
    return std::string_view(data, static_cast<std::size_t>(len));
  }

  constexpr std::string_view appendLiteralChar(char c) noexcept {
    return appendLiteral(&c, 1);
  }

  constexpr NodeIdx addNode(AstNode node) noexcept {
    if (node_count >= node_capacity) {
      grow(nodes, node_capacity);
    }
    nodes[node_count] = node;
    return static_cast<NodeIdx>(node_count++);
  }

  constexpr void markDead(NodeIdx idx) noexcept {
    nodes[idx].kind = NodeKind::Dead;
  }

  constexpr int addCharClass(const CharRangeSet& rs) noexcept {
    if (char_class_count >= char_class_capacity) {
      grow(char_classes, char_class_capacity);
    }
    auto& cc = char_classes[char_class_count];
    cc.range_offset = total_ranges;
    int count = 0;
    rs.forEach([&](CharRange r) {
      if (total_ranges >= range_capacity) {
        grow(ranges, range_capacity);
      }
      ranges[total_ranges++] = r;
      ++count;
    });
    cc.range_count = count;
    return char_class_count++;
  }

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    return charClassTest(ranges + cc.range_offset, cc.range_count, c);
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

 private:
  template <typename T>
  constexpr void grow(T*& arr, int& cap) noexcept {
    int newCap = cap * 2;
    auto* newArr = new T[newCap]{};
    for (int i = 0; i < cap; ++i) {
      newArr[i] = arr[i];
    }
    delete[] arr;
    arr = newArr;
    cap = newCap;
  }
};

struct ParseSizes {
  std::size_t max_nodes;
  std::size_t max_ranges;
  std::size_t max_classes;
  std::size_t literal_buf_size;
  std::size_t literal_chars_size;
};

template <ParseSizes Sizes>
struct ParseResult {
  AstNode nodes[Sizes.max_nodes] = {};
  CharRange ranges[Sizes.max_ranges] = {};
  CharClassEntry char_classes[Sizes.max_classes] = {};
  int node_count = 0;
  int group_count = 0;
  int total_ranges = 0;
  int char_class_count = 0;
  NodeIdx root = kNoNode;
  bool valid = false;
  bool nfa_compatible = true;
  bool has_lookbehind = false;
  bool backtrack_safe = false;

  // Unified literal buffer: prefix at front, suffix at back.
  char literal_buf[Sizes.literal_buf_size > 0 ? Sizes.literal_buf_size : 1] =
      {};
  int literal_buf_size = static_cast<int>(Sizes.literal_buf_size);
  int prefix_len = 0;
  int suffix_len = 0;

  // Literal character storage for AstNode::literal string_views.
  char literal_chars
      [Sizes.literal_chars_size > 0 ? Sizes.literal_chars_size : 1] = {};
  int literal_chars_count = 0;

  std::size_t error_pos = 0;
  char error_message[128] = {};

  constexpr ParseResult() noexcept = default;

  ParseResult(const ParseResult&) = delete;
  ParseResult& operator=(const ParseResult&) = delete;
  ParseResult& operator=(ParseResult&&) = delete;

  constexpr ParseResult(ParseResult&& src) noexcept
      : node_count(src.node_count),
        group_count(src.group_count),
        total_ranges(src.total_ranges),
        char_class_count(src.char_class_count),
        root(src.root),
        valid(src.valid),
        nfa_compatible(src.nfa_compatible),
        has_lookbehind(src.has_lookbehind),
        backtrack_safe(src.backtrack_safe),
        literal_buf_size(src.literal_buf_size),
        prefix_len(src.prefix_len),
        suffix_len(src.suffix_len),
        literal_chars_count(src.literal_chars_count),
        error_pos(src.error_pos) {
    // Copy literal_chars buffer
    for (int i = 0; i < src.literal_chars_count; ++i) {
      literal_chars[i] = src.literal_chars[i];
    }
    // Copy literal_buf
    for (int i = 0; i < src.literal_buf_size; ++i) {
      literal_buf[i] = src.literal_buf[i];
    }
    // Copy nodes, rewriting string_views to point into this->literal_chars
    for (int i = 0; i < src.node_count; ++i) {
      nodes[i] = src.nodes[i];
      if (nodes[i].kind == NodeKind::Literal && !nodes[i].literal.empty()) {
        auto offset = nodes[i].literal.data() - src.literal_chars;
        nodes[i].literal =
            std::string_view(literal_chars + offset, nodes[i].literal.size());
      }
    }
    // Copy ranges and char_classes
    for (int i = 0; i < src.total_ranges; ++i) {
      ranges[i] = src.ranges[i];
    }
    for (int i = 0; i < src.char_class_count; ++i) {
      char_classes[i] = src.char_classes[i];
    }
    // Copy error_message
    for (int i = 0; i < 128; ++i) {
      error_message[i] = src.error_message[i];
    }
  }

  constexpr std::string_view appendLiteral(const char* data, int len) noexcept {
    int offset = literal_chars_count;
    for (int i = 0; i < len; ++i) {
      literal_chars[literal_chars_count++] = data[i];
    }
    return std::string_view(
        literal_chars + offset, static_cast<std::size_t>(len));
  }

  constexpr std::string_view appendLiteralChar(char c) noexcept {
    return appendLiteral(&c, 1);
  }

  constexpr int addNode(AstNode node) noexcept {
    int idx = node_count++;
    nodes[idx] = node;
    return idx;
  }

  constexpr int addCharClass(const CharRangeSet& rs) noexcept {
    int ccIdx = char_class_count++;
    auto& cc = char_classes[ccIdx];
    cc.range_offset = total_ranges;
    int count = 0;
    rs.forEach([&](CharRange r) {
      ranges[total_ranges++] = r;
      ++count;
    });
    cc.range_count = count;
    return ccIdx;
  }

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    return charClassTest(ranges + cc.range_offset, cc.range_count, c);
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

constexpr ParseSizes countLiveEntries(const AstBuilder& builder) noexcept {
  ParseSizes sizes{};
  for (int i = 0; i < builder.node_count; ++i) {
    if (builder.nodes[i].kind != NodeKind::Dead) {
      ++sizes.max_nodes;
      if (builder.nodes[i].kind == NodeKind::Literal) {
        sizes.literal_chars_size += builder.nodes[i].literal.size();
      }
    }
  }
  sizes.max_ranges = builder.total_ranges;
  sizes.max_classes = builder.char_class_count;
  sizes.literal_buf_size = builder.literal_buf_size;
  return sizes;
}

// Fixup pass: compact an AstBuilder (with possible Dead nodes) into a
// precisely-sized static ParseResult. Builds an old→new index remapping
// table, copies live nodes with remapped child_begin/child_end, copies
// ranges and char_classes, and linearizes literal_chars with string_view
// rewriting.
template <ParseSizes Sizes>
constexpr ParseResult<Sizes> compact(const AstBuilder& builder) noexcept {
  ParseResult<Sizes> result;

  result.group_count = builder.group_count;
  result.valid = builder.valid;
  result.nfa_compatible = builder.nfa_compatible;
  result.has_lookbehind = builder.has_lookbehind;
  result.backtrack_safe = builder.backtrack_safe;

  // Copy prefix from front of literal_buf
  result.prefix_len = builder.prefix_len;
  for (int i = 0; i < builder.prefix_len; ++i) {
    result.literal_buf[i] = builder.literal_buf[i];
  }
  // Copy suffix from back of literal_buf
  result.suffix_len = builder.suffix_len;
  for (int i = 0; i < builder.suffix_len; ++i) {
    int srcIdx = builder.literal_buf_size - builder.suffix_len + i;
    int dstIdx =
        static_cast<int>(Sizes.literal_buf_size) - builder.suffix_len + i;
    result.literal_buf[dstIdx] = builder.literal_buf[srcIdx];
  }

  result.error_pos = builder.error_pos;
  for (int i = 0; i < 128 && builder.error_message[i] != '\0'; ++i) {
    result.error_message[i] = builder.error_message[i];
  }

  // Build old→new node index remapping (skip Dead nodes)
  int* remap = new int[builder.node_count > 0 ? builder.node_count : 1]{};
  int newCount = 0;
  for (int i = 0; i < builder.node_count; ++i) {
    if (builder.nodes[i].kind != NodeKind::Dead) {
      remap[i] = newCount++;
    } else {
      remap[i] = kNoNode;
    }
  }

  auto remapIdx = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    return static_cast<NodeIdx>(remap[idx]);
  };

  // Copy live nodes with remapped indices
  int literalOffset = 0;
  for (int i = 0; i < builder.node_count; ++i) {
    if (builder.nodes[i].kind == NodeKind::Dead) {
      continue;
    }
    auto node = builder.nodes[i];
    node.child_begin = remapIdx(node.child_begin);
    node.child_end = remapIdx(node.child_end);

    // Copy literal chars into result and rewrite string_view
    if (node.kind == NodeKind::Literal && !node.literal.empty()) {
      int len = static_cast<int>(node.literal.size());
      for (int c = 0; c < len; ++c) {
        result.literal_chars[literalOffset + c] = node.literal[c];
      }
      node.literal = std::string_view(
          result.literal_chars + literalOffset, node.literal.size());
      literalOffset += len;
    }

    result.nodes[result.node_count++] = node;
  }
  result.literal_chars_count = literalOffset;

  result.root = remapIdx(builder.root);

  // Copy ranges and char_classes directly (no dead entries)
  for (int i = 0; i < builder.total_ranges; ++i) {
    result.ranges[i] = builder.ranges[i];
  }
  result.total_ranges = builder.total_ranges;

  for (int i = 0; i < builder.char_class_count; ++i) {
    result.char_classes[i] = builder.char_classes[i];
  }
  result.char_class_count = builder.char_class_count;

  delete[] remap;
  return result;
}

constexpr int computeFixedWidth(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Empty:
      return 0;
    case NodeKind::Literal:
      return static_cast<int>(n.literal.size());
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
      return 1;
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return 0;
    case NodeKind::Group:
      return computeFixedWidth(ast, n.child_begin);
    case NodeKind::Sequence: {
      int total = 0;
      int child = n.child_begin;
      while (child >= 0) {
        int w = computeFixedWidth(ast, child);
        if (w < 0) {
          return -1;
        }
        total += w;
        child = ast.nodes[child].child_end;
      }
      return total;
    }
    case NodeKind::Alternation: {
      int child = n.child_begin;
      if (child < 0) {
        return 0;
      }
      int width = computeFixedWidth(ast, child);
      if (width < 0) {
        return -1;
      }
      int next = ast.nodes[child].child_end;
      while (next >= 0) {
        int w = computeFixedWidth(ast, next);
        if (w < 0 || w != width) {
          return -1;
        }
        next = ast.nodes[next].child_end;
      }
      return width;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat != n.max_repeat || n.max_repeat < 0) {
        return -1;
      }
      int innerWidth = computeFixedWidth(ast, n.child_begin);
      if (innerWidth < 0) {
        return -1;
      }
      return innerWidth * n.min_repeat;
    }
    case NodeKind::Backref:
      return -1;
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return 0;
  }
  return -1;
}

constexpr int computeFixedWidthPrefix(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Empty:
      return 0;
    case NodeKind::Literal:
      return static_cast<int>(n.literal.size());
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
      return 1;
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Dead:
      return 0;
    case NodeKind::Group:
      return computeFixedWidthPrefix(ast, n.child_begin);
    case NodeKind::Sequence: {
      int total = 0;
      int child = n.child_begin;
      while (child >= 0) {
        int fw = computeFixedWidth(ast, child);
        if (fw >= 0) {
          total += fw;
          child = ast.nodes[child].child_end;
        } else {
          total += computeFixedWidthPrefix(ast, child);
          break;
        }
      }
      return total;
    }
    case NodeKind::Alternation: {
      int child = n.child_begin;
      if (child < 0) {
        return 0;
      }
      int minPrefix = computeFixedWidthPrefix(ast, child);
      int next = ast.nodes[child].child_end;
      while (next >= 0) {
        int p = computeFixedWidthPrefix(ast, next);
        if (p < minPrefix) {
          minPrefix = p;
        }
        next = ast.nodes[next].child_end;
      }
      return minPrefix;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_begin);
        if (innerFW >= 0) {
          return innerFW * n.min_repeat;
        }
        return computeFixedWidthPrefix(ast, n.child_begin);
      }
      return 0;
    }
    case NodeKind::Backref:
      return 0;
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return 0;
  }
  return 0;
}

struct CharAtOffset {
  int nodeIdx = -1;
  int charOffset = 0;
  bool valid = false;
};

constexpr CharAtOffset resolveCharAtOffset(
    const auto& ast, int nodeIdx, int offset) noexcept {
  if (nodeIdx < 0 || offset < 0) {
    return {};
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Literal:
      if (offset < static_cast<int>(n.literal.size())) {
        return {nodeIdx, offset, true};
      }
      return {};
    case NodeKind::CharClass:
      if (offset == 0) {
        return {nodeIdx, 0, true};
      }
      return {};
    case NodeKind::Group:
      return resolveCharAtOffset(ast, n.child_begin, offset);
    case NodeKind::Sequence: {
      int child = n.child_begin;
      int accumulated = 0;
      while (child >= 0) {
        int fw = computeFixedWidth(ast, child);
        if (fw < 0) {
          int prefix = computeFixedWidthPrefix(ast, child);
          if (offset < accumulated + prefix) {
            return resolveCharAtOffset(ast, child, offset - accumulated);
          }
          return {};
        }
        if (offset < accumulated + fw) {
          return resolveCharAtOffset(ast, child, offset - accumulated);
        }
        accumulated += fw;
        child = ast.nodes[child].child_end;
      }
      return {};
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_begin);
        if (innerFW > 0) {
          int fixedPart = innerFW * n.min_repeat;
          if (offset < fixedPart) {
            return resolveCharAtOffset(ast, n.child_begin, offset % innerFW);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_begin);
          if (offset < innerPrefix) {
            return resolveCharAtOffset(ast, n.child_begin, offset);
          }
        }
      }
      return {};
    }
    case NodeKind::Empty:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::Alternation:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Backref:
    case NodeKind::Dead:
      return {};
  }
  return {};
}

struct LeafChars {
  CharAtOffset entries[64] = {};
  int count = 0;
  bool overflow = false;
};

constexpr void collectLeafCharsAtOffset(
    const auto& ast, int nodeIdx, int offset, LeafChars& out) noexcept {
  if (nodeIdx < 0 || offset < 0 || out.overflow) {
    return;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Literal:
      if (offset < static_cast<int>(n.literal.size())) {
        if (out.count >= 64) {
          out.overflow = true;
          return;
        }
        out.entries[out.count++] = {nodeIdx, offset, true};
      }
      break;
    case NodeKind::CharClass:
      if (offset == 0) {
        if (out.count >= 64) {
          out.overflow = true;
          return;
        }
        out.entries[out.count++] = {nodeIdx, 0, true};
      }
      break;
    case NodeKind::Group:
      collectLeafCharsAtOffset(ast, n.child_begin, offset, out);
      break;
    case NodeKind::Alternation: {
      int child = n.child_begin;
      while (child >= 0 && !out.overflow) {
        collectLeafCharsAtOffset(ast, child, offset, out);
        child = ast.nodes[child].child_end;
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
            collectLeafCharsAtOffset(ast, child, offset - accumulated, out);
          }
          return;
        }
        if (offset < accumulated + fw) {
          collectLeafCharsAtOffset(ast, child, offset - accumulated, out);
          return;
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
          int fixedPart = innerFW * n.min_repeat;
          if (offset < fixedPart) {
            collectLeafCharsAtOffset(ast, n.child_begin, offset % innerFW, out);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_begin);
          if (offset < innerPrefix) {
            collectLeafCharsAtOffset(ast, n.child_begin, offset, out);
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
}

constexpr bool areCharSetsDisjoint(
    const auto& ast, const CharAtOffset& a, const CharAtOffset& b) noexcept {
  const auto& na = ast.nodes[a.nodeIdx];
  const auto& nb = ast.nodes[b.nodeIdx];
  if (na.kind == NodeKind::Literal && nb.kind == NodeKind::Literal) {
    return na.literal[a.charOffset] != nb.literal[b.charOffset];
  }
  if (na.kind == NodeKind::Literal && nb.kind == NodeKind::CharClass) {
    return !ast.charClassTestAt(nb.char_class_index, na.literal[a.charOffset]);
  }
  if (na.kind == NodeKind::CharClass && nb.kind == NodeKind::Literal) {
    return !ast.charClassTestAt(na.char_class_index, nb.literal[b.charOffset]);
  }
  if (na.kind == NodeKind::CharClass && nb.kind == NodeKind::CharClass) {
    const auto& cca = ast.char_classes[na.char_class_index];
    const auto& ccb = ast.char_classes[nb.char_class_index];
    for (int i = 0; i < cca.range_count; ++i) {
      for (int j = 0; j < ccb.range_count; ++j) {
        auto ra = ast.ranges[cca.range_offset + i];
        auto rb = ast.ranges[ccb.range_offset + j];
        if (ra.lo <= rb.hi && rb.lo <= ra.hi) {
          return false;
        }
      }
    }
    return true;
  }
  return false;
}

/// Returns true if the subtree rooted at nodeIdx can possibly match
/// zero characters. Recurses into children for composite nodes.
constexpr bool isPossiblyZeroWidth(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }
  const auto& node = ast.nodes[nodeIdx];

  if (node.isZeroWidth()) {
    return true;
  }

  switch (node.kind) {
    case NodeKind::Repeat:
      return node.min_repeat == 0 || isPossiblyZeroWidth(ast, node.child_begin);

    case NodeKind::Group:
      return isPossiblyZeroWidth(ast, node.child_begin);

    case NodeKind::Alternation: {
      int child = node.child_begin;
      while (child >= 0) {
        if (isPossiblyZeroWidth(ast, child)) {
          return true;
        }
        child = ast.nodes[child].child_end;
      }
      return false;
    }

    case NodeKind::Sequence: {
      int child = node.child_begin;
      while (child >= 0) {
        if (!isPossiblyZeroWidth(ast, child)) {
          return false;
        }
        child = ast.nodes[child].child_end;
      }
      return true;
    }

    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Dead:
      return true;

    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Backref:
      return false;
  }
}

} // namespace detail
} // namespace regex
} // namespace folly
