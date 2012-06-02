/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_STRING_INL_H_
#define FOLLY_STRING_INL_H_

#include <stdexcept>

#ifndef FOLLY_BASE_STRING_H_
#error This file may only be included from String.h
#endif

namespace folly {

namespace detail {
// Map from character code to value of one-character escape sequence
// ('\n' = 10 maps to 'n'), 'O' if the character should be printed as
// an octal escape sequence, or 'P' if the character is printable and
// should be printed as is.
extern const char cEscapeTable[];
}  // namespace detail

template <class String>
void cEscape(StringPiece str, String& out) {
  char esc[4];
  esc[0] = '\\';
  out.reserve(out.size() + str.size());
  auto p = str.begin();
  auto last = p;  // last regular character
  // We advance over runs of regular characters (printable, not double-quote or
  // backslash) and copy them in one go; this is faster than calling push_back
  // repeatedly.
  while (p != str.end()) {
    char c = *p;
    unsigned char v = static_cast<unsigned char>(c);
    char e = detail::cEscapeTable[v];
    if (e == 'P') {  // printable
      ++p;
    } else if (e == 'O') {  // octal
      out.append(&*last, p - last);
      esc[1] = '0' + ((v >> 6) & 7);
      esc[2] = '0' + ((v >> 3) & 7);
      esc[3] = '0' + (v & 7);
      out.append(esc, 4);
      ++p;
      last = p;
    } else {  // special 1-character escape
      out.append(&*last, p - last);
      esc[1] = e;
      out.append(esc, 2);
      ++p;
      last = p;
    }
  }
  out.append(&*last, p - last);
}

namespace detail {
// Map from the character code of the character following a backslash to
// the unescaped character if a valid one-character escape sequence
// ('n' maps to 10 = '\n'), 'O' if this is the first character of an
// octal escape sequence, 'X' if this is the first character of a
// hexadecimal escape sequence, or 'I' if this escape sequence is invalid.
extern const char cUnescapeTable[];

// Map from the character code to the hex value, or 16 if invalid hex char.
extern const unsigned char hexTable[];
}  // namespace detail

template <class String>
void cUnescape(StringPiece str, String& out, bool strict) {
  out.reserve(out.size() + str.size());
  auto p = str.begin();
  auto last = p;  // last regular character (not part of an escape sequence)
  // We advance over runs of regular characters (not backslash) and copy them
  // in one go; this is faster than calling push_back repeatedly.
  while (p != str.end()) {
    char c = *p;
    if (c != '\\') {  // normal case
      ++p;
      continue;
    }
    out.append(&*last, p - last);
    if (p == str.end()) {  // backslash at end of string
      if (strict) {
        throw std::invalid_argument("incomplete escape sequence");
      }
      out.push_back('\\');
      last = p;
      continue;
    }
    ++p;
    char e = detail::cUnescapeTable[static_cast<unsigned char>(*p)];
    if (e == 'O') {  // octal
      unsigned char val = 0;
      for (int i = 0; i < 3 && p != str.end() && *p >= '0' && *p <= '7';
           ++i, ++p) {
        val = (val << 3) | (*p - '0');
      }
      out.push_back(val);
      last = p;
    } else if (e == 'X') {  // hex
      ++p;
      if (p == str.end()) {  // \x at end of string
        if (strict) {
          throw std::invalid_argument("incomplete hex escape sequence");
        }
        out.append("\\x");
        last = p;
        continue;
      }
      unsigned char val = 0;
      unsigned char h;
      for (; (p != str.end() &&
              (h = detail::hexTable[static_cast<unsigned char>(*p)]) < 16);
           ++p) {
        val = (val << 4) | h;
      }
      out.push_back(val);
      last = p;
    } else if (e == 'I') {  // invalid
      if (strict) {
        throw std::invalid_argument("invalid escape sequence");
      }
      out.push_back('\\');
      out.push_back(*p);
      ++p;
      last = p;
    } else {  // standard escape sequence, \' etc
      out.push_back(e);
      ++p;
      last = p;
    }
  }
  out.append(&*last, p - last);
}

namespace detail {

/*
 * The following functions are type-overloaded helpers for
 * internalSplit().
 */
inline size_t delimSize(char)          { return 1; }
inline size_t delimSize(StringPiece s) { return s.size(); }
inline bool atDelim(const char* s, char c) {
 return *s == c;
}
inline bool atDelim(const char* s, StringPiece sp) {
  return !std::memcmp(s, sp.start(), sp.size());
}

// These are used to short-circuit internalSplit() in the case of
// 1-character strings.
inline char delimFront(char c) {
  // This one exists only for compile-time; it should never be called.
  std::abort();
  return c;
}
inline char delimFront(StringPiece s) {
  assert(!s.empty() && s.start() != nullptr);
  return *s.start();
}

/*
 * These output conversion templates allow us to support multiple
 * output string types, even when we are using an arbitrary
 * OutputIterator.
 */
template<class OutStringT> struct OutputConverter {};

template<> struct OutputConverter<std::string> {
  std::string operator()(StringPiece sp) const {
    return sp.toString();
  }
};

template<> struct OutputConverter<fbstring> {
  fbstring operator()(StringPiece sp) const {
    return sp.toFbstring();
  }
};

template<> struct OutputConverter<StringPiece> {
  StringPiece operator()(StringPiece sp) const { return sp; }
};

/*
 * Shared implementation for all the split() overloads.
 *
 * This uses some external helpers that are overloaded to let this
 * algorithm be more performant if the deliminator is a single
 * character instead of a whole string.
 *
 * @param ignoreEmpty iff true, don't copy empty segments to output
 */
template<class OutStringT, class DelimT, class OutputIterator>
void internalSplit(DelimT delim, StringPiece sp, OutputIterator out,
    bool ignoreEmpty) {
  assert(sp.start() != nullptr);

  const char* s = sp.start();
  const size_t strSize = sp.size();
  const size_t dSize = delimSize(delim);

  OutputConverter<OutStringT> conv;

  if (dSize > strSize || dSize == 0) {
    if (!ignoreEmpty || strSize > 0) {
      *out++ = conv(sp);
    }
    return;
  }
  if (boost::is_same<DelimT,StringPiece>::value && dSize == 1) {
    // Call the char version because it is significantly faster.
    return internalSplit<OutStringT>(delimFront(delim), sp, out,
      ignoreEmpty);
  }

  int tokenStartPos = 0;
  int tokenSize = 0;
  for (int i = 0; i <= strSize - dSize; ++i) {
    if (atDelim(&s[i], delim)) {
      if (!ignoreEmpty || tokenSize > 0) {
        *out++ = conv(StringPiece(&s[tokenStartPos], tokenSize));
      }

      tokenStartPos = i + dSize;
      tokenSize = 0;
      i += dSize - 1;
    } else {
      ++tokenSize;
    }
  }

  if (!ignoreEmpty || tokenSize > 0) {
    tokenSize = strSize - tokenStartPos;
    *out++ = conv(StringPiece(&s[tokenStartPos], tokenSize));
  }
}

template<class String> StringPiece prepareDelim(const String& s) {
  return StringPiece(s);
}
inline char prepareDelim(char c) { return c; }

}

//////////////////////////////////////////////////////////////////////

template<class Delim, class String, class OutputType>
void split(const Delim& delimiter,
           const String& input,
           std::vector<OutputType>& out,
           bool ignoreEmpty) {
  detail::internalSplit<OutputType>(
    detail::prepareDelim(delimiter),
    StringPiece(input),
    std::back_inserter(out),
    ignoreEmpty);
}

template<class Delim, class String, class OutputType>
void split(const Delim& delimiter,
           const String& input,
           fbvector<OutputType>& out,
           bool ignoreEmpty = false) {
  detail::internalSplit<OutputType>(
    detail::prepareDelim(delimiter),
    StringPiece(input),
    std::back_inserter(out),
    ignoreEmpty);
}

template<class OutputValueType, class Delim, class String,
         class OutputIterator>
void splitTo(const Delim& delimiter,
             const String& input,
             OutputIterator out,
             bool ignoreEmpty) {
  detail::internalSplit<OutputValueType>(
    detail::prepareDelim(delimiter),
    StringPiece(input),
    out,
    ignoreEmpty);
}

namespace detail {
/**
 * Hex-dump at most 16 bytes starting at offset from a memory area of size
 * bytes.  Return the number of bytes actually dumped.
 */
size_t hexDumpLine(const void* ptr, size_t offset, size_t size,
                   std::string& line);
}  // namespace detail

template <class OutIt>
void hexDump(const void* ptr, size_t size, OutIt out) {
  size_t offset = 0;
  std::string line;
  while (offset < size) {
    offset += detail::hexDumpLine(ptr, offset, size, line);
    *out++ = line;
  }
}

}  // namespace folly

#endif /* FOLLY_STRING_INL_H_ */

