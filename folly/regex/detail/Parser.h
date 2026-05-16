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
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>

namespace folly {
namespace regex {
namespace detail {

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

  constexpr NodeIdx parseRegex() noexcept { return parseAlternation(); }

  constexpr NodeIdx parseAlternation() noexcept {
    NodeIdx first = parseSequence();
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
      NodeIdx next = parseSequence();
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

  constexpr NodeIdx parseSequence() noexcept {
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

    while (!atEnd() && peek() != '|' && peek() != ')') {
      char c = peek();

      // Fast path: plain literal character (not special)
      if (c != '(' && c != '[' && c != '.' && c != '^' && c != '$' &&
          c != '\\' && c != '*' && c != '+' && c != '?' && c != ')' &&
          c != '|') {
        // Check if a quantifier follows this character
        bool quantifierFollows = false;
        if (pos + 1 < pattern.size()) {
          char next = pattern[pos + 1];
          quantifierFollows =
              (next == '*' || next == '+' || next == '?' || next == '{');
        }
        if (!quantifierFollows) {
          litChunks->append(&c, 1);
          advance();
          continue;
        }
      }

      // Try to coalesce escaped literal characters
      if (c == '\\' && pos + 1 < pattern.size()) {
        char next = pattern[pos + 1];
        bool isLiteralEscape = !(
            next == 'd' || next == 'D' || next == 'w' || next == 'W' ||
            next == 's' || next == 'S' || next == 'b' || next == 'B' ||
            next == 'A' || next == 'z' || next == 'Z' || next == 'C' ||
            (next >= '1' && next <= '9'));

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
            litChunks->append(&escaped, 1);
            continue;
          }
          // Quantifier follows — backtrack and let parseQuantified handle it
          pos = savedPos;
        }
      }

      // Flush accumulated literals before handling non-literal element
      NodeIdx flushed = flushLiteral();
      if (flushed != kNoNode) {
        addChild(flushed);
      }

      NodeIdx child = parseQuantified();
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

  constexpr NodeIdx parseQuantified() noexcept {
    NodeIdx atom = parseAtom();
    if (!result.valid) {
      return kNoNode;
    }

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
      auto parsedCount = parseCount();
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
    if (!atEnd() && peek() == '?') {
      advance();
      repeat_mode = RepeatMode::Lazy;
    } else if (!atEnd() && peek() == '+') {
      advance();
      repeat_mode = RepeatMode::Possessive;
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

  constexpr CountPair parseCount() noexcept {
    int n = parseNumber();
    if (n < 0) {
      error("Expected number after '{'");
      return {-1, -1};
    }

    if (tryConsume('}')) {
      return {n, n};
    }

    if (!tryConsume(',')) {
      error("Expected ',' or '}' in counted repetition");
      return {-1, -1};
    }

    if (tryConsume('}')) {
      return {n, -1};
    }

    int m = parseNumber();
    if (m < 0) {
      error("Expected number or '}' after ','");
      return {-1, -1};
    }

    if (!tryConsume('}')) {
      error("Expected '}' after count");
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

  constexpr NodeIdx parseAtom() noexcept {
    if (atEnd()) {
      error("Unexpected end of pattern");
      return kNoNode;
    }

    char c = peek();

    if (c == '(') {
      return parseGroup();
    }

    if (c == '[') {
      return parseCharClass();
    }

    if (c == '.') {
      advance();
      AstNode node;
      node.kind = NodeKind::AnyChar;
      return result.addNode(node);
    }

    if (c == '^') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::Begin;
      return result.addNode(node);
    }

    if (c == '$') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::End;
      return result.addNode(node);
    }

    if (c == '\\') {
      return parseEscape();
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
    AstNode node;
    node.kind = NodeKind::Literal;
    node.literal = result.appendLiteralChar(c);
    return result.addNode(node);
  }

  constexpr NodeIdx parseGroup() noexcept {
    advance(); // consume '('

    bool capturing = true;
    NodeKind lookaroundKind = NodeKind::Empty;

    if (pos < pattern.size() && pattern[pos] == '?') {
      if (pos + 1 < pattern.size()) {
        char next = pattern[pos + 1];
        if (next == ':') {
          capturing = false;
          pos += 2;
        } else if (next == '>') {
          // Atomic group (?>...) — desugar to possessive {1,1} repeat
          pos += 2;
          NodeIdx inner = parseRegex();
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
          lookaroundKind = NodeKind::Lookahead;
          pos += 2;
        } else if (next == '!') {
          lookaroundKind = NodeKind::NegLookahead;
          pos += 2;
        } else if (
            next == '<' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '=') {
          lookaroundKind = NodeKind::Lookbehind;
          pos += 3;
        } else if (
            next == '<' && pos + 2 < pattern.size() &&
            pattern[pos + 2] == '!') {
          lookaroundKind = NodeKind::NegLookbehind;
          pos += 3;
        }
      }
    }

    if (lookaroundKind != NodeKind::Empty) {
      if (lookaroundKind == NodeKind::Lookbehind ||
          lookaroundKind == NodeKind::NegLookbehind) {
        result.has_lookbehind = true;
      }

      NodeIdx inner = parseRegex();
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
        int width = computeFixedWidth(inner);
        node.min_repeat = width; // -1 for variable-width
      }

      return result.addNode(node);
    }

    int groupId = 0;
    if (capturing) {
      groupId = ++result.group_count;
    }

    NodeIdx inner = parseRegex();
    if (!result.valid) {
      return kNoNode;
    }

    if (!tryConsume(')')) {
      error("Unmatched '('");
      return kNoNode;
    }

    AstNode node;
    node.kind = NodeKind::Group;
    node.capturing = capturing;
    node.group_id = groupId;
    node.child_first = inner;
    node.child_last = inner;
    return result.addNode(node);
  }

  constexpr int computeFixedWidth(NodeIdx nodeIdx) noexcept {
    if (nodeIdx < 0) {
      return 0;
    }
    const auto& n = result.nodes[nodeIdx];
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
        return computeFixedWidth(n.child_first);
      case NodeKind::Sequence: {
        int total = 0;
        int child = n.child_first;
        while (child >= 0) {
          int w = computeFixedWidth(child);
          if (w < 0) {
            return kNoNode;
          }
          total += w;
          child = result.nodes[child].next_sibling;
        }
        return total;
      }
      case NodeKind::Alternation: {
        int child = n.child_first;
        if (child < 0) {
          return 0;
        }
        int width = computeFixedWidth(child);
        if (width < 0) {
          return -1;
        }
        int next = result.nodes[child].next_sibling;
        while (next >= 0) {
          int w = computeFixedWidth(next);
          if (w < 0 || w != width) {
            return -1;
          }
          next = result.nodes[next].next_sibling;
        }
        return width;
      }
      case NodeKind::Repeat: {
        if (n.min_repeat != n.max_repeat || n.max_repeat < 0) {
          return -1;
        }
        int innerWidth = computeFixedWidth(n.child_first);
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

  constexpr NodeIdx parseCharClass() noexcept {
    advance(); // consume '['

    bool negated = tryConsume('^');
    CharRangeSet rs;

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
            pattern[colonPos + 1] == ']') {
          // Valid [:name:] syntax
          std::string_view className(
              pattern.data() + nameStart, colonPos - nameStart);
          if (negatedPosix) {
            CharRangeSet tmp;
            if (!addPosixClass(tmp, className)) {
              error("Unknown POSIX class name");
              return kNoNode;
            }
            tmp.invert();
            rs.merge(tmp);
          } else {
            if (!addPosixClass(rs, className)) {
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
        if (next == 'w' || next == 'W') {
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
        if (next == 'd' || next == 'D') {
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
        if (next == 's' || next == 'S') {
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
      }

      char lo = parseClassChar();
      if (!result.valid) {
        return kNoNode;
      }

      if (!atEnd() && peek() == '-' && pos + 1 < pattern.size() &&
          pattern[pos + 1] != ']') {
        advance(); // consume '-'
        char hi = parseClassChar();
        if (!result.valid) {
          return kNoNode;
        }
        if (static_cast<unsigned char>(lo) > static_cast<unsigned char>(hi)) {
          error("Invalid character range in class");
          return kNoNode;
        }
        rs.addRange(
            static_cast<unsigned char>(lo), static_cast<unsigned char>(hi));
      } else {
        rs.addChar(static_cast<unsigned char>(lo));
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

  constexpr NodeIdx parseEscape() noexcept {
    advance(); // consume '\'
    if (atEnd()) {
      error("Trailing backslash");
      return kNoNode;
    }

    char c = peek();

    // Backreferences \1 through \9
    if (c >= '1' && c <= '9') {
      advance();
      int groupNum = c - '0';
      if (groupNum > result.group_count) {
        error("Backreference to nonexistent group");
        return kNoNode;
      }
      result.nfa_compatible = false;
      result.has_backref = true;
      AstNode node;
      node.kind = NodeKind::Backref;
      node.group_id = groupNum;
      return result.addNode(node);
    }

    if (c == 'd' || c == 'D') {
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

    if (c == 'w' || c == 'W') {
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

    if (c == 's' || c == 'S') {
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

    if (c == 'b') {
      advance();
      result.nfa_compatible = false;
      AstNode node;
      node.kind = NodeKind::WordBoundary;
      return result.addNode(node);
    }

    if (c == 'B') {
      advance();
      result.nfa_compatible = false;
      AstNode node;
      node.kind = NodeKind::NegWordBoundary;
      return result.addNode(node);
    }

    if (c == 'A') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::StartOfString;
      return result.addNode(node);
    }

    if (c == 'z') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::EndOfString;
      return result.addNode(node);
    }

    if (c == 'Z') {
      advance();
      AstNode node;
      node.kind = NodeKind::Anchor;
      node.anchor = AnchorKind::EndOfStringOrNewline;
      return result.addNode(node);
    }

    if (c == 'C') {
      advance();
      AstNode node;
      node.kind = NodeKind::AnyByte;
      return result.addNode(node);
    }

    char escaped = parseEscapeChar();
    if (!result.valid) {
      return kNoNode;
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
constexpr auto parse(std::string_view pattern) {
  AstBuilder builder;
  builder.valid = true;
  Parser parser(pattern, builder);
  builder.root = parser.parseRegex();

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
      0}>(builder);
}

template <ParseErrorMode Mode = ParseErrorMode::CompileTime, std::size_t N>
constexpr auto parse(const char (&pattern)[N]) {
  return parse<N - 1, Mode>(std::string_view(pattern, N - 1));
}

} // namespace detail
} // namespace regex
} // namespace folly
