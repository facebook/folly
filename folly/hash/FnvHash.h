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
#include <string>
#include <string_view>
#include <type_traits>

namespace folly {
namespace hash {

//  fnv
//
//  Fowler / Noll / Vo (FNV) Hash
//    http://www.isthe.com/chongo/tech/comp/fnv/
//
//  Discouraged for poor performance in the smhasher suite.

constexpr uint32_t fnv32_hash_start = 2166136261UL;
constexpr uint32_t fnva32_hash_start = 2166136261UL;
constexpr uint64_t fnv64_hash_start = 14695981039346656037ULL;
constexpr uint64_t fnva64_hash_start = 14695981039346656037ULL;

/**
 * Append byte to FNV hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint32_t fnv32_append_byte_BROKEN(uint32_t hash, uint8_t c) noexcept {
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 24);
  // forcing signed char, since other platforms can use unsigned
  hash ^= static_cast<int8_t>(c);
  return hash;
}

namespace detail {

template <typename T>
constexpr bool is_hashable_byte_v = false;
template <>
inline constexpr bool is_hashable_byte_v<char> = true;
template <>
inline constexpr bool is_hashable_byte_v<signed char> = true;
template <>
inline constexpr bool is_hashable_byte_v<unsigned char> = true;

} // namespace detail

/**
 * FNV hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint32_t fnv32_buf_BROKEN(
    const C* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnv32_append_byte_BROKEN(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint32_t fnv32_buf_BROKEN(
    const void* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_BROKEN(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNV hash of a c-str.
 *
 * Continues hashing until a null byte is reached.
 *
 * @param hash  The initial hash seed.
 *
 * @methodset fnv
 */
constexpr uint32_t fnv32_BROKEN(
    const char* buf, uint32_t hash = fnv32_hash_start) noexcept {
  for (; *buf; ++buf) {
    hash = fnv32_append_byte_BROKEN(hash, static_cast<uint8_t>(*buf));
  }
  return hash;
}

/**
 * @overloadbrief FNV hash of a string.
 *
 * FNV is the Fowler / Noll / Vo Hash:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Discouraged for poor performance in the smhasher suite.
 *
 * @param hash  The initial hash seed.
 *
 * @methodset fnv
 */
inline uint32_t fnv32_BROKEN(
    const std::string& str, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_BROKEN(str.data(), str.size(), hash);
}

/**
 * Append byte to FNV hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint32_t fnv32_append_byte_FIXED(uint32_t hash, uint8_t c) noexcept {
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 24);
  // forcing unsigned char
  hash ^= static_cast<uint8_t>(c);
  return hash;
}

/**
 * FNV hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint32_t fnv32_buf_FIXED(
    const C* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnv32_append_byte_FIXED(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint32_t fnv32_buf_FIXED(
    const void* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_FIXED(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNV hash of a c-str.
 *
 * Continues hashing until a null byte is reached.
 *
 * @param hash  The initial hash seed.
 *
 * @methodset fnv
 */
constexpr uint32_t fnv32_FIXED(
    const char* buf, uint32_t hash = fnv32_hash_start) noexcept {
  for (; *buf; ++buf) {
    hash = fnv32_append_byte_FIXED(hash, static_cast<uint8_t>(*buf));
  }
  return hash;
}

/**
 * @overloadbrief FNV hash of a string.
 *
 * FNV is the Fowler / Noll / Vo Hash:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Discouraged for poor performance in the smhasher suite.
 *
 * @param hash  The initial hash seed.
 *
 * @methodset fnv
 */
inline uint32_t fnv32_FIXED(
    const std::string& str, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_FIXED(str.data(), str.size(), hash);
}

/**
 * Append a byte to FNVA hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint32_t fnva32_append_byte(uint32_t hash, uint8_t c) noexcept {
  hash ^= c;
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 24);
  return hash;
}

/**
 * FNVA hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint32_t fnva32_buf(
    const C* buf, size_t n, uint32_t hash = fnva32_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnva32_append_byte(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint32_t fnva32_buf(
    const void* buf, size_t n, uint32_t hash = fnva32_hash_start) noexcept {
  return fnva32_buf(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNVA hash of a string.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
inline uint32_t fnva32(
    const std::string& str, uint32_t hash = fnva32_hash_start) noexcept {
  return fnva32_buf(str.data(), str.size(), hash);
}

/**
 * Append a byte to FNV hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint64_t fnv64_append_byte_FIXED(uint64_t hash, uint8_t c) {
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 5) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 40);
  // forcing unsigned char
  hash ^= static_cast<uint8_t>(c);
  return hash;
}

/**
 * FNV hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint64_t fnv64_buf_FIXED(
    const C* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnv64_append_byte_FIXED(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint64_t fnv64_buf_FIXED(
    const void* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_FIXED(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNV hash of a c-str.
 *
 * Continues hashing until a null byte is reached.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint64_t fnv64_FIXED(
    const char* buf, uint64_t hash = fnv64_hash_start) noexcept {
  for (; *buf; ++buf) {
    hash = fnv64_append_byte_FIXED(hash, static_cast<uint8_t>(*buf));
  }
  return hash;
}

/**
 * @overloadbrief FNV hash of a string.
 *
 * FNV is the Fowler / Noll / Vo Hash:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Discouraged for poor performance in the smhasher suite.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
inline uint64_t fnv64_FIXED(
    std::string_view str, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_FIXED(str.data(), str.size(), hash);
}

/**
 * Append a byte to FNV hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint64_t fnv64_append_byte_BROKEN(uint64_t hash, uint8_t c) {
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 5) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 40);
  // forcing signed char, since other platforms can use unsigned
  hash ^= static_cast<int8_t>(c);
  return hash;
}

/**
 * FNV hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint64_t fnv64_buf_BROKEN(
    const C* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnv64_append_byte_BROKEN(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint64_t fnv64_buf_BROKEN(
    const void* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_BROKEN(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNV hash of a c-str.
 *
 * Continues hashing until a null byte is reached.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint64_t fnv64_BROKEN(
    const char* buf, uint64_t hash = fnv64_hash_start) noexcept {
  for (; *buf; ++buf) {
    hash = fnv64_append_byte_BROKEN(hash, static_cast<uint8_t>(*buf));
  }
  return hash;
}

/**
 * @overloadbrief FNV hash of a string.
 *
 * FNV is the Fowler / Noll / Vo Hash:
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * Discouraged for poor performance in the smhasher suite.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
inline uint64_t fnv64_BROKEN(
    std::string_view str, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_BROKEN(str.data(), str.size(), hash);
}

/**
 * Alias for fnv32_append_byte_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
constexpr uint32_t fnv32_append_byte(uint32_t hash, uint8_t c) noexcept {
  return fnv32_append_byte_BROKEN(hash, c);
}

/**
 * Alias for fnv32_buf_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint32_t fnv32_buf(
    const C* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_BROKEN(buf, n, hash);
}
inline uint32_t fnv32_buf(
    const void* buf, size_t n, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_buf_BROKEN(buf, n, hash);
}

/**
 * Alias for fnv32_BROKEN.
 *
 * @methodset fnv
 */
constexpr uint32_t fnv32(
    const char* buf, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_BROKEN(buf, hash);
}

/**
 * Alias for fnv32_BROKEN.
 *
 * @methodset fnv
 */
inline uint32_t fnv32(
    const std::string& str, uint32_t hash = fnv32_hash_start) noexcept {
  return fnv32_BROKEN(str, hash);
}

/**
 * Alias for fnv64_append_byte_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
constexpr uint64_t fnv64_append_byte(uint64_t hash, uint8_t c) {
  return fnv64_append_byte_BROKEN(hash, c);
}

/**
 * Alias for fnv64_buf_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint64_t fnv64_buf(
    const C* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_BROKEN(buf, n, hash);
}
inline uint64_t fnv64_buf(
    const void* buf, size_t n, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_buf_BROKEN(buf, n, hash);
}

/**
 * Alias for fnv64_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
constexpr uint64_t fnv64(
    const char* buf, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_BROKEN(buf, hash);
}

/**
 * Alias for fnv64_BROKEN.
 *
 * @see fnv32_BROKEN
 * @methodset fnv
 */
inline uint64_t fnv64(
    std::string_view str, uint64_t hash = fnv64_hash_start) noexcept {
  return fnv64_BROKEN(str, hash);
}

/**
 * Append a byte to FNVA hash.
 *
 * @see fnv32
 * @methodset fnv
 */
constexpr uint64_t fnva64_append_byte(uint64_t hash, uint8_t c) {
  hash ^= c;
  hash = hash //
      + (hash << 1) //
      + (hash << 4) //
      + (hash << 5) //
      + (hash << 7) //
      + (hash << 8) //
      + (hash << 40);
  return hash;
}

/**
 * FNVA hash of a byte-range.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
template <typename C, std::enable_if_t<detail::is_hashable_byte_v<C>, int> = 0>
constexpr uint64_t fnva64_buf(
    const C* buf, size_t n, uint64_t hash = fnva64_hash_start) noexcept {
  for (size_t i = 0; i < n; ++i) {
    hash = fnva64_append_byte(hash, static_cast<uint8_t>(buf[i]));
  }
  return hash;
}
inline uint64_t fnva64_buf(
    const void* buf, size_t n, uint64_t hash = fnva64_hash_start) noexcept {
  return fnva64_buf(reinterpret_cast<const uint8_t*>(buf), n, hash);
}

/**
 * FNVA hash of a string.
 *
 * @param hash  The initial hash seed.
 *
 * @see fnv32
 * @methodset fnv
 */
inline uint64_t fnva64(
    const std::string& str, uint64_t hash = fnva64_hash_start) noexcept {
  return fnva64_buf(str.data(), str.size(), hash);
}

} // namespace hash
} // namespace folly
