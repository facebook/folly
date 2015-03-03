/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_VARINT_H_
#define FOLLY_VARINT_H_

#include <type_traits>
#include <folly/Conv.h>
#include <folly/Range.h>

namespace folly {

/**
 * Variable-length integer encoding, using a little-endian, base-128
 * representation.
 *
 * The MSb is set on all bytes except the last.
 *
 * Details:
 * https://developers.google.com/protocol-buffers/docs/encoding#varints
 *
 * If you want to encode multiple values, GroupVarint (in GroupVarint.h)
 * is faster and likely smaller.
 */

/**
 * Maximum length (in bytes) of the varint encoding of a 32-bit value.
 */
constexpr size_t kMaxVarintLength32 = 5;

/**
 * Maximum length (in bytes) of the varint encoding of a 64-bit value.
 */
constexpr size_t kMaxVarintLength64 = 10;

/**
 * Encode a value in the given buffer, returning the number of bytes used
 * for encoding.
 * buf must have enough space to represent the value (at least
 * kMaxVarintLength64 bytes to encode arbitrary 64-bit values)
 */
size_t encodeVarint(uint64_t val, uint8_t* buf);

/**
 * Decode a value from a given buffer, advances data past the returned value.
 */
template <class T>
uint64_t decodeVarint(Range<T*>& data);

/**
 * ZigZag encoding that maps signed integers with a small absolute value
 * to unsigned integers with a small (positive) values. Without this,
 * encoding negative values using Varint would use up 9 or 10 bytes.
 *
 * if x >= 0, encodeZigZag(x) == 2*x
 * if x <  0, encodeZigZag(x) == -2*x + 1
 */

inline uint64_t encodeZigZag(int64_t val) {
  // Bit-twiddling magic stolen from the Google protocol buffer document;
  // val >> 63 is an arithmetic shift because val is signed
  return static_cast<uint64_t>((val << 1) ^ (val >> 63));
}

inline int64_t decodeZigZag(uint64_t val) {
  return static_cast<int64_t>((val >> 1) ^ -(val & 1));
}

// Implementation below

inline size_t encodeVarint(uint64_t val, uint8_t* buf) {
  uint8_t* p = buf;
  while (val >= 128) {
    *p++ = 0x80 | (val & 0x7f);
    val >>= 7;
  }
  *p++ = val;
  return p - buf;
}

template <class T>
inline uint64_t decodeVarint(Range<T*>& data) {
  static_assert(
      std::is_same<typename std::remove_cv<T>::type, char>::value ||
          std::is_same<typename std::remove_cv<T>::type, unsigned char>::value,
      "Only character ranges are supported");

  const int8_t* begin = reinterpret_cast<const int8_t*>(data.begin());
  const int8_t* end = reinterpret_cast<const int8_t*>(data.end());
  const int8_t* p = begin;
  uint64_t val = 0;

  // end is always greater than or equal to begin, so this subtraction is safe
  if (LIKELY(size_t(end - begin) >= kMaxVarintLength64)) {  // fast path
    int64_t b;
    do {
      b = *p++; val  = (b & 0x7f)      ; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) <<  7; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 14; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 21; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 28; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 35; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 42; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 49; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 56; if (b >= 0) break;
      b = *p++; val |= (b & 0x7f) << 63; if (b >= 0) break;
      throw std::invalid_argument("Invalid varint value. Too big.");
    } while (false);
  } else {
    int shift = 0;
    while (p != end && *p < 0) {
      val |= static_cast<uint64_t>(*p++ & 0x7f) << shift;
      shift += 7;
    }
    if (p == end) {
      throw std::invalid_argument("Invalid varint value. Too small: " +
                                  folly::to<std::string>(end - begin) +
                                  " bytes");
    }
    val |= static_cast<uint64_t>(*p++) << shift;
  }

  data.advance(p - begin);
  return val;
}

}  // namespaces

#endif /* FOLLY_VARINT_H_ */
