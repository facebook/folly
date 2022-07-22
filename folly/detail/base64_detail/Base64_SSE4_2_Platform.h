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

#include <cstdint>
#include <folly/Portability.h>
#include <folly/detail/base64_detail/Base64HiddenConstants.h>

#if FOLLY_SSE_PREREQ(4, 2)
#include <immintrin.h>

namespace folly::detail::base64_detail {

/*
 *  NOTE: PLEASE SEE README FOR A DETAILED EXPLANATIONS
 *        VIRTUALLY IMPOSSIBLE TO DECIPHER OTHERWISE.
 */

struct Base64_SSE4_2_Platform {
  using reg_t = __m128i;
  static constexpr std::size_t kRegisterSize = 16;

  // Encode ------------------------------

  static reg_t encodeToIndexesPshuvbMask() {
    // PONM,LKJI,HGFE,DCBA => KLJK'GIGH,EFDE,BCAB
    // clang-format off
      return _mm_set_epi8 (
        10, 11, 9,  10, // KLJK
        7,   8,  6,  7, // GIGH
        4,   5,  3,  4, // EFDE
        1,   2,  0,  1  // BCAB
      );
    // clang-format on
  }

  static reg_t encodeToIndexes(reg_t in) {
    in = _mm_shuffle_epi8(in, encodeToIndexesPshuvbMask());

    const reg_t t0 = _mm_and_si128(in, _mm_set1_epi32(0x0fc0fc00));
    const reg_t t1 = _mm_mulhi_epu16(t0, _mm_set1_epi32(0x04000040));
    const reg_t t2 = _mm_and_si128(in, _mm_set1_epi32(0x003f03f0));
    const reg_t t3 = _mm_mullo_epi16(t2, _mm_set1_epi32(0x01000010));

    return _mm_or_si128(t1, t3);
  }

  static reg_t lookupByIndex(reg_t in, std::int8_t const* offsetTablePtr) {
    const reg_t offsetTable =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(offsetTablePtr));

    // 0-51 become 0, 52 and bigger map to 1 and bigger
    const reg_t reduceTooMuch = _mm_subs_epu8(in, _mm_set1_epi8(51));

    // 0 when should map to A-Z, otherwise -1.
    const reg_t biggerThan25 = _mm_cmpgt_epi8(in, _mm_set1_epi8(25));

    const reg_t offsetLookup = _mm_sub_epi8(reduceTooMuch, biggerThan25);

    return _mm_add_epi8(in, _mm_shuffle_epi8(offsetTable, offsetLookup));
  }

  // Decode ------------------------------------------------------------

  // > 128 changes but stays > 128
  // > '+' stays the same
  // <= '+' becomes closer to 0 so that higher nibble in '+' is 1.
  // Using 0x0f as offset because we'll need it in a different place too.
  static reg_t separatePlusAndSlash(reg_t reg) {
    const reg_t leThanPlus = _mm_cmplt_epi8(reg, _mm_set1_epi8('+' + 1));
    const reg_t plusAndBelowOffset =
        _mm_and_si128(leThanPlus, _mm_set1_epi8(0x0f));
    return _mm_subs_epi8(reg, plusAndBelowOffset);
  }

  static reg_t initError() { return _mm_set1_epi8(0xff); }

  static bool hasErrors(reg_t errorAccumulator) {
    return _mm_movemask_epi8(
        _mm_cmpeq_epi8(errorAccumulator, _mm_setzero_si128()));
  }

  static reg_t decodeErrorDetection(reg_t reg, reg_t higherNibbles) {
    // clang-format off
    const std::int8_t s1_7 = static_cast<std::int8_t>(1 << 7);
    const reg_t pows2 = _mm_set_epi8(
        0, 0, 0, 0,
        0, 0, 0, 0,
        s1_7,   1 << 6, 1 << 5, 1 << 4,
        1 << 3, 1 << 2, 1 << 1, 1 << 0);
    // clang-format on

    reg_t higherNibbleBit = _mm_shuffle_epi8(pows2, higherNibbles);

    // Here we should lookup by lower nibbles.
    // However, this is either equivalent or, if the input byte is
    // negative the result is 0, which is OK since no negative input byte
    // is valid.
    reg_t legalHigherNibblesBits =
        _mm_shuffle_epi8(loadu(constants::kValidHighByLowNibble.data()), reg);

    return _mm_and_si128(higherNibbleBit, legalHigherNibblesBits);
  }

  static reg_t decodeComputeIndexes(reg_t reg, reg_t higherNibbles) {
    reg_t offset = _mm_shuffle_epi8(
        loadu(constants::kOffsetByHighNibbleDecodeTable.data()), higherNibbles);
    return _mm_add_epi8(offset, reg);
  }

  static reg_t decodeToIndex(reg_t reg, reg_t& errorAccumulator) {
    reg = separatePlusAndSlash(reg);

    reg_t higherNibbles =
        _mm_and_si128(_mm_srli_epi32(reg, 4), _mm_set1_epi8(0x0f));

    errorAccumulator = _mm_min_epu8(
        decodeErrorDetection(reg, higherNibbles), errorAccumulator);

    return decodeComputeIndexes(reg, higherNibbles);
  }

  static reg_t packIndexesToBytes(reg_t reg) {
    // ccc << 6 + ddd  aaa << 6 + bbb  (<< 6 == * 0x40)
    reg_t cccddd_aaabbb = _mm_maddubs_epi16(reg, _mm_set1_epi16(0x01'40));

    // Combine the whole epi32 aaabbb << 12 + cccddd (<< 12 == 0x1000)
    reg_t aaabbbcccddd =
        _mm_madd_epi16(cccddd_aaabbb, _mm_set1_epi32(0x1'1000));

    // clang-format off
    return _mm_shuffle_epi8(aaabbbcccddd, _mm_set_epi8(
      -1, -1, -1, -1, // zero out the last 4 bytes
      12, 13, 14,
      8,  9,  10,
      4,   5,  6,
      0,   1,  2
    ));
    // clang-format on
  }

  static reg_t loadu(const void* ptr) {
    return _mm_loadu_si128(reinterpret_cast<const reg_t*>(ptr));
  }

  static void storeu(void* ptr, reg_t reg) {
    _mm_storeu_si128(reinterpret_cast<reg_t*>(ptr), reg);
  }
};

} // namespace folly::detail::base64_detail

#endif // FOLLY_SSE_PREREQ(4, 2)
