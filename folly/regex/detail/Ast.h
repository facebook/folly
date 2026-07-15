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
#include <folly/regex/detail/ChunkedBuffer.h>
#include <folly/regex/detail/ConstexprCopyN.h>
#include <folly/regex/detail/ConstexprSwitch.h>
#include <folly/regex/detail/Direction.h>
#include <folly/regex/detail/DynamicBitset.h>

namespace folly::regex::detail {

using NodeIdx = int;
constexpr NodeIdx kNoNode = -1;

enum class NodeKind : uint8_t {
  Empty,
  Literal,
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
  CaseInsensitiveBackref,
  Dead,
};

template <typename... Cases>
using NodeKindSwitch =
    ConstexprSwitchDispatcher<static_cast<int>(NodeKind::Dead), Cases...>;

enum class AnchorKind : uint8_t {
  Begin,
  End,
  BeginLine,
  EndLine,
  StartOfString,
  EndOfString,
  EndOfStringOrNewline,
};

// A single named-capture-group entry: maps the parsed name to its
// sequentially-assigned group_id. Stored as a fixed-size char array so
// the entry is trivially copyable for ChunkedBuffer linearization and
// for embedding inside the constexpr ParseResult / NamedGroupMap.
struct NamedGroupEntry {
  // 31 chars max + null terminator. Names exceeding the limit produce
  // a parse error.
  static constexpr int kMaxNameLen = 31;
  char name[kMaxNameLen + 1] = {};
  int group_id = 0;

  constexpr std::string_view nameView() const noexcept {
    int len = 0;
    while (len < kMaxNameLen && name[len] != '\0') {
      ++len;
    }
    return {name, static_cast<std::size_t>(len)};
  }
};

struct CharClassEntry {
  int range_offset = 0;
  int range_count = 0;
};

struct PrefixGroupAdjustment {
  int group_id = 0;
  int chars_stripped = 0;
  int prefix_offset = 0;
  constexpr bool operator==(const PrefixGroupAdjustment&) const = default;
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
  // Cached structural width info, populated by precomputeFixedWidths.
  // fixed_width:        -2 = uncomputed, -1 = variable, >=0 = fixed width.
  // fixed_width_prefix: -1 = uncomputed, >=0 = fixed prefix length.
  int fixed_width = -2;
  int fixed_width_prefix = -1;
  int discriminator_offset = -1;
  ChunkedBuffer<DiscriminatorEntry>* valid_discriminators = nullptr;
  int probe_id = -1;

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
      case NodeKind::AnyByte:
      case NodeKind::CharClass:
      case NodeKind::Sequence:
      case NodeKind::Alternation:
      case NodeKind::Repeat:
      case NodeKind::Group:
      case NodeKind::Backref:
      case NodeKind::CaseInsensitiveBackref:
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

  /// Clear both sibling links (isolate this node from any list).
  constexpr void isolate() noexcept {
    next_sibling = kNoNode;
    prev_sibling = kNoNode;
  }

  /// Set this node's only child (child_first = child_last = childIdx).
  constexpr void setOnlyChild(NodeIdx childIdx) noexcept {
    child_first = childIdx;
    child_last = childIdx;
  }

  /// Unlink a child from this node's doubly-linked child list.
  /// Updates child_first/child_last. Does NOT mark the child dead.
  constexpr void unlinkChild(auto& ast, NodeIdx childIdx) noexcept {
    auto prevSib = ast.nodes[childIdx].prev_sibling;
    auto nextSib = ast.nodes[childIdx].next_sibling;
    if (prevSib >= 0) {
      ast.nodes[prevSib].next_sibling = nextSib;
    } else {
      child_first = nextSib;
    }
    if (nextSib >= 0) {
      ast.nodes[nextSib].prev_sibling = prevSib;
    } else {
      child_last = prevSib;
    }
    ast.nodes[childIdx].isolate();
  }

  /// Replace a child in this node's child list with a new node.
  /// Updates both sibling links and child_first/child_last.
  constexpr void replaceChild(
      auto& ast, NodeIdx oldChild, NodeIdx newChild) noexcept {
    NodeIdx prevChild = ast.nodes[oldChild].prev_sibling;
    NodeIdx nextSib = ast.nodes[oldChild].next_sibling;
    ast.nodes[newChild].next_sibling = nextSib;
    ast.nodes[newChild].prev_sibling = prevChild;
    if (nextSib >= 0) {
      ast.nodes[nextSib].prev_sibling = newChild;
    } else {
      child_last = newChild;
    }
    if (prevChild >= 0) {
      ast.nodes[prevChild].next_sibling = newChild;
    } else {
      child_first = newChild;
    }
  }

  /// Append a child to the end of this node's child list.
  /// Updates child_last (and child_first if the list was empty).
  constexpr void appendChild(auto& ast, NodeIdx newChild) noexcept {
    ast.nodes[newChild].next_sibling = kNoNode;
    if (child_last >= 0) {
      ast.nodes[child_last].next_sibling = newChild;
      ast.nodes[newChild].prev_sibling = child_last;
    } else {
      ast.nodes[newChild].prev_sibling = kNoNode;
      child_first = newChild;
    }
    child_last = newChild;
  }

