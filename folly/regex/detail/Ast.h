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

#include <folly/portability/Constexpr.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/ChunkedBuffer.h>
#include <folly/regex/detail/DynamicBitset.h>

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

struct PrefixGroupAdjustment {
  int group_id = 0;
  int chars_stripped = 0;
  int prefix_offset = 0;
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

enum class RepeatMode : uint8_t {
  Lazy, // x??, x*?, x+?, x{n,m}?
  Greedy, // x?, x*, x+, x{n,m}     (default)
  Possessive, // x?+, x*+, x++, x{n,m}+ OR optimizer-inferred
  PossessiveProbed, // Possessive that entered reverseAst — needs forward probe
};

struct AstNode {
  NodeKind kind = NodeKind::Empty;
  std::string_view literal;
  bool capturing = false;
  RepeatMode repeat_mode = RepeatMode::Greedy;
  AnchorKind anchor = AnchorKind::Begin;
  int group_id = 0;
  int min_repeat = 0;
  int max_repeat = -1; // -1 = unlimited
  NodeIdx child_first = kNoNode;
  NodeIdx child_last = kNoNode;
  NodeIdx next_sibling = kNoNode;
  NodeIdx prev_sibling = kNoNode;
  int char_class_index = -1;
  int probe_id = -1;
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

  constexpr bool isPossessive() const noexcept {
    return repeat_mode == RepeatMode::Possessive ||
        repeat_mode == RepeatMode::PossessiveProbed;
  }

