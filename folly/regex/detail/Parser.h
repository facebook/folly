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

#include <stdexcept>
#include <string_view>

#include <folly/lang/Exception.h>
#include <folly/regex/Flags.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>

namespace folly::regex::detail {

enum class ParseErrorMode {
  CompileTime,
  RuntimeReport,
};

struct regex_parse_error : std::runtime_error {
  std::size_t position;
  regex_parse_error(const char* msg, std::size_t pos)
      : std::runtime_error(msg), position(pos) {}
};

struct Parser {
  std::string_view pattern;
  std::size_t pos = 0;
  AstBuilder& result;

  constexpr explicit Parser(std::string_view pat, AstBuilder& builder)
      : pattern(pat), result(builder) {}

  constexpr bool atEnd() const noexcept { return pos >= pattern.size(); }

  constexpr char peek() const noexcept { return atEnd() ? '\0' : pattern[pos]; }

  constexpr char advance() noexcept { return pattern[pos++]; }

  constexpr bool tryConsume(char c) noexcept {
    if (!atEnd() && pattern[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }

  constexpr void error(const char* msg) noexcept { result.setError(pos, msg); }

  // Report an error when a construct is rejected because the current
  // syntax flags don't enable it. Produces a uniform message format.
  constexpr void errorSyntax(const char* construct) noexcept {
    result.setError(pos, construct);
  }

  // Check that a byte is ASCII (<=127) when CaseInsensitive is active.
  // Reports an error and returns false if non-ASCII; returns true otherwise.
  constexpr bool checkCaseInsensitiveByte(
      unsigned char c, Flags flags) noexcept {
    if (hasFlag(flags, Flags::CaseInsensitive) && c > 127) {
      error("Non-ASCII byte in case-insensitive pattern");
      return false;
    }
    return true;
  }

  // Build a CharClass node containing both cases of an ASCII alpha byte.
  // Caller must ensure isAlpha(c). Used by case-insensitive Literal
  // expansion in parseSequence/parseAtom/parseEscape.
  constexpr NodeIdx makeCaseInsensitiveLiteralNode(unsigned char c) noexcept {
    CharRangeSet rs;
    rs.addChar(static_cast<unsigned char>(c | 0x20)); // lowercase
    rs.addChar(static_cast<unsigned char>(c & ~0x20)); // uppercase
    int ccIdx = result.addCharClass(rs);
    AstNode node;
    node.kind = NodeKind::CharClass;
    node.char_class_index = ccIdx;
    return result.addNode(node);
  }

  // (?x) extended mode: when active, skip whitespace bytes (space, tab,
  // CR, LF, FF, VT) and `# ... \n` line comments at every atom boundary.
  // Whitespace inside [...] character classes, inside \xNN / \NNN escape
  // sequences, and after `\` (escaped whitespace) is NOT skipped — those
  // contexts have their own parsing loops that don't call this helper.
  constexpr void skipExtendedWhitespace(Flags flags) noexcept {
    if (!hasFlag(flags, Flags::Extended)) {
      return;
    }
    while (!atEnd()) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
          c == '\v') {
        advance();
      } else if (c == '#') {
        // Comment: consume through (but not including) the next '\n'.
        // The newline will be picked up on the next iteration as
        // ignorable whitespace.
        while (!atEnd() && peek() != '\n') {
          advance();
        }
      } else {
        break;
      }
    }
  }

  // parseRegex receives flags BY VALUE — this is the scope boundary.
  // Inner parseRegex calls (one per group) get a fresh copy. Within a
  // single parseRegex scope, parseAlternation/parseSequence/etc. take
  // flags by REFERENCE so that (?i) directives propagate across
  // alternation branches (matching Perl/PCRE2 semantics).
  //
  // Syntax-mask-default: if no Syntax_* bits are set, OR in
  // SyntaxPreset_Folly so the full superset syntax is used. This is
  // idempotent for recursive calls since the syntax bits are already
  // present from the root call.
  constexpr NodeIdx parseRegex(Flags flags) noexcept {
    if ((flags & kSyntaxFlagMask) == Flags::None) {
      flags = flags | Flags::SyntaxPreset_Folly;
    }
    return parseAlternation(flags);
  }

  constexpr NodeIdx parseAlternation(Flags& flags) noexcept {
    NodeIdx first = parseSequence(flags);
    if (!result.valid) {
      return kNoNode;
    }

    if (!tryConsume('|')) {
      return first;
    }

    AstNode alt;
    alt.kind = NodeKind::Alternation;
    alt.child_first = first;

    int lastChild = first;
    int childCount = 1;

    while (true) {
      NodeIdx next = parseSequence(flags);
      if (!result.valid) {
        return kNoNode;
      }

      result.nodes[lastChild].next_sibling = next;
      result.nodes[next].prev_sibling = lastChild;
      lastChild = next;
      ++childCount;

      if (!tryConsume('|')) {
        break;
      }
    }
    result.nodes[lastChild].next_sibling = kNoNode;

    if (childCount == 2) {
      alt.child_first = first;
      alt.child_last = lastChild;
      return result.addNode(alt);
    }

    alt.child_first = first;
    alt.child_last = lastChild;
    return result.addNode(alt);
  }

