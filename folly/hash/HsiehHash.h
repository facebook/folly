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
#include <cstring>
#include <string>

#include <folly/lang/Bits.h>

namespace folly {
namespace hash {

//  hsieh
//
//  Paul Hsieh: http://www.azillionmonkeys.com/qed/hash.html
//
//  Discouraged for suboptimal performance in the smhasher suite.

#define get16bits(d) folly::loadUnaligned<uint16_t>(d)

/**
 * hsieh hash a byte-range.
 *
 * @see hsieh_hash32_str
 * @methodset hsieh
 */
inline constexpr uint32_t hsieh_hash32_buf_constexpr(
    const unsigned char* buf, size_t len) noexcept {
  // forcing signed char, since other platforms can use unsigned
  const unsigned char* s = buf;
  uint32_t hash = static_cast<uint32_t>(len);
  uint32_t tmp = 0;
  size_t rem = 0;

  if (len <= 0 || buf == nullptr) {
    return 0;
  }

  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (; len > 0; len--) {
    hash += get16bits(s);
    tmp = (get16bits(s + 2) << 11) ^ hash;
    hash = (hash << 16) ^ tmp;
    s += 2 * sizeof(uint16_t);
    hash += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
    case 3:
      hash += get16bits(s);
      hash ^= hash << 16;
      hash ^= s[sizeof(uint16_t)] << 18;
      hash += hash >> 11;
      break;
    case 2:
      hash += get16bits(s);
      hash ^= hash << 11;
      hash += hash >> 17;
      break;
    case 1:
      hash += *s;
      hash ^= hash << 10;
      hash += hash >> 1;
      break;
    default:
      break;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
}

#undef get16bits

/**
 * hsieh hash a void* byte-range.
 *
 * @see hsieh_hash32_str
 * @methodset hsieh
 */
inline uint32_t hsieh_hash32_buf(const void* buf, size_t len) noexcept {
  return hsieh_hash32_buf_constexpr(
      reinterpret_cast<const unsigned char*>(buf), len);
}

/**
 * hsieh hash a c-str.
 *
 * Computes the strlen of the input, then byte-range hashes it.
 *
 * @see hsieh_hash32_str
 * @methodset hsieh
 */
inline uint32_t hsieh_hash32(const char* s) noexcept {
  return hsieh_hash32_buf(s, std::strlen(s));
}

/**
 * hsieh hash a string.
 *
 * Paul Hsieh: http://www.azillionmonkeys.com/qed/hash.html
 *
 * @methodset hsieh
 */
inline uint32_t hsieh_hash32_str(const std::string& str) noexcept {
  return hsieh_hash32_buf(str.data(), str.size());
}

} // namespace hash
} // namespace folly