  /// Pretty-print this node's description to the given stream.
  void prettyPrint(auto& os, bool extra) const {
    switch (kind) {
      case NodeKind::Empty:
        os << "Empty";
        break;
      case NodeKind::Literal:
        os << "Literal(\"";
        for (char c : literal) {
          switch (c) {
            case '\\':
              os << "\\\\";
              break;
            case '"':
              os << "\\\"";
              break;
            case '\n':
              os << "\\n";
              break;
            case '\r':
              os << "\\r";
              break;
            case '\t':
              os << "\\t";
              break;
            default:
              os << c;
              break;
          }
        }
        os << "\")";
        break;
      case NodeKind::AnyByte:
        os << "AnyByte";
        break;
      case NodeKind::CharClass:
        os << "CharClass[" << char_class_index << "]";
        break;
      case NodeKind::Sequence:
        os << "Sequence";
        break;
      case NodeKind::Alternation:
        os << "Alternation discriminator_offset=" << discriminator_offset
           << " char_class_index=" << char_class_index
           << " valid_discriminators="
           << (valid_discriminators ? "non-null" : "null");
        break;
      case NodeKind::Repeat:
        os << "Repeat{" << min_repeat << ",";
        if (max_repeat < 0) {
          os << "inf";
        } else {
          os << max_repeat;
        }
        os << "}";
        switch (repeat_mode) {
          case RepeatMode::Lazy:
            os << " lazy";
            break;
          case RepeatMode::Greedy:
            os << " greedy";
            break;
          case RepeatMode::Possessive:
            os << " possessive";
            break;
          case RepeatMode::PossessiveProbed:
            os << " possessive-probed";
            os << " probe_id=" << probe_id;
            break;
        }
        break;
      case NodeKind::Group:
        os << "Group(" << group_id << ")";
        if (capturing) {
          os << " capturing";
        }
        break;
      case NodeKind::Anchor:
        os << "Anchor(";
        switch (anchor) {
          case AnchorKind::Begin:
            os << "Begin";
            break;
          case AnchorKind::End:
            os << "End";
            break;
          case AnchorKind::BeginLine:
            os << "BeginLine";
            break;
          case AnchorKind::EndLine:
            os << "EndLine";
            break;
          case AnchorKind::StartOfString:
            os << "StartOfString";
            break;
          case AnchorKind::EndOfString:
            os << "EndOfString";
            break;
          case AnchorKind::EndOfStringOrNewline:
            os << "EndOfStringOrNewline";
            break;
        }
        os << ")";
        break;
      case NodeKind::Dead:
        os << "Dead";
        break;
      case NodeKind::Lookahead:
        os << "Lookahead probe_id=" << probe_id;
        break;
      case NodeKind::NegLookahead:
        os << "NegLookahead probe_id=" << probe_id;
        break;
      case NodeKind::Lookbehind:
        os << "Lookbehind probe_id=" << probe_id;
        break;
      case NodeKind::NegLookbehind:
        os << "NegLookbehind probe_id=" << probe_id;
        break;
      case NodeKind::WordBoundary:
        os << "WordBoundary";
        break;
      case NodeKind::NegWordBoundary:
        os << "NegWordBoundary";
        break;
      case NodeKind::Backref:
        os << "Backref(" << group_id << ")";
        break;
      case NodeKind::CaseInsensitiveBackref:
        os << "CaseInsensitiveBackref(" << group_id << ")";
        break;
    }

    if (extra) {
      os << " child_first=" << child_first << " child_last=" << child_last
         << " next_sibling=" << next_sibling << " prev_sibling=" << prev_sibling
         << " fixed_width=" << fixed_width
         << " fixed_width_prefix=" << fixed_width_prefix;
    }
  }
};

/// Recursively pretty-print the AST rooted at nodeIdx with indentation.
namespace detail_print {

inline void prettyPrintCharInRange(auto& os, unsigned char c) {
  switch (c) {
    case '\\':
      os << "\\\\";
      break;
    case ']':
      os << "\\]";
      break;
    case '-':
      os << "\\-";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\r':
      os << "\\r";
      break;
    case '\t':
      os << "\\t";
      break;
    case '\0':
      os << "\\0";
      break;
    default:
      if (c >= 0x20 && c < 0x7f) {
        os << static_cast<char>(c);
      } else {
        // Hex escape for non-printable characters.
        os << "\\x";
        constexpr char hex[] = "0123456789abcdef";
        os << hex[c >> 4] << hex[c & 0xf];
      }
      break;
  }
}

inline void prettyPrintCharClass(
    auto& os, const auto& ast, int charClassIndex) {
  if (charClassIndex < 0) {
    return;
  }
  const auto& cc = ast.char_classes[charClassIndex];
  os << '[';
  for (int i = 0; i < cc.range_count; ++i) {
    const auto& r = ast.ranges[cc.range_offset + i];
    prettyPrintCharInRange(os, r.lo);
    if (r.hi != r.lo) {
      os << '-';
      prettyPrintCharInRange(os, r.hi);
    }
  }
  os << ']';
}

} // namespace detail_print

void prettyPrintAst(
    auto& os, const auto& ast, int nodeIdx, bool extra, int depth = 0) {
  if (nodeIdx < 0) {
    return;
  }
  for (int i = 0; i < depth * 2; ++i) {
    os << ' ';
  }
  ast.nodes[nodeIdx].prettyPrint(os, extra);
  const auto& node = ast.nodes[nodeIdx];
  if (node.char_class_index >= 0) {
    os << ' ';
    detail_print::prettyPrintCharClass(os, ast, node.char_class_index);
  }
  os << '\n';
  int child = node.child_first;
  while (child >= 0) {
    prettyPrintAst(os, ast, child, extra, depth + 1);
    child = ast.nodes[child].next_sibling;
  }
}

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