  constexpr NodeIdx parseSequence(Flags& flags) noexcept {
    NodeIdx firstChild = kNoNode;
    NodeIdx prevChild = kNoNode;
    int childCount = 0;

    auto* litChunks = new ChunkedBuffer<char, 32>();

    auto flushLiteral = [&]() -> NodeIdx {
      if (litChunks->total_count_ == 0) {
        return kNoNode;
      }
      int len = litChunks->total_count_;
      auto* buf = new char[len];
      litChunks->linearize(buf);
      delete litChunks;
      litChunks = new ChunkedBuffer<char, 32>();
      AstNode lit;
      lit.kind = NodeKind::Literal;
      lit.literal = result.adoptLiteral(buf, len);
      return result.addNode(lit);
    };

    auto addChild = [&](NodeIdx child) {
      if (firstChild == kNoNode) {
        firstChild = child;
      }
      if (prevChild != kNoNode) {
        result.nodes[prevChild].next_sibling = child;
      }
      result.nodes[child].prev_sibling = prevChild >= 0 ? prevChild : kNoNode;
      prevChild = child;
      ++childCount;
    };

    while (!atEnd()) {
      skipExtendedWhitespace(flags);
      if (atEnd()) {
        break;
      }
      char c = peek();
      if (c == '|' || c == ')') {
        break;
      }

      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ' ') {
        // Plain literal character: check if a quantifier follows.
        bool quantifierFollows = false;
        if (pos + 1 < pattern.size()) {
          char next = pattern[pos + 1];
          quantifierFollows =
              (next == '*' || next == '+' || next == '?' || next == '{');
        }
        if (!quantifierFollows) {
          // For case-insensitive: alpha chars can't be coalesced into a
          // Literal — each becomes a CharClass with both cases. Flush any
          // accumulated non-alpha literal first.
          if (hasFlag(flags, Flags::CaseInsensitive) &&
              isAlpha(static_cast<unsigned char>(c))) {
            NodeIdx flushed = flushLiteral();
            if (flushed != kNoNode) {
              addChild(flushed);
            }
            advance();
            addChild(
                makeCaseInsensitiveLiteralNode(static_cast<unsigned char>(c)));
            continue;
          }
          litChunks->append(&c, 1);
          advance();
          continue;
        }
      }

      switch (c) {
        case '(':
        case '[':
        case '.':
        case '^':
        case '$':
        case '*':
        case '+':
        case '?':
          // Special character: fall through to flush + parseQuantified.
          break;

        case '\\': {
          // Try to coalesce escaped literal characters.
          if (pos + 1 < pattern.size()) {
            char next = pattern[pos + 1];
            // \x{...} is routed through parseEscape so multi-byte UTF-8
            // emission can produce a Literal node with 1-4 bytes. Single-
            // byte \x{N} (N <= 0x7F) still works there too, just as a
            // standalone Literal node instead of being merged with
            // adjacent literals.
            bool isXBrace =
                (next == 'x' && pos + 2 < pattern.size() &&
                 pattern[pos + 2] == '{' &&
                 hasFlag(flags, Flags::Syntax_XBraceHex));
            // An escape is "non-literal" (needs parseEscape) only if the
            // corresponding syntax flag is enabled. If the flag is off, the
            // escape falls through to parseEscapeChar which treats it as a
            // literal character.
            bool isNonLiteral =
                (((next == 'd' || next == 'D' || next == 'w' || next == 'W' ||
                   next == 's' || next == 'S') &&
                  hasFlag(flags, Flags::Syntax_ShorthandClasses)) ||
                 ((next == 'b' || next == 'B') &&
                  hasFlag(flags, Flags::Syntax_WordBoundary)) ||
                 ((next == 'A' || next == 'z' || next == 'Z') &&
                  hasFlag(flags, Flags::Syntax_BufferAnchors)) ||
                 (next == 'C' &&
                  hasFlag(flags, Flags::Syntax_SingleByteEscape)) ||
                 (next == 'g' && hasFlag(flags, Flags::Syntax_GBackref)) ||
                 (next == 'k' && hasFlag(flags, Flags::Syntax_KBackref)) ||
                 ((next >= '1' && next <= '9') &&
                  hasFlag(flags, Flags::Syntax_NumericBackref)) ||
                 isXBrace);
            bool isLiteralEscape = !isNonLiteral;

            if (isLiteralEscape) {
              auto savedPos = pos;
              advance(); // skip backslash
              char escaped = parseEscapeChar();
              if (!result.valid) {
                delete litChunks;
                return kNoNode;
              }

              // Check if quantifier follows
              bool qFollows = !atEnd() &&
                  (peek() == '*' || peek() == '+' || peek() == '?' ||
                   peek() == '{');
              if (!qFollows) {
                if (!checkCaseInsensitiveByte(
                        static_cast<unsigned char>(escaped), flags)) {
                  delete litChunks;
                  return kNoNode;
                }
                // For case-insensitive: alpha escapes also become a
                // CharClass with both cases.
                if (hasFlag(flags, Flags::CaseInsensitive) &&
                    isAlpha(static_cast<unsigned char>(escaped))) {
                  NodeIdx flushed = flushLiteral();
                  if (flushed != kNoNode) {
                    addChild(flushed);
                  }
                  addChild(makeCaseInsensitiveLiteralNode(
                      static_cast<unsigned char>(escaped)));
                  continue;
                }
                litChunks->append(&escaped, 1);
                continue;
              }
              // Quantifier follows — backtrack and let parseQuantified handle
              // it.
              pos = savedPos;
            }
          }
          break;
        }

        default: {
          // Plain literal character: check if a quantifier follows.
          bool quantifierFollows = false;
          if (pos + 1 < pattern.size()) {
            char next = pattern[pos + 1];
            quantifierFollows =
                (next == '*' || next == '+' || next == '?' || next == '{');
          }
          if (!quantifierFollows) {
            if (!checkCaseInsensitiveByte(
                    static_cast<unsigned char>(c), flags)) {
              delete litChunks;
              return kNoNode;
            }
            litChunks->append(&c, 1);
            advance();
            continue;
          }
          break;
        }
      }

      // Flush accumulated literals before handling non-literal element
      NodeIdx flushed = flushLiteral();
      if (flushed != kNoNode) {
        addChild(flushed);
      }

      NodeIdx child = parseQuantified(flags);
      if (!result.valid) {
        delete litChunks;
        return kNoNode;
      }
      addChild(child);
    }

    // Flush any remaining accumulated literals
    NodeIdx flushed = flushLiteral();
    if (flushed != kNoNode) {
      addChild(flushed);
    }

    delete litChunks;

    if (childCount == 0) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      return result.addNode(empty);
    }

    if (childCount == 1) {
      return firstChild;
    }

