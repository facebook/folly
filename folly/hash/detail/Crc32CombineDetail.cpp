/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/hash/detail/ChecksumDetail.h>

#include <folly/Bits.h>

namespace folly {

// Standard galois-field multiply.  The only modification is that a,
// b, m, and p are all bit-reflected.
//
// https://en.wikipedia.org/wiki/Finite_field_arithmetic
static uint32_t gf_multiply_sw(uint32_t a, uint32_t b, uint32_t m) {
  uint32_t p = 0;
  for (int i = 0; i < 32; i++) {
    p ^= -((b >> 31) & 1) & a;
    a = (a >> 1) ^ (-(a & 1) & m);
    b <<= 1;
  }
  return p;
}

#if FOLLY_SSE_PREREQ(4, 2)

// Reduction taken from
// https://www.nicst.de/crc.pdf
//
// This is an intrinsics-based implementation of listing 3.
static uint32_t gf_multiply_crc32c_hw(uint64_t crc1, uint64_t crc2, uint32_t) {
  const auto crc1_xmm = _mm_set_epi64x(0, crc1);
  const auto crc2_xmm = _mm_set_epi64x(0, crc2);
  const auto count = _mm_set_epi64x(0, 1);
  const auto res0 = _mm_clmulepi64_si128(crc2_xmm, crc1_xmm, 0x00);
  const auto res1 = _mm_sll_epi64(res0, count);

  // Use hardware crc32c to do reduction from 64 -> 32 bytes
  const auto res2 = _mm_cvtsi128_si64(res1);
  const auto res3 = _mm_crc32_u32(0, res2);
  const auto res4 = _mm_extract_epi32(res1, 1);
  return res3 ^ res4;
}

static uint32_t gf_multiply_crc32_hw(uint64_t crc1, uint64_t crc2, uint32_t) {
  const auto crc1_xmm = _mm_set_epi64x(0, crc1);
  const auto crc2_xmm = _mm_set_epi64x(0, crc2);
  const auto count = _mm_set_epi64x(0, 1);
  const auto res0 = _mm_clmulepi64_si128(crc2_xmm, crc1_xmm, 0x00);
  const auto res1 = _mm_sll_epi64(res0, count);

  // Do barrett reduction of 64 -> 32 bytes
  const auto mask32 = _mm_set_epi32(0, 0, 0, 0xFFFFFFFF);
  const auto barrett_reduction_constants =
      _mm_set_epi32(0x1, 0xDB710641, 0x1, 0xF7011641);
  const auto res2 = _mm_clmulepi64_si128(
      _mm_and_si128(res1, mask32), barrett_reduction_constants, 0x00);
  const auto res3 = _mm_clmulepi64_si128(
      _mm_and_si128(res2, mask32), barrett_reduction_constants, 0x10);
  return _mm_cvtsi128_si32(_mm_srli_si128(_mm_xor_si128(res3, res1), 4));
}

#else

static uint32_t gf_multiply_crc32c_hw(uint64_t, uint64_t, uint32_t) {
  return 0;
}
static uint32_t gf_multiply_crc32_hw(uint64_t, uint64_t, uint32_t) {
  return 0;
}

#endif

/*
 * Pre-calculated powers tables for crc32c and crc32.
 * Calculated using:
 *
 * printf("Powers for 0x%08x\n", polynomial);
 * auto power = polynomial;
 * for (int i = 0; i < 62; i++) {
 *   printf("%i 0x%08x\n", i, power);
 *   power = gf_multiply(power, power, polynomial);
 * }
 * printf("-------------\n");
 */
static const uint32_t crc32c_powers[] = {
    0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955, 0xb8fdb1e7,
    0x88e56f72, 0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62, 0x28461564,
    0xbf455269, 0xe2ea32dc, 0xfe7740e6, 0xf946610b, 0x3c204f8f, 0x538586e3,
    0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 0xd289cabe, 0xe94ca9bc,
    0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000, 0x00800000,
    0x00008000, 0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955,
    0xb8fdb1e7, 0x88e56f72, 0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62,
    0x28461564, 0xbf455269, 0xe2ea32dc, 0xfe7740e6, 0xf946610b, 0x3c204f8f,
    0x538586e3, 0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 0xd289cabe,
    0xe94ca9bc, 0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000,
    0x00800000, 0x00008000,
};
static const uint32_t crc32_powers[] = {
    0xedb88320, 0xb1e6b092, 0xa06a2517, 0xed627dae, 0x88d14467, 0xd7bbfe6a,
    0xec447f11, 0x8e7ea170, 0x6427800e, 0x4d47bae0, 0x09fe548f, 0x83852d0f,
    0x30362f1a, 0x7b5a9cc3, 0x31fec169, 0x9fec022a, 0x6c8dedc4, 0x15d6874d,
    0x5fde7a4e, 0xbad90e37, 0x2e4e5eef, 0x4eaba214, 0xa8a472c0, 0x429a969e,
    0x148d302a, 0xc40ba6d0, 0xc4e22c3c, 0x40000000, 0x20000000, 0x08000000,
    0x00800000, 0x00008000, 0xedb88320, 0xb1e6b092, 0xa06a2517, 0xed627dae,
    0x88d14467, 0xd7bbfe6a, 0xec447f11, 0x8e7ea170, 0x6427800e, 0x4d47bae0,
    0x09fe548f, 0x83852d0f, 0x30362f1a, 0x7b5a9cc3, 0x31fec169, 0x9fec022a,
    0x6c8dedc4, 0x15d6874d, 0x5fde7a4e, 0xbad90e37, 0x2e4e5eef, 0x4eaba214,
    0xa8a472c0, 0x429a969e, 0x148d302a, 0xc40ba6d0, 0xc4e22c3c, 0x40000000,
    0x20000000, 0x08000000,
};

template <typename F>
static uint32_t crc32_append_zeroes(
    F mult,
    uint32_t crc,
    size_t len,
    uint32_t polynomial,
    uint32_t const* powers) {
  // Append by multiplying by consecutive powers of two of the zeroes
  // array
  len >>= 2;

  while (len) {
    // Advance directly to next bit set.
    auto r = findFirstSet(len) - 1;
    len >>= r;
    powers += r;

    crc = mult(crc, *powers, polynomial);

    len >>= 1;
    powers++;
  }

  return crc;
}

namespace detail {

uint32_t crc32_combine_sw(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  return crc2 ^
      crc32_append_zeroes(
             gf_multiply_sw, crc1, crc2len, 0xEDB88320, crc32_powers);
}

uint32_t crc32_combine_hw(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  return crc2 ^
      crc32_append_zeroes(
             gf_multiply_crc32_hw, crc1, crc2len, 0xEDB88320, crc32_powers);
}

uint32_t crc32c_combine_sw(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  return crc2 ^
      crc32_append_zeroes(
             gf_multiply_sw, crc1, crc2len, 0x82F63B78, crc32c_powers);
}

uint32_t crc32c_combine_hw(uint32_t crc1, uint32_t crc2, size_t crc2len) {
  return crc2 ^
      crc32_append_zeroes(
             gf_multiply_crc32c_hw, crc1, crc2len, 0x82F63B78, crc32c_powers);
}

} // namespace detail

} // namespace folly
