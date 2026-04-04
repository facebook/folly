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

template <std::size_t PatLen>
struct Parser {
  std::string_view pattern;
  std::size_t pos = 0;
  ParseResultFor<PatLen> result;

  constexpr explicit Parser(std::string_view pat) : pattern(pat) {
    result.valid = true;
  }

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

  constexpr int parseRegex() noexcept { return parseAlternation(); }

  constexpr int parseAlternation() noexcept {
    int first = parseSequence();
    if (!result.valid) {
      return -1;
    }

    if (!tryConsume('|')) {
      return first;
    }

    AstNode alt;
    alt.kind = NodeKind::Alternation;
    alt.child_begin = first;

    int lastChild = first;
    int childCount = 1;

    while (true) {
      int next = parseSequence();
      if (!result.valid) {
        return -1;
      }

      result.nodes[lastChild].child_end = next;
      lastChild = next;
      ++childCount;

      if (!tryConsume('|')) {
        break;
      }
    }
    result.nodes[lastChild].child_end = -1;

    if (childCount == 2) {
      alt.child_begin = first;
      alt.child_end = lastChild;
      return result.addNode(alt);
    }

    alt.child_begin = first;
    alt.child_end = -1;
    return result.addNode(alt);
  }

  constexpr int parseSequence() noexcept {
    int firstChild = -1;
    int prevChild = -1;
    int childCount = 0;

    while (!atEnd() && peek() != '|' && peek() != ')') {
      int child = parseQuantified();
      if (!result.valid) {
        return -1;
      }

      if (firstChild == -1) {
        firstChild = child;
      }
      if (prevChild != -1) {
        result.nodes[prevChild].child_end = child;
      }
      prevChild = child;
      ++childCount;
    }

    if (childCount == 0) {
      AstNode empty;
      empty.kind = NodeKind::Empty;
      return result.addNode(empty);
    }

    if (childCount == 1) {
      return firstChild;
    }

    if (prevChild != -1) {
      result.nodes[prevChild].child_end = -1;
    }

    AstNode seq;
    seq.kind = NodeKind::Sequence;
    seq.child_begin = firstChild;
    seq.child_end = -1;
    return result.addNode(seq);
  }

