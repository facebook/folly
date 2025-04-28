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

#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include <folly/Portability.h>
#include <folly/memory/UninitializedMemoryHacks.h>

#if FOLLY_X64
#include <immintrin.h>
#endif

namespace folly {

enum class [[nodiscard]] UuidParseCode : unsigned char {
  SUCCESS,
  WRONG_LENGTH,
  INVALID_CHAR,
};

namespace detail {
template <auto buffer_to_buffer_func>
FOLLY_ALWAYS_INLINE UuidParseCode
uuid_parse_generic(std::string& out, std::string_view s) {
  if (s.size() != 36) {
    return UuidParseCode::WRONG_LENGTH;
  }
  folly::resizeWithoutInitialization(out, 16);
  return buffer_to_buffer_func(out.data(), s.data());
}

#if FOLLY_X64 && defined(__AVX2__)

// given a register full of hexadecimal digits (0-9, a-f, A-F),
// compute a register where within each 16-byte lane, the first 8 bytes
// have the values of the parsed 2-digit spans in the input.
//
// clang-format off
// example:
// in =  [790455cb98134298][87ec9ede1dc38e10] (ascii string starting with "7904")
// out = [0x79, 0x04, 0x55, 0xcb, 0x98, 0x13, 0x42, 0x98, _, _, _, _, _, _, _, _]
//       [0x87, 0xec, 0x9e, 0xde, 0x1d, 0xc3, 0x8e, 0x10, _, _, _, _, _, _, _, _]
// where each '_' represents some arbitrary value
// clang-format on
FOLLY_ALWAYS_INLINE __m256i
uuid_parse_parse_hex_without_validation_avx2(const __m256i in) {
  const __m256i mask = _mm256_set1_epi8(0xf0);
  const __m256i hi_nibble = _mm256_srli_epi16(_mm256_and_si256(in, mask), 4);
  // 0-9 are 0x30-0x39, so we should subtract '0'
  // A-F are 0x41-0x46, so we should subtract ('A' - 10)
  // a-f are 0x61-0x66, so we should subtract ('a' - 10)
  // clang-format off
  const __m256i hi_nibble_to_offset = _mm256_setr_epi8(
    0, 0, 0, '0', ('A' - 10), 0, ('a' - 10), 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '0', ('A' - 10), 0, ('a' - 10), 0, 0, 0, 0, 0, 0, 0, 0, 0);
  // clang-format on
  const __m256i per_byte_offset =
      _mm256_shuffle_epi8(hi_nibble_to_offset, hi_nibble);
  const __m256i digits = _mm256_sub_epi8(in, per_byte_offset);

  // shift left 12 bits
  // that is, 0x01 0x02 0x03 0x04 -> 0x00 0x10 0x00 0x30
  const __m256i out_hi_nibbles = _mm256_slli_epi16(digits, 12);
  // 0x01 0x12 0x03 0x34 etc.
  const __m256i out_combined = _mm256_or_si256(out_hi_nibbles, digits);
  // clang-format off
  const __m256i pack_odd = _mm256_setr_epi8(
    1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
    1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
  // clang-format on
  // 0x12 0x34 0x56 0x78 etc.
  return _mm256_shuffle_epi8(out_combined, pack_odd);
}

// returns a bitmask of which input lanes were actually hex digits
FOLLY_ALWAYS_INLINE unsigned int uuid_parse_validate_hex_avx2(
    const __m256i in) {
  const __m256i mask = _mm256_set1_epi8(0xf0);
  const __m256i hi_nibble = _mm256_srli_epi16(_mm256_and_si256(in, mask), 4);
  // We're using a technique based on "Special case 1 — small sets" from
  // http://0x80.pl/notesen/2018-10-18-simd-byte-lookup.html#special-case-1-small-sets
  // The technique is: actually this special case isn't for set of up to 8
  // values. It's for a union U of up to 8 sets S_i where each set
  // S_i = {all values with lo nibble in some set A and hi nibble in some set B}
  // In our case U={0-9a-fA-F}, S_1={0x3}x{0x0-0x9}, S_2={0x4,0x6}x{0x1-0x6}
  //
  // Also, it's not necessary to extract the lower nibbles because none of the
  // values we are looking for have the high bit set.
  //
  // clang-format off
  const __m256i lo_nibble_lookup = _mm256_setr_epi8(
    1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  const __m256i hi_nibble_lookup = _mm256_setr_epi8(
    0, 0, 0, 1, 2, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 2, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  // clang-format on

  const __m256i hi_mask = _mm256_shuffle_epi8(hi_nibble_lookup, hi_nibble);
  const __m256i lo_mask = _mm256_shuffle_epi8(lo_nibble_lookup, in);
  const __m256i valid_input = _mm256_cmpgt_epi8(
      _mm256_and_si256(lo_mask, hi_mask), _mm256_set1_epi8(0));
  return _mm256_movemask_epi8(valid_input);
}

FOLLY_ALWAYS_INLINE UuidParseCode
uuid_parse_buffer_to_buffer_avx2(char* out, const char* s) {
  // clang-format off
  // read the 36-byte input into two 32-byte values
  // a = [790455cb-9813-42][98-87ec-9ede1dc3]
  // b =     [55cb-9813-4298-8][7ec-9ede1dc38e10]
  // clang-format on
  const __m256i a = _mm256_loadu_si256((const __m256i_u*)(s + 0));
  const __m256i b = _mm256_loadu_si256((const __m256i_u*)(s + 4));

  // clang-format off
  // merge the values into one, discarding the positions where we expect dashes
  const __m256i shuffle_a = _mm256_setr_epi8(
    0,  1,  2,  3,  4,  5,  6,  7,  9, 10, 11, 12, 14, 15, -1, -1,
    3,  4,  5,  6,  8,  9, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1);
  const __m256i shuffle_b = _mm256_setr_epi8(
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, 13,
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, 13, 14, 15);
  // a  = [11111111-2222-33][44-5555-66666666]
  // b  =     [1111-2222-3344-5][555-666666667777]
  // sa = [11111111222233__][555566666666____]
  // sb = [______________44][____________7777]
  // c  = [1111111122223344][5555666666667777]
  // clang-format on
  const __m256i sa = _mm256_shuffle_epi8(a, shuffle_a);
  const __m256i sb = _mm256_shuffle_epi8(b, shuffle_b);
  const __m256i c = _mm256_or_si256(sa, sb);

  // check that the dashes are in the right places in the input
  const unsigned int my_dashes_mask =
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, _mm256_set1_epi8('-')));
  const unsigned int desired_non_dashes_mask =
      0b11111111011110111101111011111111u;
  // dashes_mask should be all ones!
  const unsigned int dashes_mask = my_dashes_mask ^ desired_non_dashes_mask;

  const __m256i ret = uuid_parse_parse_hex_without_validation_avx2(c);
  const unsigned int valid_hex_mask = uuid_parse_validate_hex_avx2(c);
  if (~(dashes_mask & valid_hex_mask)) {
    return UuidParseCode::INVALID_CHAR;
  }

  const unsigned long long retA = _mm256_extract_epi64(ret, 0);
  const unsigned long long retB = _mm256_extract_epi64(ret, 2);
  std::memcpy(out + 0, &retA, 8);
  std::memcpy(out + 8, &retB, 8);

  return UuidParseCode::SUCCESS;
}

inline UuidParseCode uuid_parse_avx2(std::string& out, std::string_view s) {
  return uuid_parse_generic<uuid_parse_buffer_to_buffer_avx2>(out, s);
}

#endif // FOLLY_X64 && defined(__AVX2__)

#if FOLLY_X64 && defined(__SSSE3__)

FOLLY_ALWAYS_INLINE __m128i
uuid_parse_parse_hex_without_validation_ssse3(const __m128i in) {
  const __m128i mask = _mm_set1_epi8(0xf0);
  const __m128i hi_nibble = _mm_srli_epi16(_mm_and_si128(in, mask), 4);
  // clang-format off
  const __m128i hi_nibble_to_offset = _mm_setr_epi8(
    0, 0, 0, '0', ('A' - 10), 0, ('a' - 10), 0, 0, 0, 0, 0, 0, 0, 0, 0);
  // clang-format on
  const __m128i per_byte_offset =
      _mm_shuffle_epi8(hi_nibble_to_offset, hi_nibble);
  const __m128i digits = _mm_sub_epi8(in, per_byte_offset);

  const __m128i out_hi_nibbles = _mm_slli_epi16(digits, 12);
  const __m128i out_combined = _mm_or_si128(out_hi_nibbles, digits);
  // clang-format off
  const __m128i pack_odd = _mm_setr_epi8(
    1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
  // clang-format on
  return _mm_shuffle_epi8(out_combined, pack_odd);
}

FOLLY_ALWAYS_INLINE unsigned int uuid_parse_validate_hex_ssse3(
    const __m128i in) {
  const __m128i mask = _mm_set1_epi8(0xf0);
  const __m128i hi_nibble = _mm_srli_epi16(_mm_and_si128(in, mask), 4);
  // clang-format off
  const __m128i lo_nibble_lookup = _mm_setr_epi8(
    1, 3, 3, 3, 3, 3, 3, 1, 1, 1, 0, 0, 0, 0, 0, 0);
  const __m128i hi_nibble_lookup = _mm_setr_epi8(
    0, 0, 0, 1, 2, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  // clang-format on

  const __m128i hi_mask = _mm_shuffle_epi8(hi_nibble_lookup, hi_nibble);
  const __m128i lo_mask = _mm_shuffle_epi8(lo_nibble_lookup, in);
  const __m128i valid_input =
      _mm_cmpgt_epi8(_mm_and_si128(lo_mask, hi_mask), _mm_set1_epi8(0));
  return _mm_movemask_epi8(valid_input);
}

FOLLY_ALWAYS_INLINE UuidParseCode
uuid_parse_buffer_to_buffer_ssse3(char* out, const char* s) {
  // clang-format off
  // read the 36-byte input into three 16-byte values
  // a = [01234567-89ab-cd]
  // b =         [-89ab-cdef-0123-]
  // c =                     [123-456789abcdef]
  // clang-format on
  const __m128i a = _mm_loadu_si128((const __m128i_u*)(s + 0));
  const __m128i b = _mm_loadu_si128((const __m128i_u*)(s + 8));
  const __m128i c = _mm_loadu_si128((const __m128i_u*)(s + 20));

  // clang-format off
  // merge the values into one, discarding the positions where we expect dashes
  const __m128i shuffle_b = _mm_setr_epi8(
   11, 12, 13, 14, -1, -1, -1, -1, 1, 2, 3, 4, 6, 7, 8, 9);
  // a  = [11111111-2222-33]
  // b  =         [-2222-3344-5666-]
  // c  =                     [666-777777777777]
  // sb =             [5666____22223344]
  // ascii_digits_a = [1111111122223344]
  // ascii_digits_b = [5666777777777777]
  // clang-format on
  const __m128i sb = _mm_shuffle_epi8(b, shuffle_b);
  const __m128i ascii_digits_a = _mm_castpd_si128( // no _mm_blend_epi64
      _mm_blend_pd(_mm_castsi128_pd(a), _mm_castsi128_pd(sb), 0b10));
  const __m128i ascii_digits_b = _mm_castps_si128( // _mm_blend_epi32 is AVX2
      _mm_blend_ps(_mm_castsi128_ps(c), _mm_castsi128_ps(sb), 0b0001));

  // check that the dashes are in the right places in the input
  const unsigned int my_dashes_mask =
      _mm_movemask_epi8(_mm_cmpeq_epi8(b, _mm_set1_epi8('-')));
  const unsigned int desired_non_dashes_mask =
      0b11111111111111110111101111011110u;
  // dashes_mask should be all ones!
  const unsigned int dashes_mask = my_dashes_mask ^ desired_non_dashes_mask;

  const __m128i ret_a =
      uuid_parse_parse_hex_without_validation_ssse3(ascii_digits_a);
  const __m128i ret_b =
      uuid_parse_parse_hex_without_validation_ssse3(ascii_digits_b);
  const unsigned int valid_hex_mask_a =
      uuid_parse_validate_hex_ssse3(ascii_digits_a);
  const unsigned int valid_hex_mask_b =
      uuid_parse_validate_hex_ssse3(ascii_digits_b);
  const unsigned int valid_hex_mask =
      (valid_hex_mask_a << 16) | valid_hex_mask_b;

  if (~(dashes_mask & valid_hex_mask)) {
    return UuidParseCode::INVALID_CHAR;
  }

  const unsigned long long ret_a_scalar = _mm_extract_epi64(ret_a, 0);
  const unsigned long long ret_b_scalar = _mm_extract_epi64(ret_b, 0);
  std::memcpy(out + 0, &ret_a_scalar, 8);
  std::memcpy(out + 8, &ret_b_scalar, 8);
  return UuidParseCode::SUCCESS;
}

inline UuidParseCode uuid_parse_ssse3(std::string& out, std::string_view s) {
  return uuid_parse_generic<uuid_parse_buffer_to_buffer_ssse3>(out, s);
}

#endif // FOLLY_X64 && defined(__SSSE3__)

// NOTE: add NEON or SVE?

// scalar fallback
constexpr std::array<std::uint8_t, 256> generateValueTable() {
  std::array<std::uint8_t, 256> table = {};
  for (size_t i = 0; i < 256; ++i) {
    if (i >= '0' && i <= '9') {
      table[i] = static_cast<std::uint8_t>(i - '0');
    } else if (i >= 'A' && i <= 'F') {
      table[i] = static_cast<std::uint8_t>(i - 'A' + 10);
    } else if (i >= 'a' && i <= 'f') {
      table[i] = static_cast<std::uint8_t>(i - 'a' + 10);
    } else {
      table[i] = 16;
    }
  }
  return table;
}
inline constexpr std::array<std::uint8_t, 256> value_table =
    generateValueTable();
static_assert(
    value_table[0] == 16, "value_table must be initialized at compile time");

FOLLY_ALWAYS_INLINE UuidParseCode
uuid_parse_buffer_to_buffer_scalar(char* out, const char* s) {
  int i = 0;
  std::uint8_t tmp[32];
  std::memcpy(tmp + 0, s + 0, 8);
  std::memcpy(tmp + 8, s + 9, 4);
  std::memcpy(tmp + 12, s + 14, 4);
  std::memcpy(tmp + 16, s + 19, 4);
  std::memcpy(tmp + 20, s + 24, 12);
  if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
    return UuidParseCode::INVALID_CHAR;
  }
  while (i < 32) {
    const char hi = value_table[tmp[i++]];
    const char lo = value_table[tmp[i++]];
    if (hi == 16 || lo == 16) {
      return UuidParseCode::INVALID_CHAR;
    }
    *(out++) = (hi << 4) | lo;
  }
  return UuidParseCode::SUCCESS;
}

inline UuidParseCode uuid_parse_scalar(std::string& out, std::string_view s) {
  return uuid_parse_generic<uuid_parse_buffer_to_buffer_scalar>(out, s);
}
} // namespace detail

FOLLY_ALWAYS_INLINE UuidParseCode
uuid_parse_buffer_to_buffer(char* out, const char* s) {
#if FOLLY_X64 && defined(__AVX2__)
  return detail::uuid_parse_buffer_to_buffer_avx2(out, s);
#elif FOLLY_X64 && defined(__SSSE3__)
  return detail::uuid_parse_buffer_to_buffer_ssse3(out, s);
#else
  return detail::uuid_parse_buffer_to_buffer_scalar(out, s);
#endif
}

// reads 36 bytes from s and writes 16 bytes to out
inline UuidParseCode uuid_parse(std::uint8_t* out, const char* s) {
  return uuid_parse_buffer_to_buffer((char*)out, s);
}

// checks that s is 36 bytes long and overwrites s with 16 bytes
inline UuidParseCode uuid_parse(std::string& out, std::string_view s) {
  return detail::uuid_parse_generic<uuid_parse_buffer_to_buffer>(out, s);
}

} // namespace folly