  // Named capture group entries. Append-only during parsing via
  // addNamedGroup; ChunkedBuffer doesn't need indexing here. Linearized
  // into ParseResult::named_groups by compact() and then sorted by name
  // for runtime binary search.
  ChunkedBuffer<NamedGroupEntry> named_group_store;

  int group_count = 0;
  NodeIdx root = kNoNode;
  bool valid = false;
  bool nfa_compatible = true;
  int min_width = 0;
  int probe_count = 0;
  bool has_lookbehind = false;
  bool has_backref = false;
  bool backtrack_safe = false;

  // Literal buffer for the leading literal prefix: occupies [0..prefix_len-1].
  char* literal_buf = nullptr;
  int literal_buf_size = 0;
  int prefix_len = 0;
  int prefix_strip_len = 0;

  // Trailing dot-star optimization metadata.
  int trailing_dot_star_min = -1;
  bool trailing_dot_star_dot_all = false;
  int trailing_dot_star_anchor = -1;

  // Leading dot-star optimization metadata.
  int leading_dot_star_min = -1;
  bool leading_dot_star_dot_all = false;
  int leading_dot_star_anchor = -1;

 private:
  template <typename T>
  static constexpr void grow(T*& arr, int& cap) noexcept {
    int newCap = cap * 2;
    auto* newArr = new T[newCap]{};
    constexpr_copy_n(arr, cap, newArr);
    delete[] arr;
    arr = newArr;
    cap = newCap;
  }