  constexpr int parseQuantified() noexcept {
    int atom = parseAtom();
    if (!result.valid) {
      return -1;
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
        return -1;
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

    bool greedy = true;
    bool possessive = false;
    if (!atEnd() && peek() == '?') {
      advance();
      greedy = false;
    } else if (!atEnd() && peek() == '+') {
      advance();
      possessive = true;
      result.nfa_compatible = false;
    }

    AstNode rep;
    rep.kind = NodeKind::Repeat;
    rep.min_repeat = minR;
    rep.max_repeat = maxR;
    rep.greedy = greedy;
    rep.possessive = possessive;
    rep.child_begin = atom;
    rep.child_end = atom;
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

  constexpr int parseAtom() noexcept {
    if (atEnd()) {
      error("Unexpected end of pattern");
      return -1;
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
      return -1;
    }

    if (c == ')') {
      error("Unmatched ')'");
      return -1;
    }

    advance();
    AstNode node;
    node.kind = NodeKind::Literal;
    node.ch = c;
    return result.addNode(node);
  }

  constexpr int parseGroup() noexcept {
    advance(); // consume '('

    bool capturing = true;
    NodeKind lookaroundKind = NodeKind::Empty;

    if (pos < pattern.size() && pattern[pos] == '?') {
      if (pos + 1 < pattern.size()) {
        char next = pattern[pos + 1];
        if (next == ':') {
          capturing = false;
          pos += 2;
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
      result.nfa_compatible = false;

      if (lookaroundKind == NodeKind::Lookbehind ||
          lookaroundKind == NodeKind::NegLookbehind) {
        result.has_lookbehind = true;
      }

      int inner = parseRegex();
      if (!result.valid) {
        return -1;
      }

      if (!tryConsume(')')) {
        error("Unmatched '('");
        return -1;
      }

      AstNode node;
      node.kind = lookaroundKind;
      node.child_begin = inner;
      node.child_end = inner;

      if (lookaroundKind == NodeKind::Lookbehind ||
          lookaroundKind == NodeKind::NegLookbehind) {
        int width = computeFixedWidth(inner);
        if (width < 0) {
          error("Lookbehind requires fixed-width pattern");
          return -1;
        }
        node.min_repeat = width;
      }

      return result.addNode(node);
    }

    int groupId = 0;
    if (capturing) {
      groupId = ++result.group_count;
    }

    int inner = parseRegex();
    if (!result.valid) {
      return -1;
    }

    if (!tryConsume(')')) {
      error("Unmatched '('");
      return -1;
    }

    AstNode node;
    node.kind = NodeKind::Group;
    node.capturing = capturing;
    node.group_id = groupId;
    node.child_begin = inner;
    node.child_end = inner;
    return result.addNode(node);
  }

  constexpr int computeFixedWidth(int nodeIdx) noexcept {
    if (nodeIdx < 0) {
      return 0;
    }
    const auto& n = result.nodes[nodeIdx];
    switch (n.kind) {
      case NodeKind::Empty:
        return 0;
      case NodeKind::Literal:
      case NodeKind::AnyChar:
      case NodeKind::AnyByte:
      case NodeKind::CharClass:
        return 1;
      case NodeKind::Anchor:
      case NodeKind::WordBoundary:
      case NodeKind::NegWordBoundary:
        return 0;
      case NodeKind::Group:
        return computeFixedWidth(n.child_begin);
      case NodeKind::Sequence: {
        int total = 0;
        int child = n.child_begin;
        while (child >= 0) {
          int w = computeFixedWidth(child);
          if (w < 0) {
            return -1;
          }
          total += w;
          child = result.nodes[child].child_end;
        }
        return total;
      }
      case NodeKind::Alternation: {
        int child = n.child_begin;
        if (child < 0) {
          return 0;
        }
        int width = computeFixedWidth(child);
        if (width < 0) {
          return -1;
        }
        int next = result.nodes[child].child_end;
        while (next >= 0) {
          int w = computeFixedWidth(next);
          if (w < 0 || w != width) {
            return -1;
          }
          next = result.nodes[next].child_end;
        }
        return width;
      }
      case NodeKind::Repeat: {
        if (n.min_repeat != n.max_repeat || n.max_repeat < 0) {
          return -1;
        }
        int innerWidth = computeFixedWidth(n.child_begin);
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

  constexpr int parseCharClass() noexcept {
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
        return -1;
      }

      if (!atEnd() && peek() == '-' && pos + 1 < pattern.size() &&
          pattern[pos + 1] != ']') {
        advance(); // consume '-'
        char hi = parseClassChar();
        if (!result.valid) {
          return -1;
        }
        if (static_cast<unsigned char>(lo) > static_cast<unsigned char>(hi)) {
          error("Invalid character range in class");
          return -1;
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
      return -1;
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
    node.negated = negated;
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

  constexpr int parseEscape() noexcept {
    advance(); // consume '\'
    if (atEnd()) {
      error("Trailing backslash");
      return -1;
    }

    char c = peek();

    // Backreferences \1 through \9
    if (c >= '1' && c <= '9') {
      advance();
      int groupNum = c - '0';
      if (groupNum > result.group_count) {
        error("Backreference to nonexistent group");
        return -1;
      }
      result.nfa_compatible = false;
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
      return -1;
    }
    AstNode node;
    node.kind = NodeKind::Literal;
    node.ch = escaped;
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
        if (atEnd() || pos + 1 > pattern.size()) {
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

template <ParseErrorMode Mode = ParseErrorMode::CompileTime>
constexpr auto parse(std::string_view pattern) {
  constexpr std::size_t kMaxLen = 256;
  Parser<kMaxLen> parser(pattern);
  parser.result.root = parser.parseRegex();

  if (parser.result.valid && !parser.atEnd()) {
    parser.error("Unexpected character after pattern");
  }

  if constexpr (Mode == ParseErrorMode::CompileTime) {
    if (!parser.result.valid) {
      const char* msg = parser.result.error_message;
      folly::throw_exception<regex_parse_error>(msg, parser.result.error_pos);
    }
  }

  return parser.result;
}

template <ParseErrorMode Mode = ParseErrorMode::CompileTime, std::size_t N>
constexpr auto parse(const char (&pattern)[N]) {
  return parse<Mode>(std::string_view(pattern, N - 1));
}

} // namespace detail
} // namespace regex
} // namespace folly
