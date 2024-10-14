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

#include <folly/Portability.h>
#include <folly/algorithm/simd/Ignore.h>
#include <folly/lang/Bits.h>

#include <cstdint>
#include <type_traits>
#include <utility>

#if FOLLY_X64
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

FOLLY_PUSH_WARNING
FOLLY_GCC_DISABLE_WARNING("-Wignored-attributes")

namespace folly::simd {

/**
 * movemask
 * movemask_fn
 *
 * This is a low level utility used for simd search algorithms.
 *
 * It is a logical extension of _mm_movemask_epi8 for different types
 * for both x86 and arm.
 *
 * Main interface looks like this:
 * folly::simd::movemask<scalar_type>(simdRegister)
 *   -> std::pair<Bits, BitsPerElement>;
 *
 * scalar type - type of element in the simdRegister
 *
 *  Bits - unsigned integral, containing the bitmask (first is lowest bit).
 *  BitsPerElement - std::integral_constant with number of bits per element.
 *
 * There are also overloads taking `ignore`
 *
 *   folly::simd::movemask<T>(nativeRegister, ignore_extrema)
 *   folly::simd::movemask<T>(nativeRegister, ignore_none)
 *
 * These are there if not all the native register contains valid results,
 * and some need to be ignored (zeroed out)
 *
 * Example: find in 8 shorts on arm.
 *
 *  std::optional<std::uint32_t> findUint16(
 *       std::span<const std::uint16_t> haystack,
 *       std::uint16_t needle) {
 *    uint16x8_t loaded = vld1q_u16(arr.data());
 *    uint16x8_t simdNeedle = vdupq_n_u16(needle);
 *    uint16x8_t test = vceqq_u16(loaded, simdNeedle);
 *
 *    auto [bits, bitsPerElement] = folly::simd::movemask<std::uint16_t>(test);
 *    if (!bits) {
 *      return std::nullopt;
 *    }
 *    return std::countl_zero(bits) / bitsPerElement();
 *  }
 *
 * Arm implementation is based on:
 * https://github.com/jfalcou/eve/blob/a2e2cf539e36e9a3326800194ad5206a8ef3f5b7/include/eve/detail/function/simd/arm/neon/movemask.hpp#L48
 *
 **/

template <typename Scalar>
struct movemask_fn {
  template <typename Reg>
  auto operator()(Reg reg) const;

  template <typename Reg, typename Ignore>
  auto operator()(Reg reg, Ignore ignore) const;
};

template <typename Scalar>
inline constexpr movemask_fn<Scalar> movemask;

#if FOLLY_X64

template <typename Scalar>
template <typename Reg>
FOLLY_ERASE auto movemask_fn<Scalar>::operator()(Reg reg) const {
  std::integral_constant<std::uint32_t, sizeof(Scalar) == 2 ? 2 : 1>
      bitsPerElement;

  using uint_t = std::
      conditional_t<std::is_same_v<Reg, __m128i>, std::uint16_t, std::uint32_t>;

  auto mmask = static_cast<uint_t>([&] {
    if constexpr (std::is_same_v<Reg, __m128i>) {
      if constexpr (sizeof(Scalar) <= 2) {
        return _mm_movemask_epi8(reg);
      } else if constexpr (sizeof(Scalar) == 4) {
        return _mm_movemask_ps(_mm_castsi128_ps(reg));
      } else if constexpr (sizeof(Scalar) == 8) {
        return _mm_movemask_pd(_mm_castsi128_pd(reg));
      }
    }
#if defined(__AVX2__)
    else if constexpr (std::is_same_v<Reg, __m256i>) {
      if constexpr (sizeof(Scalar) <= 2) {
        return _mm256_movemask_epi8(reg);
      } else if constexpr (sizeof(Scalar) == 4) {
        return _mm256_movemask_ps(_mm256_castsi256_ps(reg));
      } else if constexpr (sizeof(Scalar) == 8) {
        return _mm256_movemask_pd(_mm256_castsi256_pd(reg));
      }
    }
#endif
  }());
  return std::pair{mmask, bitsPerElement};
}

#endif

#if FOLLY_AARCH64

namespace detail {

FOLLY_ERASE auto movemaskChars16Aarch64(uint8x16_t reg) {
  uint16x8_t u16s = vreinterpretq_u16_u8(reg);
  u16s = vshrq_n_u16(u16s, 4);
  uint8x8_t packed = vmovn_u16(u16s);
  std::uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(packed), 0);
  return std::pair{bits, std::integral_constant<std::uint32_t, 4>{}};
}

template <typename Reg>
FOLLY_ERASE uint64x1_t asUint64x1Aarch64(Reg reg) {
  if constexpr (std::is_same_v<Reg, uint32x2_t>) {
    return vreinterpret_u64_u32(reg);
  } else if constexpr (std::is_same_v<Reg, uint16x4_t>) {
    return vreinterpret_u64_u16(reg);
  } else {
    return vreinterpret_u64_u8(reg);
  }
}

} // namespace detail

template <typename Scalar>
template <typename Reg>
FOLLY_ERASE auto movemask_fn<Scalar>::operator()(Reg reg) const {
  if constexpr (std::is_same_v<Reg, uint64x2_t>) {
    return movemask<std::uint32_t>(vmovn_u64(reg));
  } else if constexpr (std::is_same_v<Reg, uint32x4_t>) {
    return movemask<std::uint16_t>(vmovn_u32(reg));
  } else if constexpr (std::is_same_v<Reg, uint16x8_t>) {
    return movemask<std::uint8_t>(vmovn_u16(reg));
  } else if constexpr (std::is_same_v<Reg, uint8x16_t>) {
    return detail::movemaskChars16Aarch64(reg);
  } else {
    std::uint64_t mmask = vget_lane_u64(detail::asUint64x1Aarch64(reg), 0);
    return std::pair{
        mmask, std::integral_constant<std::uint32_t, sizeof(Scalar) * 8>{}};
  }
}

#endif

#if FOLLY_X64 || FOLLY_AARCH64

template <typename Scalar>
template <typename Reg, typename Ignore>
FOLLY_ERASE auto movemask_fn<Scalar>::operator()(Reg reg, Ignore ignore) const {
  auto [bits, bitsPerElement] = operator()(reg);

  if constexpr (std::is_same_v<Ignore, ignore_none>) {
    return std::pair{bits, bitsPerElement};
  } else {
    static constexpr int kCardinal = sizeof(Reg) / sizeof(Scalar);

    int bitsToKeep = (kCardinal - ignore.last) * bitsPerElement;

    bits = clear_n_least_significant_bits(bits, ignore.first * bitsPerElement);
    bits = clear_n_most_significant_bits(bits, sizeof(bits) * 8 - bitsToKeep);
    return std::pair{bits, bitsPerElement};
  }
}

#endif

} // namespace folly::simd

FOLLY_POP_WARNING