 public:
  constexpr std::string_view literal_prefix() const noexcept {
    return std::string_view(literal_buf, prefix_len);
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
    delete[] prefix_group_adjustments;
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

  // Mark a node as Dead. The node must already be unlinked from the
  // AST graph (child_first/next_sibling/prev_sibling of its parent
  // and siblings updated) before calling this. compact() skips Dead
  // nodes entirely, so any remaining links from live nodes to a Dead
  // node will produce dangling kNoNode references.
  constexpr void markDead(NodeIdx idx) noexcept {
    nodes[idx].kind = NodeKind::Dead;
  }

  // Mark a node and all of its descendants as Dead.
  constexpr void markDeadRecursive(NodeIdx idx) noexcept {
    if (idx < 0) {
      return;
    }
    markDead(idx);
    int child = nodes[idx].child_first;
    while (child >= 0) {
      int next = nodes[child].next_sibling;
      markDeadRecursive(child);
      child = next;
    }
  }

  constexpr int addCharClass(const CharRangeSet& rs) noexcept {
    // Deduplicate: if an identical class already exists, reuse it.
    int newCount = rs.count();
    for (int i = 0; i < char_class_count; ++i) {
      if (char_classes[i].range_count != newCount) {
        continue;
      }
      int offset = char_classes[i].range_offset;
      bool match = true;
      int ri = 0;
      rs.forEach([&](CharRange r) {
        if (match && !(ranges[offset + ri] == r)) {
          match = false;
        }
        ++ri;
      });
      if (match) {
        return i;
      }
    }

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

  // Append a new named group entry. Caller has already validated that
  // `name` fits in NamedGroupEntry::kMaxNameLen. Caller is also expected
  // to check for duplicates first via hasNamedGroup; this method does
  // not deduplicate.
  constexpr void addNamedGroup(std::string_view name, int groupId) noexcept {
    NamedGroupEntry entry;
    int len = static_cast<int>(name.size());
    if (len > NamedGroupEntry::kMaxNameLen) {
      len = NamedGroupEntry::kMaxNameLen;
    }
    for (int i = 0; i < len; ++i) {
      entry.name[i] = name[i];
    }
    entry.group_id = groupId;
    named_group_store.append(&entry, 1);
  }

  // Byte-exact (case-sensitive) lookup over named_group_store. Used by
  // the parser to detect duplicate names at definition time and to
  // resolve \k<name> / (?P=name) backreferences. Linear scan over the
  // ChunkedBuffer's blocks; total entry count is small in practice.
  constexpr bool hasNamedGroup(std::string_view name) const noexcept {
    const auto* block = &named_group_store.first_;
    while (block) {
      for (int i = 0; i < block->count; ++i) {
        if (block->data[i].nameView() == name) {
          return true;
        }
      }
      block = block->next;
    }
    return false;
  }

  // Look up a named group's group_id. Returns -1 if not found.
  constexpr int findNamedGroup(std::string_view name) const noexcept {
    const auto* block = &named_group_store.first_;
    while (block) {
      for (int i = 0; i < block->count; ++i) {
        if (block->data[i].nameView() == name) {
          return block->data[i].group_id;
        }
      }
      block = block->next;
    }
    return -1;
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

struct ParseSizes {
  std::size_t max_nodes;
  std::size_t max_ranges;
  std::size_t max_classes;
  std::size_t literal_buf_size;
  std::size_t literal_chars_size;
  std::size_t prefix_group_adjustments;
  std::size_t max_named_groups;

  constexpr bool operator==(const ParseSizes&) const = default;
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
  int min_width = 0;
  int probe_count = 0;
  bool has_lookbehind = false;
  bool has_backref = false;
  bool backtrack_safe = false;

  // Literal buffer for the leading literal prefix.
  char literal_buf[Sizes.literal_buf_size > 0 ? Sizes.literal_buf_size : 1] =
      {};
  int literal_buf_size = static_cast<int>(Sizes.literal_buf_size);
  int prefix_len = 0;
  int prefix_strip_len = 0;

  // Trailing dot-star optimization metadata.
  int trailing_dot_star_min = -1;
  bool trailing_dot_star_dot_all = false;
  int trailing_dot_star_anchor = -1;

  // Leading dot-star optimization metadata.
  int leading_dot_star_min = -1;
  bool leading_dot_star_dot_all = false;
  int leading_dot_star_anchor = -1;

  constexpr std::string_view literal_prefix() const noexcept {
    return std::string_view(literal_buf, prefix_len);
  }

  PrefixGroupAdjustment prefix_group_adjustments
      [Sizes.prefix_group_adjustments > 0 ? Sizes.prefix_group_adjustments
                                          : 1] = {};
  int prefix_group_adjustment_count = 0;

  // Sorted (by name) array of named-group entries.
  // for runtime binary-search lookup. Size is precisely
  // Sizes.max_named_groups (using 1 as a placeholder when empty).
  NamedGroupEntry
      named_groups[Sizes.max_named_groups > 0 ? Sizes.max_named_groups : 1] =
          {};
  int named_group_count = 0;

  // Literal character storage for AstNode::literal string_views.
  char literal_chars
      [Sizes.literal_chars_size > 0 ? Sizes.literal_chars_size : 1] = {};
  int literal_chars_count = 0;

  std::size_t error_pos = 0;
  char error_message[128] = {};

  constexpr ParseResult() noexcept = default;
  constexpr ~ParseResult() = default;

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
        min_width(src.min_width),
        probe_count(src.probe_count),
        has_lookbehind(src.has_lookbehind),
        has_backref(src.has_backref),
        backtrack_safe(src.backtrack_safe),
        literal_buf_size(src.literal_buf_size),
        prefix_len(src.prefix_len),
        prefix_strip_len(src.prefix_strip_len),
        trailing_dot_star_min(src.trailing_dot_star_min),
        trailing_dot_star_dot_all(src.trailing_dot_star_dot_all),
        trailing_dot_star_anchor(src.trailing_dot_star_anchor),
        leading_dot_star_min(src.leading_dot_star_min),
        leading_dot_star_dot_all(src.leading_dot_star_dot_all),
        leading_dot_star_anchor(src.leading_dot_star_anchor),
        prefix_group_adjustment_count(src.prefix_group_adjustment_count),
        named_group_count(src.named_group_count),
        literal_chars_count(src.literal_chars_count),
        error_pos(src.error_pos) {
    // Copy literal_chars buffer
    for (int i = 0; i < src.literal_chars_count; ++i) {
      literal_chars[i] = src.literal_chars[i];
    }
    // Copy prefix group adjustments
    constexpr_copy_n(
        src.prefix_group_adjustments,
        src.prefix_group_adjustment_count,
        prefix_group_adjustments);
    // Copy named groups
    constexpr_copy_n(src.named_groups, src.named_group_count, named_groups);
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
    constexpr_copy_n(src.error_message, sizeof(error_message), error_message);
  }

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    return charClassTest(ranges + cc.range_offset, cc.range_count, c);
  }
};

// Recursively mark all nodes reachable from nodeIdx in the visited bitset.
template <typename AstT>
constexpr void markReachable(
    const AstT& builder, int nodeIdx, DynamicBitset& visited) noexcept {
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
  sizes.prefix_group_adjustments = builder.prefix_group_adjustment_count;
  sizes.literal_buf_size = builder.literal_buf_size;
  sizes.max_named_groups =
      static_cast<std::size_t>(builder.named_group_store.total_count_);
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
  result.min_width = builder.min_width;
  result.probe_count = builder.probe_count;
  result.has_lookbehind = builder.has_lookbehind;
  result.has_backref = builder.has_backref;
  result.backtrack_safe = builder.backtrack_safe;
  result.trailing_dot_star_min = builder.trailing_dot_star_min;
  result.trailing_dot_star_dot_all = builder.trailing_dot_star_dot_all;
  result.trailing_dot_star_anchor = builder.trailing_dot_star_anchor;
  result.leading_dot_star_min = builder.leading_dot_star_min;
  result.leading_dot_star_dot_all = builder.leading_dot_star_dot_all;
  result.leading_dot_star_anchor = builder.leading_dot_star_anchor;

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

  auto remapChildFirst = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    if (remap[idx] == kNoNode) {
      folly::throw_exception<std::runtime_error>(
          "compact: child_first links to Dead node");
    }
    return static_cast<NodeIdx>(remap[idx]);
  };
  auto remapChildLast = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    if (remap[idx] == kNoNode) {
      folly::throw_exception<std::runtime_error>(
          "compact: child_last links to Dead node");
    }
    return static_cast<NodeIdx>(remap[idx]);
  };
  auto remapNextSib = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    if (remap[idx] == kNoNode) {
      folly::throw_exception<std::runtime_error>(
          "compact: next_sibling links to Dead node");
    }
    return static_cast<NodeIdx>(remap[idx]);
  };
  auto remapPrevSib = [&](NodeIdx idx) -> NodeIdx {
    if (idx < 0 || idx >= builder.node_count) {
      return kNoNode;
    }
    if (remap[idx] == kNoNode) {
      folly::throw_exception<std::runtime_error>(
          "compact: prev_sibling links to Dead node");
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
    node.child_first = remapChildFirst(node.child_first);
    node.child_last = remapChildLast(node.child_last);
    node.next_sibling = remapNextSib(node.next_sibling);
    node.prev_sibling = remapPrevSib(node.prev_sibling);

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

  result.root = remapChildFirst(builder.root);

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
  constexpr_copy_n(
      builder.prefix_group_adjustments,
      builder.prefix_group_adjustment_count,
      result.prefix_group_adjustments);

  // Copy named groups from the builder's ChunkedBuffer into the
  // ParseResult's static array, then sort by name for runtime
  // binary-search lookup. Insertion sort is fine — count is small.
  result.named_group_count = builder.named_group_store.total_count_;
  builder.named_group_store.linearize(result.named_groups);
  for (int i = 1; i < result.named_group_count; ++i) {
    NamedGroupEntry key = result.named_groups[i];
    auto keyView = key.nameView();
    int j = i - 1;
    while (j >= 0 && result.named_groups[j].nameView() > keyView) {
      result.named_groups[j + 1] = result.named_groups[j];
      --j;
    }
    result.named_groups[j + 1] = key;
  }

  // Copy prefix from front of literal_buf
  result.prefix_len = builder.prefix_len;
  result.prefix_strip_len = builder.prefix_strip_len;
  constexpr_copy_n(builder.literal_buf, builder.prefix_len, result.literal_buf);

  delete[] remap;
  return result;
}

// --------------------------------------------------------------------------
// ProbeStore: compact storage for probe subtrees (possessive probes,
// future lookaround probes). Holds only the AST nodes, char classes,
// ranges, and literal chars needed by probe patterns.
// --------------------------------------------------------------------------

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
    Direction dir{}; // forward or reverse
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

  // Stubs for literal prefix — ProbeStore doesn't have these
  // but the possessive reverse probe code in NfaExecutor references them.
  static constexpr int prefix_len = 0;
  static constexpr int prefix_strip_len = 0;
  static constexpr std::string_view literal_prefix() noexcept { return {}; }

  // ProbeStore doesn't use trailing dot-star.
  static constexpr int trailing_dot_star_min = -1;
  static constexpr bool trailing_dot_star_dot_all = false;
  static constexpr int trailing_dot_star_anchor = -1;

  // ProbeStore doesn't use leading dot-star.
  static constexpr int leading_dot_star_min = -1;
  static constexpr bool leading_dot_star_dot_all = false;
  static constexpr int leading_dot_star_anchor = -1;

  constexpr bool charClassTestAt(int ccIdx, char c) const noexcept {
    const auto& cc = char_classes[ccIdx];
    return charClassTest(ranges + cc.range_offset, cc.range_count, c);
  }

  constexpr bool hasProbe(int probeId) const noexcept {
    return probeId >= 0 && probeId < probe_count && probes[probeId].root >= 0;
  }

  constexpr ProbeStore() noexcept = default;
  constexpr ~ProbeStore() = default;

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
    constexpr_copy_n(src.literal_chars, src.literal_chars_count, literal_chars);
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
    constexpr_copy_n(src.ranges, src.total_ranges, ranges);
    constexpr_copy_n(src.char_classes, src.char_class_count, char_classes);
    constexpr_copy_n(src.probes, src.probe_count, probes);
  }
};

