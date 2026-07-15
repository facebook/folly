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

// Constexpr AST → regex string serializer.
//
// Walks a post-optimization AstBuilder via DFS from builder.root and
// emits a syntactically valid regex string that, when re-parsed with
// Flags::None, produces a structurally equivalent AST.
//
// Key design points:
//   - All flag-dependent information is encoded in syntax itself
//     (scoped (?m:^) for BeginLine, (?i:\NN) for CaseInsensitiveBackref,
//      \C for AnyByte, etc.) so the inner re-parse uses Flags::None.
//   - (?:...) wrapping preserves operator precedence across re-parse.
//   - Large char classes (252+ of 256 chars) emit as inverted sets.

#pragma once

#include <cstddef>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>

namespace folly::regex::detail {

// Structural type usable as C++20 NTTP.
template <std::size_t MaxLen>
struct SerializedPattern {
  char data[MaxLen + 1] = {};
  std::size_t len = 0;
  constexpr std::size_t length() const noexcept { return len; }
  constexpr char operator[](std::size_t i) const noexcept { return data[i]; }
  constexpr bool operator==(const SerializedPattern&) const = default;
};

// Upper bound on serialized regex length given the original pattern length.
// In serialized mode, annotations add overhead: (?~gN:), (?~pN:), (?~DN:),
// (?~=N:), etc. The * 6 multiplier and + 128 constant accommodate the
// worst case where every node gets an annotation wrapper.
constexpr std::size_t UpperBoundSerializedLen(std::size_t patLen) noexcept {
  return patLen * 6 + 128;
}

namespace serializer_detail {

// Constexpr output buffer.
struct EmitBuf {
  char* data;
  std::size_t len = 0;
  std::size_t cap;
  bool serialized_mode = false;