  constexpr bool isPossessiveProbed() const noexcept {
    return repeat_mode == RepeatMode::PossessiveProbed;
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
  bool has_backref = false;
  bool backtrack_safe = false;
  int min_width = 0;
  int probe_count = 0;

  // Unified literal buffer: prefix occupies [0..prefix_len-1],
  // suffix occupies [literal_buf_size-suffix_len..literal_buf_size-1].
  char* literal_buf = nullptr;
  int literal_buf_size = 0;
  int prefix_len = 0;
  int prefix_strip_len = 0;
  int suffix_len = 0;
  int suffix_strip_len = 0;

  constexpr std::string_view literal_prefix() const noexcept {
    return std::string_view(literal_buf, prefix_len);
  }

  constexpr std::string_view literal_suffix() const noexcept {
    return std::string_view(
        literal_buf + literal_buf_size - suffix_len, suffix_len);
  }

  PrefixGroupAdjustment* prefix_group_adjustments = nullptr;
  int prefix_group_adjustment_count = 0;

  constexpr void ensurePrefixGroupAdjustments() noexcept {
    if (prefix_group_adjustments == nullptr && group_count > 0) {
      prefix_group_adjustments = new PrefixGroupAdjustment[group_count]{};
    }
  }

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
    delete[] prefix_group_adjustments;
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

// --------------------------------------------------------------------------
// Child-list linking helpers. These maintain both next_sibling (next sibling)
// and prev_sibling (previous sibling) to keep the doubly-linked invariant.
// --------------------------------------------------------------------------

// Link an array of node indices into a sibling list, setting both
// next_sibling (forward) and prev_sibling (backward) pointers.
// Pattern A: the most common linking operation (~16 sites).
constexpr void linkSiblingList(auto& ast, const int* arr, int count) noexcept {
  if (count <= 0) {
    return;
  }
  ast.nodes[arr[0]].prev_sibling = kNoNode;
  for (int i = 0; i < count - 1; ++i) {
    ast.nodes[arr[i]].next_sibling = arr[i + 1];
    ast.nodes[arr[i + 1]].prev_sibling = arr[i];
  }
  ast.nodes[arr[count - 1]].next_sibling = kNoNode;
}

// Append a new sibling after prevNode, maintaining both links.
// If prevNode is kNoNode, newNode becomes a standalone first child.
constexpr void appendSibling(
    auto& ast, NodeIdx prevNode, NodeIdx newNode) noexcept {
  if (prevNode >= 0) {
    ast.nodes[prevNode].next_sibling = newNode;
  }
  ast.nodes[newNode].prev_sibling = prevNode;
}

// Replace oldChild with newChild in a sibling list, splicing the
// replacement into the same position. prevChild is the sibling before
// oldChild (kNoNode if oldChild is first). parentIdx is the Seq/Alt parent.
constexpr void replaceSibling(
    auto& ast,
    NodeIdx parentIdx,
    NodeIdx oldChild,
    NodeIdx newChild,
    NodeIdx prevChild) noexcept {
  NodeIdx nextSib = ast.nodes[oldChild].next_sibling;
  ast.nodes[newChild].next_sibling = nextSib;
  ast.nodes[newChild].prev_sibling = prevChild;
  if (nextSib >= 0) {
    ast.nodes[nextSib].prev_sibling = newChild;
  }
  if (prevChild >= 0) {
    ast.nodes[prevChild].next_sibling = newChild;
  } else {
    ast.nodes[parentIdx].child_first = newChild;
  }
}

// Walk to the last sibling in a child list.
constexpr NodeIdx getLastSibling(const auto& ast, NodeIdx first) noexcept {
  NodeIdx last = first;
  while (last >= 0 && ast.nodes[last].next_sibling >= 0) {
    last = ast.nodes[last].next_sibling;
  }
  return last;
}

struct ParseSizes {
  std::size_t max_nodes;
  std::size_t max_ranges;
  std::size_t max_classes;
  std::size_t literal_buf_size;
  std::size_t literal_chars_size;
  std::size_t prefix_group_adjustments;
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
  bool has_backref = false;
  bool backtrack_safe = false;
  int min_width = 0;
  int probe_count = 0;

  // Unified literal buffer: prefix at front, suffix at back.
  char literal_buf[Sizes.literal_buf_size > 0 ? Sizes.literal_buf_size : 1] =
      {};
  int literal_buf_size = static_cast<int>(Sizes.literal_buf_size);
  int prefix_len = 0;
  int prefix_strip_len = 0;
  int suffix_len = 0;
  int suffix_strip_len = 0;

  constexpr std::string_view literal_prefix() const noexcept {
    return std::string_view(literal_buf, prefix_len);
  }

  constexpr std::string_view literal_suffix() const noexcept {
    return std::string_view(
        literal_buf + literal_buf_size - suffix_len, suffix_len);
  }

  PrefixGroupAdjustment prefix_group_adjustments
      [Sizes.prefix_group_adjustments > 0 ? Sizes.prefix_group_adjustments
                                          : 1] = {};
  int prefix_group_adjustment_count = 0;

  constexpr void ensurePrefixGroupAdjustments() noexcept {}

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
        has_backref(src.has_backref),
        backtrack_safe(src.backtrack_safe),
        min_width(src.min_width),
        probe_count(src.probe_count),
        literal_buf_size(src.literal_buf_size),
        prefix_len(src.prefix_len),
        prefix_strip_len(src.prefix_strip_len),
        suffix_len(src.suffix_len),
        suffix_strip_len(src.suffix_strip_len),
        prefix_group_adjustment_count(src.prefix_group_adjustment_count),
        literal_chars_count(src.literal_chars_count),
        error_pos(src.error_pos) {
    // Copy literal_chars buffer
    for (int i = 0; i < src.literal_chars_count; ++i) {
      literal_chars[i] = src.literal_chars[i];
    }
    // Copy prefix group adjustments
    folly::constexpr_memcpy(
        prefix_group_adjustments,
        src.prefix_group_adjustments,
        src.prefix_group_adjustment_count);
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
    folly::constexpr_memcpy(
        error_message, src.error_message, sizeof(error_message));
  }

  constexpr std::string_view appendLiteral(const char* data, int len) noexcept {
    int offset = literal_chars_count;
    folly::constexpr_memcpy(literal_chars + offset, data, len);
    literal_chars_count += len;
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

// Recursively mark all nodes reachable from nodeIdx in the visited bitset.
constexpr void markReachable(
    const AstBuilder& builder, int nodeIdx, DynamicBitset& visited) noexcept {
  if (nodeIdx < 0 || nodeIdx >= builder.node_count || visited.test(nodeIdx)) {
    return;
  }
  visited.set(nodeIdx);
  const auto& node = builder.nodes[nodeIdx];
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        markReachable(builder, child, visited);
        child = builder.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      markReachable(builder, node.child_first, visited);
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

// Walk from the root to find all reachable nodes and mark any
// unreachable nodes as Dead. Must be called before countLiveEntries()
// so that the resulting sizes are precise.
constexpr void markUnreachableNodes(AstBuilder& builder) noexcept {
  if (builder.root < 0 || builder.node_count == 0) {
    return;
  }
  DynamicBitset visited;
  visited.reserve(builder.node_count);
  markReachable(builder, builder.root, visited);
  for (int i = 0; i < builder.node_count; ++i) {
    if (!visited.test(i)) {
      builder.nodes[i].kind = NodeKind::Dead;
    }
  }
}

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
  sizes.prefix_group_adjustments = builder.prefix_group_adjustment_count;
  return sizes;
}

// Fixup pass: compact an AstBuilder (with possible Dead nodes) into a
// precisely-sized static ParseResult. Builds an old→new index remapping
// table, copies live nodes with remapped child_first/next_sibling, copies
// ranges and char_classes, and linearizes literal_chars with string_view
// rewriting.
template <ParseSizes Sizes>
constexpr ParseResult<Sizes> compact(const AstBuilder& builder) noexcept {
  ParseResult<Sizes> result;

  result.group_count = builder.group_count;
  result.valid = builder.valid;
  result.nfa_compatible = builder.nfa_compatible;
  result.has_lookbehind = builder.has_lookbehind;
  result.has_backref = builder.has_backref;
  result.backtrack_safe = builder.backtrack_safe;
  result.min_width = builder.min_width;
  result.probe_count = builder.probe_count;

  // Copy prefix from front of literal_buf
  result.prefix_len = builder.prefix_len;
  result.prefix_strip_len = builder.prefix_strip_len;
  folly::constexpr_memcpy(
      result.literal_buf, builder.literal_buf, builder.prefix_len);
  // Copy suffix from back of literal_buf
  result.suffix_len = builder.suffix_len;
  result.suffix_strip_len = builder.suffix_strip_len;
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
    node.child_first = remapIdx(node.child_first);
    node.child_last = remapIdx(node.child_last);
    node.next_sibling = remapIdx(node.next_sibling);
    node.prev_sibling = remapIdx(node.prev_sibling);

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

  // Copy prefix group adjustments
  result.prefix_group_adjustment_count = builder.prefix_group_adjustment_count;
  folly::constexpr_memcpy(
      result.prefix_group_adjustments,
      builder.prefix_group_adjustments,
      builder.prefix_group_adjustment_count);

  delete[] remap;
  return result;
}

// --------------------------------------------------------------------------
// ProbeStore: compact storage for probe subtrees (possessive probes,
// future lookaround probes). Holds only the AST nodes, char classes,
// ranges, and literal chars needed by probe patterns.
// --------------------------------------------------------------------------

enum class ProbeDirection : uint8_t { Forward, Reverse };

struct ProbeStoreSizes {
  std::size_t max_nodes;
  std::size_t max_ranges;
  std::size_t max_classes;
  std::size_t literal_chars_size;
  std::size_t max_probes; // max_probe_id + 1, for direct indexing
};

template <ProbeStoreSizes Sizes>
struct ProbeStore {
  struct ProbeEntry {
    int root = -1; // root node index within this store's nodes[], -1 = dead
    ProbeDirection dir{}; // forward or reverse
    bool negated = false; // for negative lookaround
    int lookbehind_width = -1; // >= 0 enables fixed-width fast path
  };

  AstNode nodes[Sizes.max_nodes > 0 ? Sizes.max_nodes : 1] = {};
  CharRange ranges[Sizes.max_ranges > 0 ? Sizes.max_ranges : 1] = {};
  CharClassEntry char_classes[Sizes.max_classes > 0 ? Sizes.max_classes : 1] =
      {};
  char literal_chars
      [Sizes.literal_chars_size > 0 ? Sizes.literal_chars_size : 1] = {};
  int node_count = 0;
  int total_ranges = 0;
  int char_class_count = 0;
  int literal_chars_count = 0;

  ProbeEntry probes[Sizes.max_probes > 0 ? Sizes.max_probes : 1] = {};
  int probe_count = 0; // max_probe_id + 1

  // Needed so BacktrackExecutor can reference Ast.root in
  // findFirstConsumingNode checks. Always kNoNode for ProbeStore
  // since probes are never root patterns.
  static constexpr NodeIdx root = kNoNode;

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    return charClassTest(ranges + cc.range_offset, cc.range_count, c);
  }

  constexpr bool hasProbe(int probeId) const noexcept {
    return probeId >= 0 && probeId < probe_count && probes[probeId].root >= 0;
  }

  constexpr ProbeStore() noexcept = default;

  ProbeStore(const ProbeStore&) = delete;
  ProbeStore& operator=(const ProbeStore&) = delete;
  ProbeStore& operator=(ProbeStore&&) = delete;

  constexpr ProbeStore(ProbeStore&& src) noexcept
      : node_count(src.node_count),
        total_ranges(src.total_ranges),
        char_class_count(src.char_class_count),
        literal_chars_count(src.literal_chars_count),
        probe_count(src.probe_count) {
    // Copy literal_chars buffer
    folly::constexpr_memcpy(
        literal_chars, src.literal_chars, src.literal_chars_count);
    // Copy nodes, rewriting string_views to point into this->literal_chars
    for (int i = 0; i < src.node_count; ++i) {
      nodes[i] = src.nodes[i];
      if (nodes[i].kind == NodeKind::Literal && !nodes[i].literal.empty()) {
        auto offset = nodes[i].literal.data() - src.literal_chars;
        nodes[i].literal =
            std::string_view(literal_chars + offset, nodes[i].literal.size());
      }
    }
    // Copy ranges, char_classes, probes
    folly::constexpr_memcpy(ranges, src.ranges, src.total_ranges);
    folly::constexpr_memcpy(
        char_classes, src.char_classes, src.char_class_count);
    folly::constexpr_memcpy(probes, src.probes, src.probe_count);
  }
};

// Recursively mark all nodes reachable from nodeIdx in the visited bitset.
// Works on AstBuilder nodes.
constexpr void markReachableFrom(
    const AstBuilder& builder, int nodeIdx, DynamicBitset& visited) noexcept {
  if (nodeIdx < 0 || nodeIdx >= builder.node_count || visited.test(nodeIdx)) {
    return;
  }
  visited.set(nodeIdx);
  const auto& node = builder.nodes[nodeIdx];
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        markReachableFrom(builder, child, visited);
        child = builder.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      markReachableFrom(builder, node.child_first, visited);
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

// Count the probe subtrees in a forward-optimized AstBuilder, filtering
// by a surviving probe_id bitset. Returns ProbeStoreSizes.
template <int MaxProbeIds>
constexpr ProbeStoreSizes extractAndCountProbes(
    const AstBuilder& builder,
    const FixedBitset<MaxProbeIds>& surviving) noexcept {
  ProbeStoreSizes sizes{};

  // Find the max probe_id to determine probes[] array size
  int maxProbeId = -1;
  for (int i = 0; i < builder.node_count; ++i) {
    int pid = builder.nodes[i].probe_id;
    if (pid >= 0 && pid < MaxProbeIds && surviving.test(pid)) {
      if (pid > maxProbeId) {
        maxProbeId = pid;
      }
    }
  }
  sizes.max_probes =
      maxProbeId >= 0 ? static_cast<std::size_t>(maxProbeId + 1) : 1;

  // Collect reachable nodes from each surviving probe's child_first
  DynamicBitset reachable;
  reachable.reserve(builder.node_count);

  // Also collect referenced char class indices
  DynamicBitset ccUsed;
  ccUsed.reserve(builder.char_class_count > 0 ? builder.char_class_count : 1);

  for (int i = 0; i < builder.node_count; ++i) {
    int pid = builder.nodes[i].probe_id;
    if (pid < 0 || pid >= MaxProbeIds || !surviving.test(pid)) {
      continue;
    }
    // Mark the subtree rooted at child_first as reachable
    markReachableFrom(builder, builder.nodes[i].child_first, reachable);
  }

  // Count reachable nodes and literal chars; collect char class refs
  for (int i = 0; i < builder.node_count; ++i) {
    if (!reachable.test(i)) {
      continue;
    }
    ++sizes.max_nodes;
    const auto& node = builder.nodes[i];
    if (node.kind == NodeKind::Literal && !node.literal.empty()) {
      sizes.literal_chars_size += node.literal.size();
    }
    if (node.kind == NodeKind::CharClass && node.char_class_index >= 0) {
      ccUsed.set(node.char_class_index);
    }
  }

  // Count referenced char classes and their ranges
  for (int i = 0; i < builder.char_class_count; ++i) {
    if (ccUsed.test(i)) {
      ++sizes.max_classes;
      sizes.max_ranges += builder.char_classes[i].range_count;
    }
  }

  return sizes;
}

// Compact surviving probe subtrees from a forward-optimized AstBuilder
// into a precisely-sized ProbeStore.
template <ProbeStoreSizes Sizes, int MaxProbeIds>
constexpr ProbeStore<Sizes> compactProbeStore(
    const AstBuilder& builder,
    const FixedBitset<MaxProbeIds>& surviving) noexcept {
  ProbeStore<Sizes> store;

  // Find max probe_id for probe_count
  int maxProbeId = -1;
  for (int i = 0; i < builder.node_count; ++i) {
    int pid = builder.nodes[i].probe_id;
    if (pid >= 0 && pid < MaxProbeIds && surviving.test(pid)) {
      if (pid > maxProbeId) {
        maxProbeId = pid;
      }
    }
  }
  store.probe_count = maxProbeId >= 0 ? maxProbeId + 1 : 0;

  // Collect reachable nodes from all surviving probes
  DynamicBitset reachable;
  reachable.reserve(builder.node_count);

  DynamicBitset ccUsed;
  ccUsed.reserve(builder.char_class_count > 0 ? builder.char_class_count : 1);

  for (int i = 0; i < builder.node_count; ++i) {
    int pid = builder.nodes[i].probe_id;
    if (pid < 0 || pid >= MaxProbeIds || !surviving.test(pid)) {
      continue;
    }
    markReachableFrom(builder, builder.nodes[i].child_first, reachable);
  }

  // Collect referenced char classes
  for (int i = 0; i < builder.node_count; ++i) {
    if (reachable.test(i) && builder.nodes[i].kind == NodeKind::CharClass &&
        builder.nodes[i].char_class_index >= 0) {
      ccUsed.set(builder.nodes[i].char_class_index);
    }
  }

  // Build char class remapping: old_cc_idx -> new_cc_idx
  int* ccRemap =
      new int[builder.char_class_count > 0 ? builder.char_class_count : 1]{};
  for (int i = 0; i < builder.char_class_count; ++i) {
    ccRemap[i] = -1;
  }
  for (int i = 0; i < builder.char_class_count; ++i) {
    if (ccUsed.test(i)) {
      int newIdx = store.char_class_count++;
      ccRemap[i] = newIdx;

      // Copy ranges for this char class
      auto& srcCC = builder.char_classes[i];
      auto& dstCC = store.char_classes[newIdx];
      dstCC.range_offset = store.total_ranges;
      dstCC.range_count = srcCC.range_count;
      for (int r = 0; r < srcCC.range_count; ++r) {
        store.ranges[store.total_ranges++] =
            builder.ranges[srcCC.range_offset + r];
      }
    }
  }

  // Build node remapping: old_node_idx -> new_node_idx
  int* nodeRemap = new int[builder.node_count > 0 ? builder.node_count : 1]{};
  int newNodeCount = 0;
  for (int i = 0; i < builder.node_count; ++i) {
    if (reachable.test(i)) {
      nodeRemap[i] = newNodeCount++;
    } else {
      nodeRemap[i] = kNoNode;
    }
  }

  auto remapIdx = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    return static_cast<NodeIdx>(nodeRemap[idx]);
  };

  // Copy reachable nodes with remapped indices
  for (int i = 0; i < builder.node_count; ++i) {
    if (!reachable.test(i)) {
      continue;
    }
    auto node = builder.nodes[i];
    node.child_first = remapIdx(node.child_first);
    node.child_last = remapIdx(node.child_last);
    node.next_sibling = remapIdx(node.next_sibling);
    node.prev_sibling = remapIdx(node.prev_sibling);

    // Remap char class index
    if (node.kind == NodeKind::CharClass && node.char_class_index >= 0) {
      node.char_class_index = ccRemap[node.char_class_index];
    }

    // Copy literal chars and rewrite string_view
    if (node.kind == NodeKind::Literal && !node.literal.empty()) {
      int len = static_cast<int>(node.literal.size());
      int offset = store.literal_chars_count;
      for (int c = 0; c < len; ++c) {
        store.literal_chars[offset + c] = node.literal[c];
      }
      node.literal =
          std::string_view(store.literal_chars + offset, node.literal.size());
      store.literal_chars_count += len;
    }

    store.nodes[store.node_count++] = node;
  }

  // Record probe entries: for each surviving probe, store its remapped root
  for (int i = 0; i < builder.node_count; ++i) {
    int pid = builder.nodes[i].probe_id;
    if (pid < 0 || pid >= MaxProbeIds || !surviving.test(pid)) {
      continue;
    }
    if (pid < store.probe_count) {
      store.probes[pid].root = remapIdx(builder.nodes[i].child_first);
      // Set direction and negation based on node kind
      auto kind = builder.nodes[i].kind;
      if (kind == NodeKind::Lookahead) {
        store.probes[pid].dir = ProbeDirection::Forward;
        store.probes[pid].negated = false;
      } else if (kind == NodeKind::NegLookahead) {
        store.probes[pid].dir = ProbeDirection::Forward;
        store.probes[pid].negated = true;
      } else if (kind == NodeKind::Lookbehind) {
        store.probes[pid].dir = ProbeDirection::Reverse;
        store.probes[pid].negated = false;
        store.probes[pid].lookbehind_width = builder.nodes[i].min_repeat;
      } else if (kind == NodeKind::NegLookbehind) {
        store.probes[pid].dir = ProbeDirection::Reverse;
        store.probes[pid].negated = true;
        store.probes[pid].lookbehind_width = builder.nodes[i].min_repeat;
      }
      // Possessive repeats keep defaults (Forward, not negated)
    }
  }

  delete[] ccRemap;
  delete[] nodeRemap;
  return store;
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
      return computeFixedWidth(ast, n.child_first);
    case NodeKind::Sequence: {
      int total = 0;
      int child = n.child_first;
      while (child >= 0) {
        int w = computeFixedWidth(ast, child);
        if (w < 0) {
          return -1;
        }
        total += w;
        child = ast.nodes[child].next_sibling;
      }
      return total;
    }
    case NodeKind::Alternation: {
      int child = n.child_first;
      if (child < 0) {
        return 0;
      }
      int width = computeFixedWidth(ast, child);
      if (width < 0) {
        return -1;
      }
      int next = ast.nodes[child].next_sibling;
      while (next >= 0) {
        int w = computeFixedWidth(ast, next);
        if (w < 0 || w != width) {
          return -1;
        }
        next = ast.nodes[next].next_sibling;
      }
      return width;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat != n.max_repeat || n.max_repeat < 0) {
        return -1;
      }
      int innerWidth = computeFixedWidth(ast, n.child_first);
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

// Returns true if a node has exactly one deterministic match outcome for any
// given position — it either matches a fixed amount of input or fails, with
// no alternative match lengths or branches to try on backtracking.
constexpr bool isNonBacktracking(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return true;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Literal:
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Empty:
    case NodeKind::Dead:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      return true;
    case NodeKind::Group:
      return isNonBacktracking(ast, n.child_first);
    case NodeKind::Sequence: {
      int child = n.child_first;
      while (child >= 0) {
        if (!isNonBacktracking(ast, child)) {
          return false;
        }
        child = ast.nodes[child].next_sibling;
      }
      return true;
    }
    case NodeKind::Repeat:
      if (n.isPossessive()) {
        return true;
      }
      // Fixed-count repeat with non-backtracking inner
      if (n.min_repeat == n.max_repeat && n.min_repeat >= 0) {
        return isNonBacktracking(ast, n.child_first);
      }
      return false;
    case NodeKind::Alternation: {
      // An alternation is non-backtracking if all branches have the same
      // fixed width and each branch is individually non-backtracking.
      // Same-width means the alternation always consumes a fixed number
      // of characters — only the CONTENT varies, not the length.
      if (computeFixedWidth(ast, nodeIdx) < 0) {
        return false;
      }
      int child = n.child_first;
      while (child >= 0) {
        if (!isNonBacktracking(ast, child)) {
          return false;
        }
        child = ast.nodes[child].next_sibling;
      }
      return true;
    }
    case NodeKind::Backref:
      return false;
  }
  return false;
}

// Assign probe IDs to all Possessive Repeat and Lookaround nodes via
// depth-first traversal. Called BEFORE reversal and BEFORE optimization
// so that both forward and reverse ASTs get matching IDs in the same order.
constexpr void assignProbeIds(auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = ast.nodes[nodeIdx];
  if (node.kind == NodeKind::Repeat &&
      node.repeat_mode == RepeatMode::Possessive) {
    node.probe_id = ast.probe_count++;
  }
  if (node.kind == NodeKind::Lookahead ||
      node.kind == NodeKind::NegLookahead ||
      node.kind == NodeKind::Lookbehind ||
      node.kind == NodeKind::NegLookbehind) {
    node.probe_id = ast.probe_count++;
  }
  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        assignProbeIds(ast, child);
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
      assignProbeIds(ast, node.child_first);
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

constexpr int computeMinWidth(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& n = ast.nodes[nodeIdx];
  switch (n.kind) {
    case NodeKind::Empty:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Dead:
    case NodeKind::Backref:
      return 0;
    case NodeKind::Literal:
      return static_cast<int>(n.literal.size());
    case NodeKind::AnyChar:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
      return 1;
    case NodeKind::Group:
      return computeMinWidth(ast, n.child_first);
    case NodeKind::Sequence: {
      int total = 0;
      int child = n.child_first;
      while (child >= 0) {
        total += computeMinWidth(ast, child);
        child = ast.nodes[child].next_sibling;
      }
      return total;
    }
    case NodeKind::Alternation: {
      int child = n.child_first;
      if (child < 0) {
        return 0;
      }
      int minW = computeMinWidth(ast, child);
      int next = ast.nodes[child].next_sibling;
      while (next >= 0) {
        int w = computeMinWidth(ast, next);
        if (w < minW) {
          minW = w;
        }
        next = ast.nodes[next].next_sibling;
      }
      return minW;
    }
    case NodeKind::Repeat: {
      int innerWidth = computeMinWidth(ast, n.child_first);
      return innerWidth * n.min_repeat;
    }
  }
  return 0;
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
      return computeFixedWidthPrefix(ast, n.child_first);
    case NodeKind::Sequence: {
      int total = 0;
      int child = n.child_first;
      while (child >= 0) {
        int fw = computeFixedWidth(ast, child);
        if (fw >= 0) {
          total += fw;
          child = ast.nodes[child].next_sibling;
        } else {
          total += computeFixedWidthPrefix(ast, child);
          break;
        }
      }
      return total;
    }
    case NodeKind::Alternation: {
      int child = n.child_first;
      if (child < 0) {
        return 0;
      }
      int minPrefix = computeFixedWidthPrefix(ast, child);
      int next = ast.nodes[child].next_sibling;
      while (next >= 0) {
        int p = computeFixedWidthPrefix(ast, next);
        if (p < minPrefix) {
          minPrefix = p;
        }
        next = ast.nodes[next].next_sibling;
      }
      return minPrefix;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_first);
        if (innerFW >= 0) {
          return innerFW * n.min_repeat;
        }
        return computeFixedWidthPrefix(ast, n.child_first);
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
      return resolveCharAtOffset(ast, n.child_first, offset);
    case NodeKind::Sequence: {
      int child = n.child_first;
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
        child = ast.nodes[child].next_sibling;
      }
      return {};
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_first);
        if (innerFW > 0) {
          int fixedPart = innerFW * n.min_repeat;
          if (offset < fixedPart) {
            return resolveCharAtOffset(ast, n.child_first, offset % innerFW);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_first);
          if (offset < innerPrefix) {
            return resolveCharAtOffset(ast, n.child_first, offset);
          }
        }
      }
      return {};
    }
    case NodeKind::Alternation:
      if (n.discriminator_offset >= 0 && offset == n.discriminator_offset &&
          n.char_class_index >= 0) {
        return {nodeIdx, 0, true};
      }
      return {};
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
      collectLeafCharsAtOffset(ast, n.child_first, offset, out);
      break;
    case NodeKind::Alternation: {
      int child = n.child_first;
      while (child >= 0 && !out.overflow) {
        collectLeafCharsAtOffset(ast, child, offset, out);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Sequence: {
      int child = n.child_first;
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
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Repeat: {
      if (n.min_repeat > 0) {
        int innerFW = computeFixedWidth(ast, n.child_first);
        if (innerFW > 0) {
          int fixedPart = innerFW * n.min_repeat;
          if (offset < fixedPart) {
            collectLeafCharsAtOffset(ast, n.child_first, offset % innerFW, out);
          }
        } else if (innerFW < 0) {
          int innerPrefix = computeFixedWidthPrefix(ast, n.child_first);
          if (offset < innerPrefix) {
            collectLeafCharsAtOffset(ast, n.child_first, offset, out);
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
      return node.min_repeat == 0 || isPossiblyZeroWidth(ast, node.child_first);

    case NodeKind::Group:
      return isPossiblyZeroWidth(ast, node.child_first);

    case NodeKind::Alternation: {
      int child = node.child_first;
      while (child >= 0) {
        if (isPossiblyZeroWidth(ast, child)) {
          return true;
        }
        child = ast.nodes[child].next_sibling;
      }
      return false;
    }

    case NodeKind::Sequence: {
      int child = node.child_first;
      while (child >= 0) {
        if (!isPossiblyZeroWidth(ast, child)) {
          return false;
        }
        child = ast.nodes[child].next_sibling;
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