    if (prevChild != kNoNode) {
      result.nodes[prevChild].next_sibling = kNoNode;
    }

    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_first = firstChild;
    seq.child_last = prevChild;
    return result.addNode(seq);
  }

  constexpr NodeIdx parseQuantified(Flags& flags) noexcept {
    NodeIdx atom = parseAtom(flags);
    if (!result.valid) {
      return kNoNode;
    }

    skipExtendedWhitespace(flags);
    if (atEnd()) {
      return atom;
    }

    char c = peek();
    int minR = 0, maxR = -1;
    bool hasQuantifier = false;

    if (c == '*') {
      advance();
      minR = 0;
      maxR = -1;
      hasQuantifier = true;
    } else if (c == '+') {
      advance();
      minR = 1;
      maxR = -1;
      hasQuantifier = true;
    } else if (c == '?') {
      advance();
      minR = 0;
      maxR = 1;
      hasQuantifier = true;
    } else if (c == '{') {
      auto saved = pos;
      advance();
      auto parsedCount = parseCount(flags);
      if (!result.valid) {
        return kNoNode;
      }
      if (parsedCount.first >= 0) {
        minR = parsedCount.first;
        maxR = parsedCount.second;
        hasQuantifier = true;
      } else {
        pos = saved;
      }
    }

    if (!hasQuantifier) {
      return atom;
    }

    auto repeat_mode = RepeatMode::Greedy;
    if (!atEnd() && peek() == '?' &&
        hasFlag(flags, Flags::Syntax_LazyQuantifier)) {
      advance();
      repeat_mode = RepeatMode::Lazy;
    } else if (
        !atEnd() && peek() == '+' &&
        hasFlag(flags, Flags::Syntax_PossessiveQuantifier)) {
      advance();
      repeat_mode = RepeatMode::Possessive;
    }

    // (?U) ungreedy mode (PCRE2 extension): swap Greedy ↔ Lazy. Possessive
    // (`*+`, `++`, …) is unchanged. Verified against PCRE2 (grep -P):
    //   (?U)a.*b   matches `aXXb`   (`*` lazy)
    //   (?U)a.*?b  matches `aXXbYYb` (`*?` greedy — the `?` undoes the flip)
    if (hasFlag(flags, Flags::Ungreedy)) {
      if (repeat_mode == RepeatMode::Greedy) {
        repeat_mode = RepeatMode::Lazy;
      } else if (repeat_mode == RepeatMode::Lazy) {
        repeat_mode = RepeatMode::Greedy;
      }
    }

    AstNode rep;
    rep.kind = NodeKind::Repeat;
    rep.min_repeat = minR;
    rep.max_repeat = maxR;
    rep.repeat_mode = repeat_mode;
    rep.child_first = atom;
    rep.child_last = atom;
    return result.addNode(rep);
  }

  struct CountPair {
    int first;
    int second;
  };

  constexpr CountPair parseCount(Flags flags) noexcept {
    skipExtendedWhitespace(flags);
    int n = parseNumber();
    if (n < 0) {
      return {-1, -1};
    }

    skipExtendedWhitespace(flags);
    if (tryConsume('}')) {
      return {n, n};
    }

    if (!tryConsume(',')) {
      return {-1, -1};
    }

    skipExtendedWhitespace(flags);
    if (tryConsume('}')) {
      return {n, -1};
    }

    int m = parseNumber();
    if (m < 0) {
      return {-1, -1};
    }

    skipExtendedWhitespace(flags);
    if (!tryConsume('}')) {
      return {-1, -1};
    }

    if (m < n) {
      error("Max repetition count less than min");
      return {-1, -1};
    }

    return {n, m};
  }

  constexpr int parseNumber() noexcept {
    if (atEnd() || !isDigit(peek())) {
      return -1;
    }
    int val = 0;
    while (!atEnd() && isDigit(peek())) {
      val = val * 10 + (advance() - '0');
    }
    return val;
  }

  // Returns true if `c` is a valid first character for a named group name:
  // ASCII letter or underscore (matches Perl/PCRE/Python conventions).
  static constexpr bool isNameStart(char c) noexcept {
    return isAlpha(c) || c == '_';
  }

  // Returns true if `c` is a valid continuation character for a named
  // group name: ASCII letter, digit, or underscore.
  static constexpr bool isNameCont(char c) noexcept {
    return isAlpha(c) || isDigit(c) || c == '_';
  }

  // Parse a group name terminated by `terminator`. Validates that the
  // first character is [a-zA-Z_] and subsequent characters are
  // [a-zA-Z0-9_]. The name length must be ≤ NamedGroupEntry::kMaxNameLen.
  // On success, returns a string_view into the pattern buffer pointing at
  // the name (excluding the terminator) and consumes through but not
  // including the terminator. On failure, sets an error and returns an
  // empty view.
  constexpr std::string_view parseGroupName(char terminator) noexcept {
    auto start = pos;
    if (atEnd() || !isNameStart(peek())) {
      error("Invalid or empty named group name");
      return {};
    }
    advance();
    while (!atEnd() && peek() != terminator) {
      if (!isNameCont(peek())) {
        error("Invalid character in named group name");
        return {};
      }
      advance();
    }
    if (atEnd()) {
      error("Unterminated named group name");
      return {};
    }
    auto len = pos - start;
    if (len > static_cast<std::size_t>(NamedGroupEntry::kMaxNameLen)) {
      error("Named group name too long");
      return {};
    }
    return std::string_view(pattern.data() + start, len);
  }

  constexpr NodeIdx parseAtom(Flags& flags) noexcept {
    if (atEnd()) {
      error("Unexpected end of pattern");
      return kNoNode;
    }

    char c = peek();

    if (c == '(') {
      return parseGroup(flags);
    }

    if (c == '[') {
      return parseCharClass(flags);
    }

    if (c == '.') {
      advance();
      AstNode node;
      if (hasFlag(flags, Flags::DotAll)) {
        node.kind = NodeKind::AnyByte;
      } else {
        // Non-DotAll: dot matches any byte except '\n'. Build a CharClass
        // with the existing makeDotRanges() helper so the optimizer and
        // executor see a CharClass directly.
        auto rs = makeDotRanges();
        int ccIdx = result.addCharClass(rs);
        node.kind = NodeKind::CharClass;
        node.char_class_index = ccIdx;
      }
      return result.addNode(node);
    }

    if (c == '^') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = hasFlag(flags, Flags::Multiline)
          ? AnchorKind::BeginLine
          : AnchorKind::Begin;
      return result.addNode(node);
    }

    if (c == '$') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = hasFlag(flags, Flags::Multiline)
          ? AnchorKind::EndLine
          : AnchorKind::End;
      return result.addNode(node);
    }

    if (c == '\\') {
      return parseEscape(flags);
    }

    if (c == '*' || c == '+' || c == '?') {
      error("Quantifier without preceding element");
      return kNoNode;
    }

    if (c == ')') {
      error("Unmatched ')'");
      return kNoNode;
    }

    advance();
    if (!checkCaseInsensitiveByte(static_cast<unsigned char>(c), flags)) {
      return kNoNode;
    }
    if (hasFlag(flags, Flags::CaseInsensitive) &&
        isAlpha(static_cast<unsigned char>(c))) {
      return makeCaseInsensitiveLiteralNode(static_cast<unsigned char>(c));
    }
    AstNode node;
    node.kind = NodeKind::Literal;
    node.literal = result.appendLiteralChar(c);
    return result.addNode(node);
  }

  // Try to parse an inline-flag specification at the current position.
  // Returns true if any change was made (caller should re-check pos vs
  // result). On success: setBits / clearBits hold the flag bits to OR in
  // and AND-NOT respectively, and `scoped` is true iff the spec ended at
  // a colon (scoped flag group). On failure (no flag chars matched),
  // returns false and pos is unchanged.
  struct InlineFlagSpec {
    Flags setBits = Flags::None;
    Flags clearBits = Flags::None;
    bool scoped = false; // true: '(?ims:...)' ; false: '(?ims)' directive
    bool valid = false; // false if no flag chars matched
  };

  // Parse [imsUnx]*(-[imsUnx]*)?(:|)) starting AT position `pos`. On a
  // malformed spec (e.g. (?q)), returns valid=false and pos unchanged.
  constexpr InlineFlagSpec parseInlineFlagSpec() noexcept {
    InlineFlagSpec spec;
    auto saved = pos;
    auto applyChar = [&spec](char ch, bool clearing) {
      Flags bit = Flags::None;
      switch (ch) {
        case 'i':
          bit = Flags::CaseInsensitive;
          break;
        case 'm':
          bit = Flags::Multiline;
          break;
        case 's':
          bit = Flags::DotAll;
          break;
        case 'U':
          bit = Flags::Ungreedy;
          break;
        case 'n':
          bit = Flags::NoAutoCapture;
          break;
        case 'x':
          bit = Flags::Extended;
          break;
        default:
          return false;
      }
      if (clearing) {
        spec.clearBits = spec.clearBits | bit;
      } else {
        spec.setBits = spec.setBits | bit;
      }
      return true;
    };
    auto isFlagChar = [](char ch) {
      return ch == 'i' || ch == 'm' || ch == 's' || ch == 'U' || ch == 'n' ||
          ch == 'x';
    };
    bool sawAnyFlag = false;
    while (!atEnd()) {
      char ch = peek();
      if (isFlagChar(ch)) {
        applyChar(ch, false);
        advance();
        sawAnyFlag = true;
      } else {
        break;
      }
    }
    if (!atEnd() && peek() == '-') {
      advance();
      bool sawAnyClear = false;
      while (!atEnd()) {
        char ch = peek();
        if (isFlagChar(ch)) {
          applyChar(ch, true);
          advance();
          sawAnyClear = true;
        } else {
          break;
        }
      }
      if (!sawAnyClear) {
        // '-' with no flags after — malformed; back out completely.
        pos = saved;
        return spec;
      }
      sawAnyFlag = true;
    }
    if (!sawAnyFlag) {
      pos = saved;
      return spec;
    }
    if (atEnd()) {
      pos = saved;
      return spec;
    }
    if (peek() == ':') {
      spec.scoped = true;
      advance();
      spec.valid = true;
      return spec;
    }
    if (peek() == ')') {
      spec.scoped = false;
      advance();
      spec.valid = true;
      return spec;
    }
    // Unexpected char after flag spec; not a flag group.
    pos = saved;
    return spec;
  }

  // -----------------------------------------------------------------------
  // (?~...) serialized annotation constructs. Only recognized when
  // Syntax_InternalSerialized is set. Called from parseGroup after '(?'
  // is consumed and '~' is peeked.
  // -----------------------------------------------------------------------
  constexpr NodeIdx parseSerializedAnnotation(Flags& flags) noexcept {
    pos += 2; // consume '?~'
    if (atEnd()) {
      error("Unexpected end after (?~");
      return kNoNode;
    }
    char type = advance(); // consume type character
    switch (type) {
      case 'g':
        return parseAnnotationGroup(flags);
      case 'p':
        return parseAnnotationPossessiveProbed(flags);
      case '=':
        return parseAnnotationLookaround(flags, NodeKind::Lookahead);
      case '!':
        return parseAnnotationLookaround(flags, NodeKind::NegLookahead);
      case 'b':
        return parseAnnotationLookbehind(flags, NodeKind::Lookbehind);
      case 'B':
        return parseAnnotationLookbehind(flags, NodeKind::NegLookbehind);
      case 'D':
        return parseAnnotationDiscriminator(flags);
      default:
        error("Unknown (?~...) annotation type");
        return kNoNode;
    }
  }

  // (?~gN:...) or (?~gN<name>:...) — explicit group ID
  constexpr NodeIdx parseAnnotationGroup(Flags& flags) noexcept {
    int groupId = parseNumber();
    if (groupId < 0) {
      error("Expected group ID after (?~g");
      return kNoNode;
    }
    // Optional <name>
    std::string_view name;
    if (!atEnd() && peek() == '<') {
      advance(); // consume '<'
      name = parseGroupName('>');
      if (!result.valid) {
        return kNoNode;
      }
      advance(); // consume '>'
    }
    if (!tryConsume(':')) {
      error("Expected ':' after (?~gN or (?~gN<name>");
      return kNoNode;
    }
    // Track group count
    if (groupId > result.group_count) {
      result.group_count = groupId;
    }
    if (!name.empty()) {
      result.addNamedGroup(name, groupId);
    }
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }
    if (!tryConsume(')')) {
      error("Unmatched '(' in (?~g...)");
      return kNoNode;
    }
    AstNode node;
    node.kind = NodeKind::Group;
    node.capturing = true;
    node.group_id = groupId;
    node.child_first = inner;
    node.child_last = inner;
    return result.addNode(node);
  }

  // (?~pN:...) — PossessiveProbed wrapper with probe ID
  constexpr NodeIdx parseAnnotationPossessiveProbed(Flags& flags) noexcept {
    int probeId = parseNumber();
    if (probeId < 0) {
      error("Expected probe ID after (?~p");
      return kNoNode;
    }
    if (!tryConsume(':')) {
      error("Expected ':' after (?~pN");
      return kNoNode;
    }
    // Track probe count
    if (probeId + 1 > result.probe_count) {
      result.probe_count = probeId + 1;
    }
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }
    if (!tryConsume(')')) {
      error("Unmatched '(' in (?~p...)");
      return kNoNode;
    }
    // Find the outermost Repeat node in the parsed content and set
    // PossessiveProbed mode and probe_id on it.
    int target = inner;
    // Unwrap non-capturing groups to find the Repeat.
    while (target >= 0 && result.nodes[target].kind == NodeKind::Group &&
           !result.nodes[target].capturing) {
      target = result.nodes[target].child_first;
    }
    if (target >= 0 && result.nodes[target].kind == NodeKind::Repeat) {
      result.nodes[target].repeat_mode = RepeatMode::PossessiveProbed;
      result.nodes[target].probe_id = probeId;
    } else {
      error("(?~pN:...) content must contain a quantified expression");
      return kNoNode;
    }
    return inner;
  }

  // (?~=N:...) or (?~!N:...) — Lookahead/NegLookahead with probe ID
  constexpr NodeIdx parseAnnotationLookaround(
      Flags& flags, NodeKind kind) noexcept {
    int probeId = parseNumber();
    if (probeId < 0) {
      error("Expected probe ID after (?~= or (?~!");
      return kNoNode;
    }
    if (!tryConsume(':')) {
      error("Expected ':' after probe ID in lookaround annotation");
      return kNoNode;
    }
    // Track probe count
    if (probeId + 1 > result.probe_count) {
      result.probe_count = probeId + 1;
    }
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }
    if (!tryConsume(')')) {
      error("Unmatched '(' in lookaround annotation");
      return kNoNode;
    }
    AstNode node;
    node.kind = kind;
    node.probe_id = probeId;
    node.child_first = inner;
    node.child_last = inner;
    return result.addNode(node);
  }

  // (?~bN:...) or (?~bN,W:...) or (?~BN:...) or (?~BN,W:...)
  // Lookbehind/NegLookbehind with probe ID and optional lookbehind_width
  constexpr NodeIdx parseAnnotationLookbehind(
      Flags& flags, NodeKind kind) noexcept {
    if (kind == NodeKind::Lookbehind || kind == NodeKind::NegLookbehind) {
      result.has_lookbehind = true;
    }
    int probeId = parseNumber();
    if (probeId < 0) {
      error("Expected probe ID after (?~b or (?~B");
      return kNoNode;
    }
    int lookbehindWidth = -1;
    if (!atEnd() && peek() == ',') {
      advance(); // consume ','
      lookbehindWidth = parseNumber();
      if (lookbehindWidth < 0) {
        error("Expected width after ',' in lookbehind annotation");
        return kNoNode;
      }
    }
    if (!tryConsume(':')) {
      error("Expected ':' after lookbehind annotation header");
      return kNoNode;
    }
    // Track probe count
    if (probeId + 1 > result.probe_count) {
      result.probe_count = probeId + 1;
    }
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }
    if (!tryConsume(')')) {
      error("Unmatched '(' in lookbehind annotation");
      return kNoNode;
    }
    AstNode node;
    node.kind = kind;
    node.probe_id = probeId;
    node.child_first = inner;
    node.child_last = inner;
    // lookbehind_width: if explicit, use it; otherwise compute from inner
    if (lookbehindWidth >= 0) {
      node.min_repeat = lookbehindWidth;
    } else {
      node.min_repeat = detail::computeFixedWidth(result, inner);
    }
    return result.addNode(node);
  }

  // (?~DN:...) — Discriminator offset annotation
  constexpr NodeIdx parseAnnotationDiscriminator(Flags& flags) noexcept {
    int discOffset = parseNumber();
    if (discOffset < 0) {
      error("Expected discriminator offset after (?~D");
      return kNoNode;
    }
    if (!tryConsume(':')) {
      error("Expected ':' after (?~DN");
      return kNoNode;
    }
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }
    if (!tryConsume(')')) {
      error("Unmatched '(' in (?~D...)");
      return kNoNode;
    }
    // Set discriminator_offset on the content if it's an Alternation.
    if (inner >= 0 && result.nodes[inner].kind == NodeKind::Alternation) {
      result.nodes[inner].discriminator_offset = discOffset;
    }
    return inner;
  }

  constexpr NodeIdx parseGroup(Flags& flags) noexcept {
    advance(); // consume '('

    bool capturing = true;
    NodeKind lookaroundKind = NodeKind::Empty;

    if (pos < pattern.size() && pattern[pos] == '?') {
      if (pos + 1 < pattern.size()) {
        char next = pattern[pos + 1];
        if (next == '#') {
          // (?#...) comment — body is opaque text terminated by ')'.
          // No escape processing per Perl/PCRE2 verified behavior.
          // Returns Empty so the comment shows up as a no-op child of
          // the parent sequence; the optimizer removes Empty nodes.
          pos += 2; // consume '?#'
          while (!atEnd() && peek() != ')') {
            advance();
          }
          if (atEnd()) {
            error("Unterminated (?#...) comment");
            return kNoNode;
          }
          advance(); // consume ')'
          AstNode empty;
          empty.kind = NodeKind::Empty;
          return result.addNode(empty);
        }
        if (next == '~' && hasFlag(flags, Flags::Syntax_InternalSerialized)) {
          return parseSerializedAnnotation(flags);
        }
        if (next == ':') {
          capturing = false;
          pos += 2;
        } else if (next == '>') {
          // Atomic group (?>...) — desugar to possessive {1,1} repeat.
          if (!hasFlag(flags, Flags::Syntax_AtomicGroup)) {
            errorSyntax("Atomic groups not enabled in current syntax");
            return kNoNode;
          }
          // Atomic group is its own scope: parse with a copy of flags so
          // inner (?i) directives don't leak out.
          pos += 2;
          Flags innerFlags = flags;
          NodeIdx inner = parseRegex(innerFlags);
          if (!result.valid) {
            return kNoNode;
          }
          if (!tryConsume(')')) {
            error("Unmatched '('");
            return kNoNode;
          }
          // Wrap inner in a non-capturing Group
          AstNode grp;
          grp.kind = NodeKind::Group;
          grp.capturing = false;
          grp.child_first = inner;
          grp.child_last = inner;
          NodeIdx grpIdx = result.addNode(grp);

          // Wrap in possessive Repeat{1,1}
          AstNode rep;
          rep.kind = NodeKind::Repeat;
          rep.min_repeat = 1;
          rep.max_repeat = 1;
          rep.repeat_mode = RepeatMode::Possessive;
          rep.child_first = grpIdx;
          rep.child_last = grpIdx;
          return result.addNode(rep);
        } else if (next == '=') {
          if (!hasFlag(flags, Flags::Syntax_Lookaround)) {
            errorSyntax("Lookaround not enabled in current syntax");
            return kNoNode;
          }
          lookaroundKind = NodeKind::Lookahead;
          pos += 2;
        } else if (next == '!') {
          if (!hasFlag(flags, Flags::Syntax_Lookaround)) {
            errorSyntax("Lookaround not enabled in current syntax");
            return kNoNode;
          }
          lookaroundKind = NodeKind::NegLookahead;
          pos += 2;
        } else if (
            next == '<' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '=') {
          if (!hasFlag(flags, Flags::Syntax_Lookaround)) {
            errorSyntax("Lookaround not enabled in current syntax");
            return kNoNode;
          }
          lookaroundKind = NodeKind::Lookbehind;
          pos += 3;
        } else if (
            next == '<' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '!') {
          if (!hasFlag(flags, Flags::Syntax_Lookaround)) {
            errorSyntax("Lookaround not enabled in current syntax");
            return kNoNode;
          }
          lookaroundKind = NodeKind::NegLookbehind;
          pos += 3;
        } else if (next == '<' && pos + 2 < pattern.size()) {
          // (?<name>...) — PCRE/Perl/ECMAScript named group syntax.
          // Distinguished from (?<= and (?<! by the third character
          // (which must be a valid name-start character).
          if (!hasFlag(flags, Flags::Syntax_NamedGroupAngle)) {
            errorSyntax(
                "(?<name>...) named groups not enabled in current syntax");
            return kNoNode;
          }
          pos += 2; // consume '?<'
          std::string_view name = parseGroupName('>');
          if (!result.valid) {
            return kNoNode;
          }
          if (result.hasNamedGroup(name)) {
            error("Duplicate named group");
            return kNoNode;
          }
          advance(); // consume '>'
          int groupId = ++result.group_count;
          result.addNamedGroup(name, groupId);
          // Named groups create their own scope just like regular
          // capturing groups: inner (?i) doesn't leak back.
          Flags innerFlags = flags;
          NodeIdx inner = parseRegex(innerFlags);
          if (!result.valid) {
            return kNoNode;
          }
          if (!tryConsume(')')) {
            error("Unmatched '('");
            return kNoNode;
          }
          AstNode node;
          node.kind = NodeKind::Group;
          node.capturing = true;
          node.group_id = groupId;
          node.child_first = inner;
          node.child_last = inner;
          return result.addNode(node);
        } else if (
            next == 'P' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '<') {
          // (?P<name>...) — Python named group syntax.
          if (!hasFlag(flags, Flags::Syntax_NamedGroupPython)) {
            errorSyntax(
                "(?P<name>...) named groups not enabled in current syntax");
            return kNoNode;
          }
          pos += 3; // consume '?P<'
          std::string_view name = parseGroupName('>');
          if (!result.valid) {
            return kNoNode;
          }
          if (result.hasNamedGroup(name)) {
            error("Duplicate named group");
            return kNoNode;
          }
          advance(); // consume '>'
          int groupId = ++result.group_count;
          result.addNamedGroup(name, groupId);
          Flags innerFlags = flags;
          NodeIdx inner = parseRegex(innerFlags);
          if (!result.valid) {
            return kNoNode;
          }
          if (!tryConsume(')')) {
            error("Unmatched '('");
            return kNoNode;
          }
          AstNode node;
          node.kind = NodeKind::Group;
          node.capturing = true;
          node.group_id = groupId;
          node.child_first = inner;
          node.child_last = inner;
          return result.addNode(node);
        } else if (
            next == 'P' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '=') {
          // (?P=name) — Python named backreference syntax. Resolves to
          // the same Backref / CaseInsensitiveBackref node that
          // \k<name> produces.
          if (!hasFlag(flags, Flags::Syntax_NamedGroupPython)) {
            errorSyntax("(?P=name) backrefs not enabled in current syntax");
            return kNoNode;
          }
          pos += 3; // consume '?P='
          std::string_view name = parseGroupName(')');
          if (!result.valid) {
            return kNoNode;
          }
          int groupId = result.findNamedGroup(name);
          if (groupId < 0) {
            error("Backreference to undefined named group");
            return kNoNode;
          }
          advance(); // consume ')'
          result.nfa_compatible = false;
          result.has_backref = true;
          AstNode node;
          node.kind = hasFlag(flags, Flags::CaseInsensitive)
              ? NodeKind::CaseInsensitiveBackref
              : NodeKind::Backref;
          node.group_id = groupId;
          return result.addNode(node);
        } else {
          // Try inline flag syntax: (?i), (?-i), (?im), (?i-m), (?i:...)
          auto savedFlagPos = pos;
          pos += 1; // consume '?'
          InlineFlagSpec spec = parseInlineFlagSpec();
          if (spec.valid && !hasFlag(flags, Flags::Syntax_InlineFlags)) {
            errorSyntax("Inline flags not enabled in current syntax");
            return kNoNode;
          }
          if (spec.valid) {
            Flags newFlags = (flags | spec.setBits) & ~spec.clearBits;
            if (spec.scoped) {
              // Scoped flag group: (?ims:...) — non-capturing group with
              // modified flags. Outer flags are NOT modified; the new
              // flags are the inner scope's initial state.
              NodeIdx inner = parseRegex(newFlags);
              if (!result.valid) {
                return kNoNode;
              }
              if (!tryConsume(')')) {
                error("Unmatched '('");
                return kNoNode;
              }
              return inner;
            } else {
              // Flag directive: (?ims) — modifies the calling sequence's
              // flags. parseInlineFlagSpec already consumed the trailing
              // ')'. Returns an Empty node so the directive shows up as
              // a no-op child of the parent sequence; the optimizer will
              // remove it.
              flags = newFlags;
              AstNode empty;
              empty.kind = NodeKind::Empty;
              return result.addNode(empty);
            }
          }
          // Not a flag spec — restore position to start parsing an unknown
          // (?-prefixed group, which will likely produce a parse error
          // downstream.
          pos = savedFlagPos;
        }
      }
    }

    if (lookaroundKind != NodeKind::Empty) {
      if (lookaroundKind == NodeKind::Lookbehind ||
          lookaroundKind == NodeKind::NegLookbehind) {
        result.has_lookbehind = true;
      }

      // Lookarounds get their own scope: pass a copy of flags.
      Flags innerFlags = flags;
      NodeIdx inner = parseRegex(innerFlags);
      if (!result.valid) {
        return kNoNode;
      }

      if (!tryConsume(')')) {
        error("Unmatched '('");
        return kNoNode;
      }

      AstNode node;
      node.kind = lookaroundKind;
      node.child_first = inner;
      node.child_last = inner;

      if (lookaroundKind == NodeKind::Lookbehind ||
          lookaroundKind == NodeKind::NegLookbehind) {
        // Compute fixed width if possible. If fixed-width, store it
        // for the fast-path direct comparison (no probe needed).
        // If variable-width, store -1 — the reverse probe path handles it.
        int width = detail::computeFixedWidth(result, inner);
        node.min_repeat = width; // -1 for variable-width
      }

      return result.addNode(node);
    }

    int groupId = 0;
    // (?n) no-auto-capture (PCRE2/.NET extension): plain `(...)` becomes
    // non-capturing. Named groups bypass this — they're handled in their
    // own branches above and never reach this point. Verified against
    // Perl: (?n)(\w+) yields no $1 capture.
    if (capturing && hasFlag(flags, Flags::NoAutoCapture)) {
      capturing = false;
    }
    if (capturing) {
      groupId = ++result.group_count;
    }

    // Regular group ((...) or (?:...)) — its own scope: pass a copy of
    // flags so inner (?i) doesn't leak back.
    Flags innerFlags = flags;
    NodeIdx inner = parseRegex(innerFlags);
    if (!result.valid) {
      return kNoNode;
    }

    if (!tryConsume(')')) {
      error("Unmatched '('");
      return kNoNode;
    }

    if (!capturing) {
      return inner;
    }

    AstNode node;
    node.kind = NodeKind::Group;
    node.capturing = true; // always true if we reach here
    node.group_id = groupId;
    node.child_first = inner;
    node.child_last = inner;
    return result.addNode(node);
  }

  constexpr NodeIdx parseCharClass(Flags flags) noexcept {
    advance(); // consume '['

    bool negated = tryConsume('^');
    CharRangeSet rs;
    bool caseInsensitive = hasFlag(flags, Flags::CaseInsensitive);

    // Inline helper: add a single char (with case expansion if active).
    auto addCharWithCI = [&](unsigned char c) {
      rs.addChar(c);
      if (caseInsensitive && isAlpha(c)) {
        rs.addChar(static_cast<unsigned char>(c ^ 0x20));
      }
    };

    // Inline helper: add a range, expanding the alpha overlap if active.
    auto addRangeWithCI = [&](unsigned char lo, unsigned char hi) {
      rs.addRange(lo, hi);
      if (caseInsensitive) {
        // Overlap with [A-Z] (65-90): add lowercase equivalent.
        if (lo <= 'Z' && hi >= 'A') {
          unsigned char a = lo > 'A' ? lo : 'A';
          unsigned char b = hi < 'Z' ? hi : 'Z';
          rs.addRange(
              static_cast<unsigned char>(a | 0x20),
              static_cast<unsigned char>(b | 0x20));
        }
        // Overlap with [a-z] (97-122): add uppercase equivalent.
        if (lo <= 'z' && hi >= 'a') {
          unsigned char a = lo > 'a' ? lo : 'a';
          unsigned char b = hi < 'z' ? hi : 'z';
          rs.addRange(
              static_cast<unsigned char>(a & ~0x20),
              static_cast<unsigned char>(b & ~0x20));
        }
      }
    };

    bool firstChar = true;
    while (!atEnd() && (firstChar || peek() != ']')) {
      if (peek() == '-' && firstChar) {
        rs.addChar('-');
        advance();
        firstChar = false;
        continue;
      }

      // POSIX character class: [:name:] or [:^name:]
      if (peek() == '[' && pos + 1 < pattern.size() &&
          pattern[pos + 1] == ':') {
        // Scan forward to find closing :]
        auto colonPos = pos + 2;
        bool negatedPosix = false;
        if (colonPos < pattern.size() && pattern[colonPos] == '^') {
          negatedPosix = true;
          ++colonPos;
        }
        auto nameStart = colonPos;
        while (colonPos < pattern.size() && pattern[colonPos] != ':' &&
               pattern[colonPos] != ']') {
          ++colonPos;
        }
        if (colonPos + 1 < pattern.size() && pattern[colonPos] == ':' &&
            pattern[colonPos + 1] == ']' &&
            hasFlag(flags, Flags::Syntax_PosixCharClass)) {
          // Valid [:name:] syntax
          std::string_view className(
              pattern.data() + nameStart, colonPos - nameStart);
          if (negatedPosix) {
            CharRangeSet tmp;
            if (!addPosixClass(tmp, className, caseInsensitive)) {
              error("Unknown POSIX class name");
              return kNoNode;
            }
            tmp.invert();
            rs.merge(tmp);
          } else {
            if (!addPosixClass(rs, className, caseInsensitive)) {
              error("Unknown POSIX class name");
              return kNoNode;
            }
          }
          pos = colonPos + 2; // skip past :]
          firstChar = false;
          continue;
        }
        // No closing :] found — treat [: as literal characters, fall through
      }

      if (peek() == '\\' && pos + 1 < pattern.size()) {
        char next = pattern[pos + 1];
        if ((next == 'w' || next == 'W') &&
            hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
          advance();
          advance();
          auto shorthand = makeWordRanges();
          if (next == 'W') {
            shorthand.invert();
          }
          rs.merge(shorthand);
          firstChar = false;
          continue;
        }
        if ((next == 'd' || next == 'D') &&
            hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
          advance();
          advance();
          auto shorthand = makeDigitRanges();
          if (next == 'D') {
            shorthand.invert();
          }
          rs.merge(shorthand);
          firstChar = false;
          continue;
        }
        if ((next == 's' || next == 'S') &&
            hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
          advance();
          advance();
          auto shorthand = makeSpaceRanges();
          if (next == 'S') {
            shorthand.invert();
          }
          rs.merge(shorthand);
          firstChar = false;
          continue;
        }
        if (next == 'p' || next == 'P') {
          error(
              "Unicode property escapes (\\p{...}/\\P{...}) are not supported");
          return kNoNode;
        }
      }

      char lo = parseClassChar();
      if (!result.valid) {
        return kNoNode;
      }
      if (!checkCaseInsensitiveByte(static_cast<unsigned char>(lo), flags)) {
        return kNoNode;
      }

      if (!atEnd() && peek() == '-' && pos + 1 < pattern.size() &&
          pattern[pos + 1] != ']') {
        advance(); // consume '-'
        char hi = parseClassChar();
        if (!result.valid) {
          return kNoNode;
        }
        if (!checkCaseInsensitiveByte(static_cast<unsigned char>(hi), flags)) {
          return kNoNode;
        }
        if (static_cast<unsigned char>(lo) > static_cast<unsigned char>(hi)) {
          error("Invalid character range in class");
          return kNoNode;
        }
        addRangeWithCI(
            static_cast<unsigned char>(lo), static_cast<unsigned char>(hi));
      } else {
        addCharWithCI(static_cast<unsigned char>(lo));
      }
      firstChar = false;
    }

    if (atEnd()) {
      error("Unmatched '['");
      return kNoNode;
    }

    if (peek() == '-') {
      rs.addChar('-');
      advance();
    }

    advance(); // consume ']'

    if (negated) {
      rs.invert();
    }

    int ccIdx = result.addCharClass(rs);
    AstNode node;
    node.kind = NodeKind::CharClass;
    node.char_class_index = ccIdx;
    return result.addNode(node);
  }

  constexpr char parseClassChar() noexcept {
    if (atEnd()) {
      error("Unexpected end in character class");
      return '\0';
    }
    if (peek() == '\\') {
      advance();
      return parseEscapeChar();
    }
    return advance();
  }

  constexpr NodeIdx parseEscape(Flags flags) noexcept {
    advance(); // consume '\'
    if (atEnd()) {
      error("Trailing backslash");
      return kNoNode;
    }

    char c = peek();

    // \x{N} — codepoint up to 0x10FFFF. Outside a character class we can
    // emit the UTF-8 byte sequence as a Literal node (folly is a byte
    // engine; user input is expected to be UTF-8 encoded). Single-byte
    // values (≤ 0x7F) emit as a one-byte Literal. Multi-byte values
    // (0x80..0x10FFFF) emit as a 2-4 byte Literal.
    if (c == 'x' && pos + 1 < pattern.size() && pattern[pos + 1] == '{' &&
        hasFlag(flags, Flags::Syntax_XBraceHex)) {
      pos += 2; // consume 'x' and '{'
      uint32_t val = 0;
      int digits = 0;
      while (!atEnd() && peek() != '}') {
        int d = hexDigitValue(peek());
        if (d < 0) {
          error("Invalid hex digit in \\x{...}");
          return kNoNode;
        }
        val = val * 16 + static_cast<uint32_t>(d);
        if (val > 0x10FFFF) {
          error("Codepoint exceeds maximum Unicode value U+10FFFF");
          return kNoNode;
        }
        advance();
        ++digits;
      }
      if (atEnd() || peek() != '}') {
        error("Unclosed \\x{...}");
        return kNoNode;
      }
      advance(); // consume '}'
      if (digits == 0) {
        error("Empty \\x{} escape");
        return kNoNode;
      }
      // Case-insensitive folding is undefined beyond ASCII — error per
      // the existing CaseInsensitive byte-restriction rule.
      if (val > 0x7F && hasFlag(flags, Flags::CaseInsensitive)) {
        error("Non-ASCII byte in case-insensitive pattern");
        return kNoNode;
      }
      char buf[4];
      int n = encodeUtf8(val, buf);
      if (n == 1 && hasFlag(flags, Flags::CaseInsensitive) &&
          isAlpha(static_cast<unsigned char>(buf[0]))) {
        return makeCaseInsensitiveLiteralNode(
            static_cast<unsigned char>(buf[0]));
      }
      AstNode node;
      node.kind = NodeKind::Literal;
      node.literal = result.appendLiteral(buf, n);
      return result.addNode(node);
    }

    // Backreferences \N — read all consecutive digits as the group
    // number. Per the design decision, folly always treats \NN as a
    // backref (no Perl-style fallback to octal). Users wanting literal
    // octal bytes must use \0NN explicitly.
    if (c >= '1' && c <= '9' && hasFlag(flags, Flags::Syntax_NumericBackref)) {
      int groupNum = 0;
      while (!atEnd() && peek() >= '0' && peek() <= '9') {
        groupNum = groupNum * 10 + (advance() - '0');
      }
      if (groupNum > result.group_count) {
        error("Backreference to nonexistent group");
        return kNoNode;
      }
      result.nfa_compatible = false;
      result.has_backref = true;
      AstNode node;
      // Per Perl/PCRE2/Python semantics: case-sensitivity of a backref
      // is determined by the flag state at the BACKREF SITE, not the
      // capture site. Use a distinct NodeKind so the executor can dispatch
      // at compile time without inspecting a runtime flag.
      node.kind = hasFlag(flags, Flags::CaseInsensitive)
          ? NodeKind::CaseInsensitiveBackref
          : NodeKind::Backref;
      node.group_id = groupNum;
      return result.addNode(node);
    }

    // \g forms: \gN, \g{N}, \g{-N} (relative), \g{name} (named).
    // Resolves to the same Backref/CaseInsensitiveBackref node kind that
    // numeric \N and \k<name> produce.
    if (c == 'g' && hasFlag(flags, Flags::Syntax_GBackref)) {
      advance(); // consume 'g'
      bool braced = !atEnd() && peek() == '{';
      if (braced) {
        advance(); // consume '{'
        // Named form: \g{name}. Distinguished from numeric by the first
        // character — name-start (alpha or '_') means named; '-' or
        // digit means numeric. Routes through the same name-resolution
        // path as \k<name> and (?P=name).
        if (!atEnd() && isNameStart(peek())) {
          std::string_view name = parseGroupName('}');
          if (!result.valid) {
            return kNoNode;
          }
          int groupId = result.findNamedGroup(name);
          if (groupId < 0) {
            error("Backreference to undefined named group");
            return kNoNode;
          }
          advance(); // consume '}'
          result.nfa_compatible = false;
          result.has_backref = true;
          AstNode node;
          node.kind = hasFlag(flags, Flags::CaseInsensitive)
              ? NodeKind::CaseInsensitiveBackref
              : NodeKind::Backref;
          node.group_id = groupId;
          return result.addNode(node);
        }
      }
      // Numeric forms: \gN, \g{N}, \g{-N}.
      bool negative = false;
      if (braced && !atEnd() && peek() == '-') {
        negative = true;
        advance();
      }
      if (atEnd() || peek() < '0' || peek() > '9') {
        error(
            braced ? "Expected name or digits in \\g{...}"
                   : "Expected digits after \\g");
        return kNoNode;
      }
      int val = 0;
      while (!atEnd() && peek() >= '0' && peek() <= '9') {
        val = val * 10 + (advance() - '0');
      }
      if (braced) {
        if (atEnd() || peek() != '}') {
          error("Unterminated \\g{...}");
          return kNoNode;
        }
        advance();
      }
      int groupNum = 0;
      if (negative) {
        if (val == 0) {
          error("\\g{-0} is invalid");
          return kNoNode;
        }
        groupNum = result.group_count - val + 1;
        if (groupNum < 1) {
          error("Relative backreference exceeds group count");
          return kNoNode;
        }
      } else {
        if (val == 0) {
          error("\\g0 / \\g{0} is invalid (no group 0)");
          return kNoNode;
        }
        groupNum = val;
      }
      if (groupNum > result.group_count) {
        error("Backreference to nonexistent group");
        return kNoNode;
      }
      result.nfa_compatible = false;
      result.has_backref = true;
      AstNode node;
      node.kind = hasFlag(flags, Flags::CaseInsensitive)
          ? NodeKind::CaseInsensitiveBackref
          : NodeKind::Backref;
      node.group_id = groupNum;
      return result.addNode(node);
    }

    // Named backreference \k<name> (Perl/PCRE syntax). Resolves the
    // name to its group_id at parse time and creates the same Backref
    // / CaseInsensitiveBackref node that numeric backrefs produce. The
    // referenced group must already be defined (no forward refs).
    if (c == 'k' && hasFlag(flags, Flags::Syntax_KBackref)) {
      advance(); // consume 'k'
      if (!tryConsume('<')) {
        error("Expected '<' after \\k");
        return kNoNode;
      }
      std::string_view name = parseGroupName('>');
      if (!result.valid) {
        return kNoNode;
      }
      int groupId = result.findNamedGroup(name);
      if (groupId < 0) {
        error("Backreference to undefined named group");
        return kNoNode;
      }
      advance(); // consume '>'
      result.nfa_compatible = false;
      result.has_backref = true;
      AstNode node;
      node.kind = hasFlag(flags, Flags::CaseInsensitive)
          ? NodeKind::CaseInsensitiveBackref
          : NodeKind::Backref;
      node.group_id = groupId;
      return result.addNode(node);
    }

    if ((c == 'd' || c == 'D') &&
        hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
      advance();
      auto rs = makeDigitRanges();
      if (c == 'D') {
        rs.invert();
      }
      int ccIdx = result.addCharClass(rs);
      AstNode node;
      node.kind = NodeKind::CharClass;
      node.char_class_index = ccIdx;
      return result.addNode(node);
    }

    if ((c == 'w' || c == 'W') &&
        hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
      advance();
      auto rs = makeWordRanges();
      if (c == 'W') {
        rs.invert();
      }
      int ccIdx = result.addCharClass(rs);
      AstNode node;
      node.kind = NodeKind::CharClass;
      node.char_class_index = ccIdx;
      return result.addNode(node);
    }

    if ((c == 's' || c == 'S') &&
        hasFlag(flags, Flags::Syntax_ShorthandClasses)) {
      advance();
      auto rs = makeSpaceRanges();
      if (c == 'S') {
        rs.invert();
      }
      int ccIdx = result.addCharClass(rs);
      AstNode node;
      node.kind = NodeKind::CharClass;
      node.char_class_index = ccIdx;
      return result.addNode(node);
    }

    if (c == 'b' && hasFlag(flags, Flags::Syntax_WordBoundary)) {
      advance();
      result.nfa_compatible = false;
      AstNode node;
      node.kind = NodeKind::WordBoundary;
      return result.addNode(node);
    }

    if (c == 'B' && hasFlag(flags, Flags::Syntax_WordBoundary)) {
      advance();
      result.nfa_compatible = false;
      AstNode node;
      node.kind = NodeKind::NegWordBoundary;
      return result.addNode(node);
    }

    if (c == 'A' && hasFlag(flags, Flags::Syntax_BufferAnchors)) {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::StartOfString;
      return result.addNode(node);
    }

    if (c == 'z' && hasFlag(flags, Flags::Syntax_BufferAnchors)) {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::EndOfString;
      return result.addNode(node);
    }

    if (c == 'Z' && hasFlag(flags, Flags::Syntax_BufferAnchors)) {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::EndOfStringOrNewline;
      return result.addNode(node);
    }

    if (c == 'C' && hasFlag(flags, Flags::Syntax_SingleByteEscape)) {
      advance();
      AstNode node;
      node.kind = NodeKind::AnyByte;
      return result.addNode(node);
    }

    if (c == 'p' || c == 'P') {
      error("Unicode property escapes (\\p{...}/\\P{...}) are not supported");
      return kNoNode;
    }

    char escaped = parseEscapeChar();
    if (!result.valid) {
      return kNoNode;
    }
    if (!checkCaseInsensitiveByte(static_cast<unsigned char>(escaped), flags)) {
      return kNoNode;
    }
    if (hasFlag(flags, Flags::CaseInsensitive) &&
        isAlpha(static_cast<unsigned char>(escaped))) {
      return makeCaseInsensitiveLiteralNode(
          static_cast<unsigned char>(escaped));
    }
    AstNode node;
    node.kind = NodeKind::Literal;
    node.literal = result.appendLiteralChar(escaped);
    return result.addNode(node);
  }

  static constexpr int hexDigitValue(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
      return 10 + ch - 'A';
    }
    return -1;
  }

  constexpr char parseEscapeChar() noexcept {
    if (atEnd()) {
      error("Trailing backslash");
      return '\0';
    }
    char c = advance();
    switch (c) {
      case 'n':
        return '\n';
      case 'r':
        return '\r';
      case 't':
        return '\t';
      case 'f':
        return '\f';
      case 'v':
        return '\v';
      case 'a':
        return '\a';
      case 'b':
        return '\b';
      case '0': {
        if (!atEnd() && peek() >= '0' && peek() <= '7') {
          int val = advance() - '0';
          if (!atEnd() && peek() >= '0' && peek() <= '7') {
            val = val * 8 + (advance() - '0');
          }
          return static_cast<char>(val);
        }
        return '\0';
      }
      case 'x': {
        if (!atEnd() && peek() == '{') {
          advance();
          int val = 0;
          int digits = 0;
          while (!atEnd() && peek() != '}') {
            int d = hexDigitValue(peek());
            if (d < 0) {
              error("Invalid hex digit in \\x{...}");
              return '\0';
            }
            val = val * 16 + d;
            if (val > 0xFF) {
              // Multi-byte values are valid in escape contexts that emit
              // UTF-8 byte sequences, but parseEscapeChar is used for
              // single-byte contexts (e.g., character classes). Routing
              // multi-byte through here would silently truncate; error
              // explicitly instead.
              error("Codepoint > 0xFF not allowed in this context");
              return '\0';
            }
            advance();
            ++digits;
          }
          if (atEnd() || peek() != '}') {
            error("Unclosed \\x{...}");
            return '\0';
          }
          advance();
          if (digits == 0) {
            error("Empty \\x{} escape");
            return '\0';
          }
          return static_cast<char>(val);
        }
        if (pos + 2 > pattern.size()) {
          error("Incomplete hex escape \\x");
          return '\0';
        }
        int h1 = hexDigitValue(advance());
        int h2 = hexDigitValue(advance());
        if (h1 < 0 || h2 < 0) {
          error("Invalid hex digit in \\xNN");
          return '\0';
        }
        return static_cast<char>(h1 * 16 + h2);
      }
      case '\\':
      case '.':
      case '*':
      case '+':
      case '?':
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '|':
      case '^':
      case '$':
      case '-':
      case '/':
        return c;
      default:
        return c;
    }
  }
};