  constexpr void put(char c) noexcept {
    if (len < cap) {
      data[len] = c;
    }
    ++len;
  }
};

constexpr void emitHexByte(EmitBuf& out, unsigned char c) noexcept {
  constexpr char hex[] = "0123456789abcdef";
  out.put('\\');
  out.put('x');
  out.put(hex[c >> 4]);
  out.put(hex[c & 0xf]);
}

constexpr void emitDecimal(EmitBuf& out, int val) noexcept {
  if (val == 0) {
    out.put('0');
    return;
  }
  char digits[10];
  int count = 0;
  while (val > 0) {
    digits[count++] = static_cast<char>('0' + val % 10);
    val /= 10;
  }
  for (int i = count - 1; i >= 0; --i) {
    out.put(digits[i]);
  }
}

// Emit a single character, escaping metacharacters as needed.
// inCharClass: true when inside [...].
constexpr void emitLiteralChar(
    unsigned char c, bool inCharClass, EmitBuf& out) noexcept {
  if (inCharClass) {
    // Inside [...]: escape ] \ - ^
    if (c == ']' || c == '\\' || c == '-' || c == '^') {
      out.put('\\');
      out.put(static_cast<char>(c));
      return;
    }
  } else {
    // Outside class: escape regex metacharacters.
    switch (c) {
      case '\\':
      case '.':
      case '[':
      case ']':
      case '*':
      case '+':
      case '?':
      case '(':
      case ')':
      case '{':
      case '}':
      case '|':
      case '^':
      case '$':
        out.put('\\');
        out.put(static_cast<char>(c));
        return;
    }
  }
  // Non-printable bytes as \xHH.
  if (c < 0x20 || c >= 0x7f) {
    emitHexByte(out, c);
    return;
  }
  out.put(static_cast<char>(c));
}

// Emit a character class from builder's ranges.
constexpr void emitCharClass(
    const AstBuilder& builder, int ccIdx, EmitBuf& out) noexcept {
  const auto& cc = builder.char_classes[ccIdx];

  // Inverse-set optimization: if the total gap (chars NOT in the class)
  // is ≤4, emit [^gap] instead. Uses sorted, non-overlapping ranges
  // directly in O(range_count) — NOT O(256).
  if (cc.range_count > 0) {
    int totalGap = 0;
    // Gap before first range.
    totalGap += builder.ranges[cc.range_offset].lo;
    // Gap between adjacent ranges.
    for (int i = 0; i + 1 < cc.range_count; ++i) {
      totalGap += builder.ranges[cc.range_offset + i + 1].lo -
          builder.ranges[cc.range_offset + i].hi - 1;
    }
    // Gap after last range.
    totalGap += 255 - builder.ranges[cc.range_offset + cc.range_count - 1].hi;

    if (totalGap > 0 && totalGap <= 4) {
      unsigned char gap[4];
      int gapCount = 0;
      // Collect gap chars before first range.
      for (unsigned int c = 0;
           c < builder.ranges[cc.range_offset].lo && gapCount < 4;
           ++c) {
        gap[gapCount++] = static_cast<unsigned char>(c);
      }
      // Collect gap chars between adjacent ranges.
      for (int i = 0; i + 1 < cc.range_count && gapCount < 4; ++i) {
        unsigned int gapStart = builder.ranges[cc.range_offset + i].hi + 1;
        unsigned int gapEnd = builder.ranges[cc.range_offset + i + 1].lo;
        for (unsigned int c = gapStart; c < gapEnd && gapCount < 4; ++c) {
          gap[gapCount++] = static_cast<unsigned char>(c);
        }
      }
      // Collect gap chars after last range.
      {
        unsigned int lastHi =
            builder.ranges[cc.range_offset + cc.range_count - 1].hi;
        for (unsigned int c = lastHi + 1; c <= 255 && gapCount < 4; ++c) {
          gap[gapCount++] = static_cast<unsigned char>(c);
        }
      }
      out.put('[');
      out.put('^');
      for (int i = 0; i < gapCount; ++i) {
        emitLiteralChar(gap[i], true, out);
      }
      out.put(']');
      return;
    }
  }

  // Normal emission.
  out.put('[');
  for (int i = 0; i < cc.range_count; ++i) {
    const auto& r = builder.ranges[cc.range_offset + i];
    emitLiteralChar(r.lo, true, out);
    if (r.hi != r.lo) {
      out.put('-');
      emitLiteralChar(r.hi, true, out);
    }
  }
  out.put(']');
}

// Emit (?~<prefix> wrapper.
constexpr void emitAnnotationPrefix(
    EmitBuf& out, char type, int value) noexcept {
  out.put('(');
  out.put('?');
  out.put('~');
  out.put(type);
  emitDecimal(out, value);
  out.put(':');
}

// Forward declaration for mutual recursion with emitWrapped.
constexpr void emitNode(
    const AstBuilder& builder, int nodeIdx, EmitBuf& out) noexcept;

// Emit child wrapped in (?:...).
constexpr void emitWrapped(
    const AstBuilder& builder, int nodeIdx, EmitBuf& out) noexcept {
  out.put('(');
  out.put('?');
  out.put(':');
  emitNode(builder, nodeIdx, out);
  out.put(')');
}

// Look up a named group's name by group_id. Returns name length, or 0.
constexpr int findGroupName(
    const AstBuilder& builder, int groupId, char* nameBuf) noexcept {
  const auto* block = &builder.named_group_store.first_;
  while (block) {
    for (int i = 0; i < block->count; ++i) {
      if (block->data[i].group_id == groupId) {
        auto name = block->data[i].nameView();
        int len = static_cast<int>(name.size());
        for (int j = 0; j < len; ++j) {
          nameBuf[j] = name[j];
        }
        return len;
      }
    }
    block = block->next;
  }
  return 0;
}

// Recursive DFS emission of a single AST node.
constexpr void emitNode(
    const AstBuilder& builder, int nodeIdx, EmitBuf& out) noexcept {
  if (nodeIdx < 0) {
    return;
  }
  const auto& node = builder.nodes[nodeIdx];
  if (node.kind == NodeKind::Dead) {
    return;
  }

  switch (node.kind) {
    case NodeKind::Empty:
      break;

    case NodeKind::Literal:
      for (std::size_t i = 0; i < node.literal.size(); ++i) {
        emitLiteralChar(
            static_cast<unsigned char>(node.literal[i]), false, out);
      }
      break;

    case NodeKind::AnyByte:
      out.put('\\');
      out.put('C');
      break;

    case NodeKind::CharClass:
      emitCharClass(builder, node.char_class_index, out);
      break;

    case NodeKind::Sequence: {
      int child = node.child_first;
      while (child >= 0) {
        // Alternation children need wrapping in a Sequence for precedence.
        // In serialized mode, Alternations with discriminator self-wrap
        // via (?~DN:...) which serves as the precedence wrapper too.
        if (builder.nodes[child].kind == NodeKind::Alternation) {
          if (out.serialized_mode &&
              builder.nodes[child].discriminator_offset >= 0) {
            // Self-wrapping — just call emitNode, which adds (?~DN:...).
            emitNode(builder, child, out);
          } else {
            emitWrapped(builder, child, out);
          }
        } else {
          emitNode(builder, child, out);
        }
        child = builder.nodes[child].next_sibling;
      }
      break;
    }

    case NodeKind::Alternation: {
      // In serialized mode, wrap Alternations that have a discriminator
      // offset with (?~DN:...) so the inner parser can recover the offset.
      bool emitDiscWrapper =
          out.serialized_mode && node.discriminator_offset >= 0;
      if (emitDiscWrapper) {
        emitAnnotationPrefix(out, 'D', node.discriminator_offset);
      }
      int child = node.child_first;
      bool first = true;
      while (child >= 0) {
        if (!first) {
          out.put('|');
        }
        first = false;
        emitNode(builder, child, out);
        child = builder.nodes[child].next_sibling;
      }
      if (emitDiscWrapper) {
        out.put(')');
      }
      break;
    }

      // Helper case: emitNode handles Alternation inline above; the
      // discriminator wrapping for serialized_mode is done at the call
      // sites (Sequence wrapping and Group wrapping) below.

    case NodeKind::Repeat: {
      // In serialized_mode, PossessiveProbed repeats (reverse only) with
      // probe_id >= 0 are wrapped in (?~pN:...) so the inner parser can
      // reconstruct the probe_id. Emitted for both Possessive (forward)
      // and PossessiveProbed (reverse) — the forward probe subtrees are
      // needed by the probe store for runtime verification.
      bool emitProbeWrapper = out.serialized_mode &&
          (node.repeat_mode == RepeatMode::Possessive ||
           node.repeat_mode == RepeatMode::PossessiveProbed) &&
          node.probe_id >= 0;
      if (emitProbeWrapper) {
        emitAnnotationPrefix(out, 'p', node.probe_id);
      }

      int child = node.child_first;
      // Check if child needs (?:...) wrapping for correct quantifier binding.
      bool needsWrap = false;
      if (child >= 0) {
        const auto& ch = builder.nodes[child];
        switch (ch.kind) {
          case NodeKind::Literal:
            needsWrap = ch.literal.size() > 1;
            break;
          case NodeKind::Sequence:
          case NodeKind::Alternation:
          case NodeKind::Repeat:
          case NodeKind::Empty:
            needsWrap = true;
            break;
          case NodeKind::AnyByte:
          case NodeKind::CharClass:
          case NodeKind::Group:
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
            break;
        }
      }
      if (needsWrap) {
        emitWrapped(builder, child, out);
      } else {
        emitNode(builder, child, out);
      }
      // Quantifier suffix.
      if (node.min_repeat == 0 && node.max_repeat == -1) {
        out.put('*');
      } else if (node.min_repeat == 1 && node.max_repeat == -1) {
        out.put('+');
      } else if (node.min_repeat == 0 && node.max_repeat == 1) {
        out.put('?');
      } else {
        out.put('{');
        emitDecimal(out, node.min_repeat);
        if (node.max_repeat != node.min_repeat) {
          out.put(',');
          if (node.max_repeat >= 0) {
            emitDecimal(out, node.max_repeat);
          }
        }
        out.put('}');
      }
      // Mode suffix.
      if (node.repeat_mode == RepeatMode::Lazy) {
        out.put('?');
      } else if (
          node.repeat_mode == RepeatMode::Possessive ||
          node.repeat_mode == RepeatMode::PossessiveProbed) {
        out.put('+');
      }

      if (emitProbeWrapper) {
        out.put(')');
      }
      break;
    }

    case NodeKind::Group: {
      if (node.capturing && out.serialized_mode) {
        // Serialized mode: emit (?~gN:...) or (?~gN<name>:...)
        char nameBuf[NamedGroupEntry::kMaxNameLen + 1] = {};
        int nameLen = findGroupName(builder, node.group_id, nameBuf);
        out.put('(');
        out.put('?');
        out.put('~');
        out.put('g');
        emitDecimal(out, node.group_id);
        if (nameLen > 0) {
          out.put('<');
          for (int i = 0; i < nameLen; ++i) {
            out.put(nameBuf[i]);
          }
          out.put('>');
        }
        out.put(':');
      } else if (node.capturing) {
        char nameBuf[NamedGroupEntry::kMaxNameLen + 1] = {};
        int nameLen = findGroupName(builder, node.group_id, nameBuf);
        if (nameLen > 0) {
          out.put('(');
          out.put('?');
          out.put('<');
          for (int i = 0; i < nameLen; ++i) {
            out.put(nameBuf[i]);
          }
          out.put('>');
        } else {
          out.put('(');
        }
      } else {
        out.put('(');
        out.put('?');
        out.put(':');
      }
      emitNode(builder, node.child_first, out);
      out.put(')');
      break;
    }

    case NodeKind::Anchor:
      switch (node.anchor) {
        case AnchorKind::Begin:
          out.put('^');
          break;
        case AnchorKind::End:
          out.put('$');
          break;
        case AnchorKind::BeginLine:
          // Scoped flag: without Multiline, bare ^ → Begin not BeginLine.
          out.put('(');
          out.put('?');
          out.put('m');
          out.put(':');
          out.put('^');
          out.put(')');
          break;
        case AnchorKind::EndLine:
          out.put('(');
          out.put('?');
          out.put('m');
          out.put(':');
          out.put('$');
          out.put(')');
          break;
        case AnchorKind::StartOfString:
          out.put('\\');
          out.put('A');
          break;
        case AnchorKind::EndOfString:
          out.put('\\');
          out.put('z');
          break;
        case AnchorKind::EndOfStringOrNewline:
          out.put('\\');
          out.put('Z');
          break;
      }
      break;

    case NodeKind::WordBoundary:
      out.put('\\');
      out.put('b');
      break;

    case NodeKind::NegWordBoundary:
      out.put('\\');
      out.put('B');
      break;

    case NodeKind::Lookahead:
      if (out.serialized_mode && node.probe_id >= 0) {
        // (?~=N:...)
        emitAnnotationPrefix(out, '=', node.probe_id);
      } else {
        out.put('(');
        out.put('?');
        out.put('=');
      }
      emitNode(builder, node.child_first, out);
      out.put(')');
      break;

    case NodeKind::NegLookahead:
      if (out.serialized_mode && node.probe_id >= 0) {
        // (?~!N:...)
        emitAnnotationPrefix(out, '!', node.probe_id);
      } else {
        out.put('(');
        out.put('?');
        out.put('!');
      }
      emitNode(builder, node.child_first, out);
      out.put(')');
      break;

    case NodeKind::Lookbehind:
      if (out.serialized_mode && node.probe_id >= 0) {
        // (?~bN:...) or (?~bN,W:...)
        out.put('(');
        out.put('?');
        out.put('~');
        out.put('b');
        emitDecimal(out, node.probe_id);
        if (node.min_repeat >= 0) {
          out.put(',');
          emitDecimal(out, node.min_repeat);
        }
        out.put(':');
      } else {
        out.put('(');
        out.put('?');
        out.put('<');
        out.put('=');
      }
      emitNode(builder, node.child_first, out);
      out.put(')');
      break;

    case NodeKind::NegLookbehind:
      if (out.serialized_mode && node.probe_id >= 0) {
        // (?~BN:...) or (?~BN,W:...)
        out.put('(');
        out.put('?');
        out.put('~');
        out.put('B');
        emitDecimal(out, node.probe_id);
        if (node.min_repeat >= 0) {
          out.put(',');
          emitDecimal(out, node.min_repeat);
        }
        out.put(':');
      } else {
        out.put('(');
        out.put('?');
        out.put('<');
        out.put('!');
      }
      emitNode(builder, node.child_first, out);
      out.put(')');
      break;

    case NodeKind::Backref:
      out.put('\\');
      emitDecimal(out, node.group_id);
      break;

    case NodeKind::CaseInsensitiveBackref:
      // Scoped CI flag since global CI is stripped in inner re-parse.
      out.put('(');
      out.put('?');
      out.put('i');
      out.put(':');
      out.put('\\');
      emitDecimal(out, node.group_id);
      out.put(')');
      break;

    case NodeKind::Dead:
      break;
  }
}

} // namespace serializer_detail

// Serialize the post-optimization AST in builder to a regex string.
// markUnreachableNodes should have been called first.
// When serialized_mode is true, emits (?~...) annotation constructs
// encoding metadata (group IDs, probe IDs, discriminator offsets).
template <std::size_t MaxLen>
constexpr SerializedPattern<MaxLen> serializeAstToRegex(
    const AstBuilder& builder, bool serialized_mode = false) noexcept {
  SerializedPattern<MaxLen> result;
  serializer_detail::EmitBuf out{result.data, 0, MaxLen, serialized_mode};
  serializer_detail::emitNode(builder, builder.root, out);
  result.len = out.len;
  if (result.len <= MaxLen) {
    result.data[result.len] = '\0';
  }
  return result;
}

} // namespace folly::regex::detail
