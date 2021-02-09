/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/Unicode.h>

#include <folly/Conv.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

std::string codePointToUtf8(char32_t cp) {
  std::string result;

  // Based on description from http://en.wikipedia.org/wiki/UTF-8.

  if (cp <= 0x7f) {
    result.resize(1);
    result[0] = static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    result.resize(2);
    result[1] = static_cast<char>(0x80 | (0x3f & cp));
    result[0] = static_cast<char>(0xC0 | (cp >> 6));
  } else if (cp <= 0xFFFF) {
    result.resize(3);
    result[2] = static_cast<char>(0x80 | (0x3f & cp));
    result[1] = (0x80 | static_cast<char>((0x3f & (cp >> 6))));
    result[0] = (0xE0 | static_cast<char>(cp >> 12));
  } else if (cp <= 0x10FFFF) {
    result.resize(4);
    result[3] = static_cast<char>(0x80 | (0x3f & cp));
    result[2] = static_cast<char>(0x80 | (0x3f & (cp >> 6)));
    result[1] = static_cast<char>(0x80 | (0x3f & (cp >> 12)));
    result[0] = static_cast<char>(0xF0 | (cp >> 18));
  }

  return result;
}

char32_t utf8ToCodePoint(
    const unsigned char*& p, const unsigned char* const e, bool skipOnError) {
  // clang-format off
  /** UTF encodings
  *  | # of B | First CP |  Last CP  | Bit Pattern
  *  |   1    |   0x0000 |   0x007F  | 0xxxxxxx
  *  |   2    |   0x0080 |   0x07FF  | 110xxxxx 10xxxxxx
  *  |   3    |   0x0800 |   0xFFFF  | 1110xxxx 10xxxxxx 10xxxxxx
  *  |   4    |  0x10000 | 0x10FFFF  | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  *  |   5    |       -  |        -  | 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
  *  |   6    |       -  |        -  | 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
  *
  *
  * NOTE:
  * - the 4B encoding can encode values up to 0x1FFFFF,
  *   but Unicode defines 0x10FFFF to be the largest code point
  * - the 5B & 6B encodings will all encode values larger than 0x1FFFFF
  *   (so larger than the largest code point value 0x10FFFF) so they form invalid
  *   unicode code points
  *
  * On invalid input (invalid encoding or code points larger than 0x10FFFF):
  * - When skipOnError is true, this function will skip the first byte and return
  *   U'\ufffd'. Potential optimization: skip the whole invalid range.
  * - When skipOnError is false, throws.
  */
  // clang-format on

  const auto skip = [&] {
    ++p;
    return U'\ufffd';
  };

  if (p >= e) {
    if (skipOnError) {
      return skip();
    }
    throw std::runtime_error("folly::utf8ToCodePoint empty/invalid string");
  }

  unsigned char fst = *p;
  if (!(fst & 0x80)) {
    // trivial case, 1 byte encoding
    return *p++;
  }

  static const uint32_t bitMask[] = {
      (1 << 7) - 1,
      (1 << 11) - 1,
      (1 << 16) - 1,
      (1 << 21) - 1,
  };

  // upper control bits are masked out later
  uint32_t d = fst;

  // multi-byte encoded values must start with bits 0b11. 0xC0 is 0b11000000
  if ((fst & 0xC0) != 0xC0) {
    if (skipOnError) {
      return skip();
    }
    throw std::runtime_error(
        to<std::string>("folly::utf8ToCodePoint i=0 d=", d));
  }

  fst <<= 1;

  for (unsigned int i = 1; i != 4 && p + i < e; ++i) {
    const unsigned char tmp = p[i];

    // from the second byte on, format should be 10xxxxxx
    if ((tmp & 0xC0) != 0x80) {
      if (skipOnError) {
        return skip();
      }
      throw std::runtime_error(to<std::string>(
          "folly::utf8ToCodePoint i=", i, " tmp=", (uint32_t)tmp));
    }

    // gradually fill a 32 bit integer d with non control bits in tmp
    // 0x3F is 0b00111111 which clears out the first 2 control bits
    d = (d << 6) | (tmp & 0x3F);
    fst <<= 1;

    if (!(fst & 0x80)) {
      // We know the length of encoding now, since we encounter the first "0" in
      // fst (the first byte). This branch processes the last byte of encoding.
      d &= bitMask[i]; // d is now the code point

      // overlong, could have been encoded with i bytes
      if ((d & ~bitMask[i - 1]) == 0) {
        if (skipOnError) {
          return skip();
        }
        throw std::runtime_error(
            to<std::string>("folly::utf8ToCodePoint i=", i, " d=", d));
      }

      // check for surrogates only needed for 3 bytes
      if (i == 2) {
        if (d >= 0xD800 && d <= 0xDFFF) {
          if (skipOnError) {
            return skip();
          }
          throw std::runtime_error(
              to<std::string>("folly::utf8ToCodePoint i=", i, " d=", d));
        }
      }

      // While UTF-8 encoding can encode arbitrary numbers, 0x10FFFF is the
      // largest defined Unicode code point.
      // Only >=4 bytes can UTF-8 encode such values, so i=3 here.
      if (d > 0x10FFFF) {
        if (skipOnError) {
          return skip();
        }
        throw std::runtime_error(
            "folly::utf8ToCodePoint encoding exceeds max unicode code point");
      }
      p += i + 1;
      return d;
    }
  }

  if (skipOnError) {
    return skip();
  }
  throw std::runtime_error("folly::utf8ToCodePoint encoding length maxed out");
}

//////////////////////////////////////////////////////////////////////

} // namespace folly