// Count the probe subtrees in a forward-optimized AstBuilder, filtering
// by a surviving probe_id bitset. Returns ProbeStoreSizes.
template <int MaxProbeIds, typename AstT>
constexpr ProbeStoreSizes extractAndCountProbes(
    const AstT& builder, const FixedBitset<MaxProbeIds>& surviving) noexcept {
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
    markReachable(builder, builder.nodes[i].child_first, reachable);
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
template <ProbeStoreSizes Sizes, int MaxProbeIds, typename AstT>
constexpr ProbeStore<Sizes> compactProbeStore(
    const AstT& builder, const FixedBitset<MaxProbeIds>& surviving) noexcept {
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
    markReachable(builder, builder.nodes[i].child_first, reachable);
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
        store.probes[pid].dir = Direction::Forward;
        store.probes[pid].negated = false;
      } else if (kind == NodeKind::NegLookahead) {
        store.probes[pid].dir = Direction::Forward;
        store.probes[pid].negated = true;
      } else if (kind == NodeKind::Lookbehind) {
        store.probes[pid].dir = Direction::Reverse;
        store.probes[pid].negated = false;
        store.probes[pid].lookbehind_width = builder.nodes[i].min_repeat;
      } else if (kind == NodeKind::NegLookbehind) {
        store.probes[pid].dir = Direction::Reverse;
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

/// Returns the fixed width in bytes of the subtree rooted at nodeIdx,
/// or -1 if the subtree can match variable-length strings. Recurses
/// into children for composite nodes.
constexpr int computeFixedWidth(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& n = ast.nodes[nodeIdx];
  if (n.fixed_width != -2) {
    return n.fixed_width;
  }
  struct Ctx {
    const decltype(ast)& ast;
    int nodeIdx;
    const decltype(n)& n;
  };
  Ctx ctx{ast, nodeIdx, n};
  return NodeKindSwitch<
      Case<NodeKind::Empty, [](auto&) -> int { return 0; }>,
      Case<
          NodeKind::Literal,
          [](auto& c) -> int { return static_cast<int>(c.n.literal.size()); }>,
      Case<NodeKind::AnyByte, Fallthrough>,
      Case<NodeKind::CharClass, [](auto&) -> int { return 1; }>,
      Case<NodeKind::Anchor, Fallthrough>,
      Case<NodeKind::WordBoundary, Fallthrough>,
      Case<NodeKind::NegWordBoundary, Fallthrough>,
      Case<NodeKind::Dead, [](auto&) -> int { return 0; }>,
      Case<
          NodeKind::Group,
          [](auto& c) -> int {
            return computeFixedWidth(c.ast, c.n.child_first);
          }>,
      Case<
          NodeKind::Sequence,
          [](auto& c) -> int {
            int total = 0;
            int child = c.n.child_first;
            while (child >= 0) {
              int w = computeFixedWidth(c.ast, child);
              if (w < 0) {
                return -1;
              }
              total += w;
              child = c.ast.nodes[child].next_sibling;
            }
            return total;
          }>,
      Case<
          NodeKind::Alternation,
          [](auto& c) -> int {
            int child = c.n.child_first;
            if (child < 0) {
              return 0;
            }
            int width = computeFixedWidth(c.ast, child);
            if (width < 0) {
              return -1;
            }
            int next = c.ast.nodes[child].next_sibling;
            while (next >= 0) {
              int w = computeFixedWidth(c.ast, next);
              if (w < 0 || w != width) {
                return -1;
              }
              next = c.ast.nodes[next].next_sibling;
            }
            return width;
          }>,
      Case<
          NodeKind::Repeat,
          [](auto& c) -> int {
            if (c.n.min_repeat != c.n.max_repeat || c.n.max_repeat < 0) {
              return -1;
            }
            int innerWidth = computeFixedWidth(c.ast, c.n.child_first);
            if (innerWidth < 0) {
              return -1;
            }
            return innerWidth * c.n.min_repeat;
          }>,
      Case<NodeKind::Backref, Fallthrough>,
      Case<NodeKind::CaseInsensitiveBackref, [](auto&) -> int { return -1; }>,
      Case<NodeKind::Lookahead, Fallthrough>,
      Case<NodeKind::NegLookahead, Fallthrough>,
      Case<NodeKind::Lookbehind, Fallthrough>,
      Case<NodeKind::NegLookbehind, [](auto&) -> int { return 0; }>>::
      dispatch(n.kind, ctx);
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
    case NodeKind::CaseInsensitiveBackref:
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
  if (node.kind == NodeKind::Lookahead || node.kind == NodeKind::NegLookahead ||
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
// reassignGroupIds: DFS walk that re-assigns capturing group IDs
// sequentially. When reverseChildren is true, iterates children via
// child_last → prev_sibling (for reversed ASTs).
// Also updates Backref/CaseInsensitiveBackref nodes and
// named_group_store entries.
// --------------------------------------------------------------------------

namespace reassign_detail {

constexpr void collectGroupMappingDfs(
    const AstBuilder& builder,
    int nodeIdx,
    bool reverseChildren,
    int* mapping,
    int& nextId) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  const auto& node = builder.nodes[nodeIdx];
  if (node.kind == NodeKind::Dead) {
    return;
  }

  if (node.kind == NodeKind::Group && node.capturing) {
    mapping[node.group_id] = nextId++;
  }

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      if (reverseChildren) {
        int child = node.child_last;
        while (child >= 0) {
          collectGroupMappingDfs(
              builder, child, reverseChildren, mapping, nextId);
          child = builder.nodes[child].prev_sibling;
        }
      } else {
        int child = node.child_first;
        while (child >= 0) {
          collectGroupMappingDfs(
              builder, child, reverseChildren, mapping, nextId);
          child = builder.nodes[child].next_sibling;
        }
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      collectGroupMappingDfs(
          builder, node.child_first, reverseChildren, mapping, nextId);
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

constexpr void reassignProbeIdsDfs(
    AstBuilder& builder, int nodeIdx, bool reverseChildren) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& node = builder.nodes[nodeIdx];
  if (node.kind == NodeKind::Dead) {
    return;
  }

  if (node.kind == NodeKind::Repeat &&
      (node.repeat_mode == RepeatMode::Possessive ||
       node.repeat_mode == RepeatMode::PossessiveProbed)) {
    node.probe_id = builder.probe_count++;
  }
  if (node.kind == NodeKind::Lookahead || node.kind == NodeKind::NegLookahead ||
      node.kind == NodeKind::Lookbehind ||
      node.kind == NodeKind::NegLookbehind) {
    node.probe_id = builder.probe_count++;
  }

  switch (node.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      if (reverseChildren) {
        int child = node.child_last;
        while (child >= 0) {
          reassignProbeIdsDfs(builder, child, reverseChildren);
          child = builder.nodes[child].prev_sibling;
        }
      } else {
        int child = node.child_first;
        while (child >= 0) {
          reassignProbeIdsDfs(builder, child, reverseChildren);
          child = builder.nodes[child].next_sibling;
        }
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
      reassignProbeIdsDfs(builder, node.child_first, reverseChildren);
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

} // namespace reassign_detail

constexpr void reassignGroupIds(
    AstBuilder& builder, int nodeIdx, bool reverseChildren) noexcept {
  int maxOldId = builder.group_count;
  int mapSize = maxOldId + 1;
  int* mapping = new int[mapSize > 0 ? mapSize : 1]{};
  for (int i = 0; i < mapSize; ++i) {
    mapping[i] = -1;
  }

  int nextId = 1;
  reassign_detail::collectGroupMappingDfs(
      builder, nodeIdx, reverseChildren, mapping, nextId);
  builder.group_count = nextId - 1;

  // Apply mapping to all Group, Backref, CaseInsensitiveBackref nodes.
  for (int i = 0; i < builder.node_count; ++i) {
    auto& n = builder.nodes[i];
    if ((n.kind == NodeKind::Group && n.capturing) ||
        n.kind == NodeKind::Backref ||
        n.kind == NodeKind::CaseInsensitiveBackref) {
      if (n.group_id >= 0 && n.group_id < mapSize && mapping[n.group_id] >= 0) {
        n.group_id = mapping[n.group_id];
      }
    }
  }

  // Apply mapping to named_group_store entries.
  auto* block = &builder.named_group_store.first_;
  while (block) {
    for (int i = 0; i < block->count; ++i) {
      int old = block->data[i].group_id;
      if (old >= 0 && old < mapSize && mapping[old] >= 0) {
        block->data[i].group_id = mapping[old];
      }
    }
    block = block->next;
  }

  delete[] mapping;
}

constexpr void reassignProbeIds(
    AstBuilder& builder, int nodeIdx, bool reverseChildren) noexcept {
  builder.probe_count = 0;
  reassign_detail::reassignProbeIdsDfs(builder, nodeIdx, reverseChildren);
}

/// Returns the minimum possible match width for the subtree rooted at
/// nodeIdx. Repeat/Alternation lower bounds, zero-width kinds return 0.
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
    case NodeKind::CaseInsensitiveBackref:
      return 0;
    case NodeKind::Literal:
      return static_cast<int>(n.literal.size());
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

/// Returns the fixed-width prefix length (in bytes) of the subtree rooted
/// at nodeIdx. Unlike computeFixedWidth, this returns the longest leading
/// prefix that has a known fixed width, even if the tail is variable.
constexpr int computeFixedWidthPrefix(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return 0;
  }
  const auto& n = ast.nodes[nodeIdx];
  if (n.fixed_width_prefix != -1) {
    return n.fixed_width_prefix;
  }
  struct Ctx {
    const decltype(ast)& ast;
    int nodeIdx;
    const decltype(n)& n;
  };
  Ctx ctx{ast, nodeIdx, n};
  return NodeKindSwitch<
      Case<NodeKind::Empty, [](auto&) -> int { return 0; }>,
      Case<
          NodeKind::Literal,
          [](auto& c) -> int { return static_cast<int>(c.n.literal.size()); }>,
      Case<NodeKind::AnyByte, Fallthrough>,
      Case<NodeKind::CharClass, [](auto&) -> int { return 1; }>,
      Case<NodeKind::Anchor, Fallthrough>,
      Case<NodeKind::WordBoundary, Fallthrough>,
      Case<NodeKind::NegWordBoundary, Fallthrough>,
      Case<NodeKind::Dead, [](auto&) -> int { return 0; }>,
      Case<
          NodeKind::Group,
          [](auto& c) -> int {
            return computeFixedWidthPrefix(c.ast, c.n.child_first);
          }>,
      Case<
          NodeKind::Sequence,
          [](auto& c) -> int {
            int total = 0;
            int child = c.n.child_first;
            while (child >= 0) {
              int fw = computeFixedWidth(c.ast, child);
              if (fw >= 0) {
                total += fw;
                child = c.ast.nodes[child].next_sibling;
              } else {
                total += computeFixedWidthPrefix(c.ast, child);
                break;
              }
            }
            return total;
          }>,
      Case<
          NodeKind::Alternation,
          [](auto& c) -> int {
            int child = c.n.child_first;
            if (child < 0) {
              return 0;
            }
            int minPrefix = computeFixedWidthPrefix(c.ast, child);
            int next = c.ast.nodes[child].next_sibling;
            while (next >= 0) {
              int p = computeFixedWidthPrefix(c.ast, next);
              if (p < minPrefix) {
                minPrefix = p;
              }
              next = c.ast.nodes[next].next_sibling;
            }
            return minPrefix;
          }>,
      Case<
          NodeKind::Repeat,
          [](auto& c) -> int {
            if (c.n.min_repeat > 0) {
              int innerFW = computeFixedWidth(c.ast, c.n.child_first);
              if (innerFW >= 0) {
                return innerFW * c.n.min_repeat;
              }
              return computeFixedWidthPrefix(c.ast, c.n.child_first);
            }
            return 0;
          }>,
      Case<NodeKind::Backref, Fallthrough>,
      Case<NodeKind::CaseInsensitiveBackref, [](auto&) -> int { return 0; }>,
      Case<NodeKind::Lookahead, Fallthrough>,
      Case<NodeKind::NegLookahead, Fallthrough>,
      Case<NodeKind::Lookbehind, Fallthrough>,
      Case<NodeKind::NegLookbehind, [](auto&) -> int { return 0; }>>::
      dispatch(n.kind, ctx);
}

// Bottom-up pass that fills fixed_width / fixed_width_prefix on every
// reachable node. Subsequent computeFixedWidth/computeFixedWidthPrefix
// calls become O(1) cache hits as long as the tree structure is not
// mutated. Callers that mutate the tree afterward should invalidate the
// stale entries (set fixed_width=-2, fixed_width_prefix=-1) on the
// affected nodes; deeper unchanged subtrees keep their valid caches and
// their parents' recomputation will reuse them.
constexpr void precomputeFixedWidths(auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  auto& n = ast.nodes[nodeIdx];
  // Visit children first so the recursive computeFixedWidth* calls below
  // hit cached child values instead of re-walking each subtree.
  struct Ctx {
    decltype(ast)& ast;
    int nodeIdx;
    decltype(n)& n;
  };
  Ctx ctx{ast, nodeIdx, n};
  NodeKindSwitch<
      Case<NodeKind::Group, Fallthrough>,
      Case<NodeKind::Lookahead, Fallthrough>,
      Case<NodeKind::Lookbehind, Fallthrough>,
      Case<NodeKind::NegLookahead, Fallthrough>,
      Case<NodeKind::NegLookbehind, Fallthrough>,
      Case<
          NodeKind::Repeat,
          [](auto& c) { precomputeFixedWidths(c.ast, c.n.child_first); }>,
      Case<NodeKind::Sequence, Fallthrough>,
      Case<
          NodeKind::Alternation,
          [](auto& c) {
            int child = c.n.child_first;
            while (child >= 0) {
              precomputeFixedWidths(c.ast, child);
              child = c.ast.nodes[child].next_sibling;
            }
          }>,
      Case<NodeKind::Anchor, Fallthrough>,
      Case<NodeKind::AnyByte, Fallthrough>,
      Case<NodeKind::Backref, Fallthrough>,
      Case<NodeKind::CaseInsensitiveBackref, Fallthrough>,
      Case<NodeKind::CharClass, Fallthrough>,
      Case<NodeKind::Empty, Fallthrough>,
      Case<NodeKind::Literal, Fallthrough>,
      Case<NodeKind::NegWordBoundary, Fallthrough>,
      Case<NodeKind::WordBoundary, Fallthrough>,
      Case<NodeKind::Dead, [](auto&) {}>>::dispatch(n.kind, ctx);
  n.fixed_width = computeFixedWidth(ast, nodeIdx);
  n.fixed_width_prefix = computeFixedWidthPrefix(ast, nodeIdx);
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
          break;
        }
        if (offset < accumulated + fw) {
          return resolveCharAtOffset(ast, child, offset - accumulated);
        }
        accumulated += fw;
        child = ast.nodes[child].next_sibling;
      }
      // Fallback: if the Sequence has a materialized branch char class
      // (set during materializeDiscriminatorCharClasses for branches
      // with optional prefixes), return it.
      if (n.char_class_index >= 0) {
        return {nodeIdx, 0, true};
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
    case NodeKind::AnyByte:
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
    case NodeKind::AnyByte:
    case NodeKind::Anchor:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::NegWordBoundary:
    case NodeKind::WordBoundary:
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
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
      return false;
  }
}

} // namespace folly::regex::detail