template <std::size_t PatLen, ParseErrorMode Mode = ParseErrorMode::CompileTime>
constexpr auto parse(std::string_view pattern, Flags flags = Flags::None) {
  AstBuilder builder;
  builder.valid = true;
  Parser parser(pattern, builder);
  builder.root = parser.parseRegex(compilationFlags(flags));

  if (builder.valid && !parser.atEnd()) {
    parser.error("Unexpected character after pattern");
  }

  if constexpr (Mode == ParseErrorMode::CompileTime) {
    if (!builder.valid) {
      const char* msg = builder.error_message;
      folly::throw_exception<regex_parse_error>(msg, builder.error_pos);
    }
  }

  markUnreachableNodes(builder);

  return compact<ParseSizes{
      (PatLen < 4 ? 8 : PatLen * 2),
      (PatLen < 4 ? 16 : PatLen * 4),
      (PatLen < 4 ? 4 : PatLen),
      0,
      PatLen,
      0,
      // Each named group declaration is at least 7 chars: (?<a>x).
      // PatLen / 7 + 1 is a tight upper bound; round up to PatLen for
      // safety against shorter syntax variants and to avoid edge cases
      // when patterns are very small.
      PatLen}>(builder);
}

template <ParseErrorMode Mode = ParseErrorMode::CompileTime, std::size_t N>
constexpr auto parse(const char (&pattern)[N], Flags flags = Flags::None) {
  return parse<N - 1, Mode>(std::string_view(pattern, N - 1), flags);
}

} // namespace folly::regex::detail
