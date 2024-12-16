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
#include <folly/algorithm/simd/Movemask.h>
#include <folly/algorithm/simd/detail/SimdPlatform.h>
#include <folly/lang/SafeAssert.h>

#include <array>

#if FOLLY_X64 && FOLLY_SSE_PREREQ(4, 2)
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

namespace folly {
namespace simd::detail {

/**
 * SimdPlatform<T>
 *
 * Common interface for some SIMD operations between: sse4.2, avx2,
 * arm-neon.
 *
 * Supported types for T at the moment are uint8_16/uint16_t/uint32_t/uint64_t
 *
 * If it's not one of the supported platforms:
 *  std::same_as<SimdPlatform<T>, void>
 *  There is also a macro: FOLLY_DETAIL_HAS_SIMD_PLATFORM set to 1 or 0
 *
 **/

#if FOLLY_X64 && FOLLY_SSE_PREREQ(4, 2) || FOLLY_AARCH64

template <typename Platform>
struct SimdPlatformCommon {
  /**
   * sclar_t - type of scalar we operate on (uint8_t, uint16_t etc)
   * reg_t - type of a simd register (__m128i)
   * logical_t - type of a simd logical register (matches reg_t so far)
   **/
  using scalar_t = typename Platform::scalar_t;
  using reg_t = typename Platform::reg_t;
  using logical_t = typename Platform::logical_t;

  static constexpr int kCardinal = sizeof(reg_t) / sizeof(scalar_t);

  /**
   * loads:
   * precondition: at least one element should be not ignored.
   *
   * loada - load from an aligned (to sizeof(reg_t)) address
   * loadu - load from an unaligned address
   * unsafeLoadu - load from an unaligned address that disables sanitizers.
   *               This is for reading a register within a page
   *               but maybe outside of the array's boundary.
   *
   * Ignored values can be garbage.
   **/
  template <typename Ignore>
  static reg_t loada(const scalar_t* ptr, Ignore);
  static reg_t loadu(const scalar_t* ptr, ignore_none);
  static reg_t unsafeLoadu(const scalar_t* ptr, ignore_none);

  /**
   * Comparing reg_t against the scalar.
   *
   * NOTE: less_equal only implemented for uint8_t
   *       for now.
   **/
  static logical_t equal(reg_t reg, scalar_t x);
  static logical_t less_equal(reg_t reg, scalar_t x);

  /**
   * logical reduction
   **/
  template <typename Ignore>
  static bool any(logical_t logical, Ignore ignore);

  template <typename Ignore>
  static bool all(logical_t logical, Ignore ignore);

  /**
   * logical operations
   **/
  static logical_t logical_or(logical_t x, logical_t y);

  /**
   * Converting register to an array for debugging
   **/
  static auto toArray(reg_t x);
};

template <typename Platform>
template <typename Ignore>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::loada(
    const scalar_t* ptr, [[maybe_unused]] Ignore ignore) -> reg_t {
  if constexpr (std::is_same_v<ignore_none, Ignore>) {
    // There is not point to aligned load instructions
    // on modern cpus. Arm doesn't even have any.
    return loadu(ptr, ignore_none{});
  } else {
    // We have a precondition: at least one element is loaded.
    // From this we can prove that we can unsafely load from
    // and aligned address.
    //
    // Here is an explanation from Stephen Canon:
    // https://stackoverflow.com/questions/25566302/vectorized-strlen-getting-away-with-reading-unallocated-memory
    if constexpr (!kIsSanitizeAddress) {
      return unsafeLoadu(ptr, ignore_none{});
    } else {
      // If the sanitizers are enabled, we want to trigger the issues.
      // We also want to match the garbage values with/without asan,
      // so that testing works on the same values as prod.
      scalar_t buf[kCardinal];
      std::memcpy(
          buf + ignore.first,
          ptr + ignore.first,
          (kCardinal - ignore.first - ignore.last) * sizeof(scalar_t));

      auto testAgainst = loadu(buf, ignore_none{});
      auto res = unsafeLoadu(ptr, ignore_none{});

      // Extra sanity check.
      FOLLY_SAFE_CHECK(all(Platform::equal(res, testAgainst), ignore));
      return res;
    }
  }
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::loadu(
    const scalar_t* ptr, ignore_none) -> reg_t {
  return Platform::loadu(ptr);
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::unsafeLoadu(
    const scalar_t* ptr, ignore_none) -> reg_t {
  return Platform::unsafeLoadu(ptr);
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::equal(reg_t reg, scalar_t x)
    -> logical_t {
  return Platform::equal(reg, Platform::broadcast(x));
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::less_equal(reg_t reg, scalar_t x)
    -> logical_t {
  static_assert(std::is_same_v<scalar_t, std::uint8_t>, "not implemented");
  return Platform::less_equal(reg, Platform::broadcast(x));
}

template <typename Platform>
template <typename Ignore>
FOLLY_ERASE bool SimdPlatformCommon<Platform>::any(
    logical_t logical, Ignore ignore) {
  if constexpr (std::is_same_v<Ignore, ignore_none>) {
    return Platform::any(logical);
  } else {
    return movemask<scalar_t>(logical, ignore).first;
  }
}

template <typename Platform>
template <typename Ignore>
FOLLY_ERASE bool SimdPlatformCommon<Platform>::all(
    logical_t logical, Ignore ignore) {
  if constexpr (std::is_same_v<Ignore, ignore_none>) {
    return Platform::all(logical);
  } else {
    auto [bits, bitsPerElement] = movemask<scalar_t>(logical, ignore_none{});

    auto expected = n_least_significant_bits<decltype(bits)>(
        bitsPerElement * (kCardinal - ignore.last));
    expected =
        clear_n_least_significant_bits(expected, ignore.first * bitsPerElement);

    return (bits & expected) == expected;
  }
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::logical_or(
    logical_t x, logical_t y) -> logical_t {
  return Platform::logical_or(x, y);
}

template <typename Platform>
FOLLY_ERASE auto SimdPlatformCommon<Platform>::toArray(reg_t x) {
  std::array<scalar_t, kCardinal> res;
  std::memcpy(&res, &x, sizeof(x));
  return res;
}

#endif

#if FOLLY_X64 && FOLLY_SSE_PREREQ(4, 2)

template <typename T>
struct SimdSse42PlatformSpecific {
  using scalar_t = T;
  using reg_t = __m128i;
  using logical_t = reg_t;

  static constexpr std::size_t kCardinal = sizeof(reg_t) / sizeof(scalar_t);

  FOLLY_ERASE
  static reg_t loadu(const scalar_t* p) {
    return _mm_loadu_si128(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ERASE
  static reg_t unsafeLoadu(const scalar_t* p) {
    return _mm_loadu_si128(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_ERASE
  static reg_t broadcast(scalar_t x) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return _mm_set1_epi8(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return _mm_set1_epi16(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return _mm_set1_epi32(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return _mm_set1_epi64x(x);
    }
  }

  FOLLY_ERASE
  static logical_t equal(reg_t x, reg_t y) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return _mm_cmpeq_epi8(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return _mm_cmpeq_epi16(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return _mm_cmpeq_epi32(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return _mm_cmpeq_epi64(x, y);
    }
  }

  FOLLY_ERASE
  static logical_t less_equal(reg_t x, reg_t y) {
    static_assert(
        std::is_same_v<std::uint8_t, scalar_t>, "other types not implemented");
    // No unsigned comparisons on x86
    // less equal <=> equal (min)
    reg_t min = _mm_min_epu8(x, y);
    return equal(x, min);
  }

  FOLLY_ERASE
  static logical_t logical_or(logical_t x, logical_t y) {
    return _mm_or_si128(x, y);
  }

  FOLLY_ERASE
  static bool any(logical_t log) { return movemask<scalar_t>(log).first; }

#if 0 // disabled untill we have a test where this is relevant
  FOLLY_ERASE
  static bool all(logical_t log) {
    auto [bits, bitsPerElement] = movemask<scalar_t>(log);
    return movemask<scalar_t>(log) ==
        n_least_significant_bits<decltype(bits)>(kCardinal * bitsPerElement);
  }
#endif
};

#define FOLLY_DETAIL_HAS_SIMD_PLATFORM 1

template <typename T>
struct SimdSse42Platform : SimdPlatformCommon<SimdSse42PlatformSpecific<T>> {};

#if defined(__AVX2__)

template <typename T>
struct SimdAvx2PlatformSpecific {
  using scalar_t = T;
  using reg_t = __m256i;
  using logical_t = reg_t;

  static constexpr std::size_t kCardinal = sizeof(reg_t) / sizeof(scalar_t);

  FOLLY_ERASE
  static reg_t loadu(const scalar_t* p) {
    return _mm256_loadu_si256(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ERASE
  static reg_t unsafeLoadu(const scalar_t* p) {
    return _mm256_loadu_si256(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_ERASE
  static reg_t broadcast(scalar_t x) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return _mm256_set1_epi8(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return _mm256_set1_epi16(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return _mm256_set1_epi32(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return _mm256_set1_epi64x(x);
    }
  }

  FOLLY_ERASE
  static logical_t equal(reg_t x, reg_t y) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return _mm256_cmpeq_epi8(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return _mm256_cmpeq_epi16(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return _mm256_cmpeq_epi32(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return _mm256_cmpeq_epi64(x, y);
    }
  }

  FOLLY_ERASE
  static logical_t less_equal(reg_t x, reg_t y) {
    static_assert(
        std::is_same_v<std::uint8_t, scalar_t>, "other types not implemented");
    // See SSE comment
    reg_t min = _mm256_min_epu8(x, y);
    return _mm256_cmpeq_epi8(x, min);
  }

  FOLLY_ERASE
  static logical_t logical_or(logical_t x, logical_t y) {
    return _mm256_or_si256(x, y);
  }

  FOLLY_ERASE
  static bool any(logical_t log) { return simd::movemask<scalar_t>(log).first; }

#if 0 // disabled untill we have a test where this is relevant
  FOLLY_ERASE
  static bool all(logical_t log) {
    auto [bits, bitsPerElement] = movemask<scalar_t>(log);
    return movemask<scalar_t>(log) ==
        n_least_significant_bits<decltype(bits)>(kCardinal * bitsPerElement);
  }
#endif
};

template <typename T>
struct SimdAvx2Platform : SimdPlatformCommon<SimdAvx2PlatformSpecific<T>> {};

template <typename T>
using SimdPlatform = SimdAvx2Platform<T>;

#else

template <typename T>
using SimdPlatform = SimdSse42Platform<T>;

#endif

#elif FOLLY_AARCH64

template <typename T>
struct SimdAarch64PlatformSpecific {
  using scalar_t = T;

  FOLLY_ERASE
  static auto loadu(const scalar_t* p) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vld1q_u8(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vld1q_u16(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vld1q_u32(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vld1q_u64(p);
    }
  }

  using reg_t = decltype(loadu(nullptr));
  using logical_t = reg_t;

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ERASE
  static reg_t unsafeLoadu(const scalar_t* p) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vld1q_u8(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vld1q_u16(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vld1q_u32(p);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vld1q_u64(p);
    }
  }

  FOLLY_ERASE
  static reg_t broadcast(scalar_t x) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vdupq_n_u8(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vdupq_n_u16(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vdupq_n_u32(x);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vdupq_n_u64(x);
    }
  }

  FOLLY_ERASE
  static logical_t equal(reg_t x, reg_t y) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vceqq_u8(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vceqq_u16(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vceqq_u32(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vceqq_u64(x, y);
    }
  }

  FOLLY_ERASE
  static logical_t less_equal(reg_t x, reg_t y) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vcleq_u8(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vcleq_u16(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vcleq_u32(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vcleq_u64(x, y);
    }
  }

  FOLLY_ALWAYS_INLINE
  static logical_t logical_or(logical_t x, logical_t y) {
    if constexpr (std::is_same_v<scalar_t, std::uint8_t>) {
      return vorrq_u8(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint16_t>) {
      return vorrq_u16(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint32_t>) {
      return vorrq_u32(x, y);
    } else if constexpr (std::is_same_v<scalar_t, std::uint64_t>) {
      return vorrq_u64(x, y);
    }
  }

  FOLLY_ALWAYS_INLINE
  static bool any(logical_t log) {
    // https://github.com/dotnet/runtime/pull/75864
    auto u32 = bit_cast<uint32x4_t>(log);
    u32 = vpmaxq_u32(u32, u32);
    auto u64 = bit_cast<uint64x2_t>(u32);
    return vgetq_lane_u64(u64, 0);
  }

#if 0 // disabled untill we have a test where this is relevant
  FOLLY_ERASE
  static bool all(logical_t log) {
    // Not quite what they did in .Net runtime, but
    // should be close.
    // https://github.com/dotnet/runtime/pull/75864
    auto u32 = bit_cast<uint32x4_t>(log);
    u32 = vpminq_u32(u32, u32);
    auto u64 = bit_cast<uint64x2_t>(u32);
    return u64 == n_least_significant_bits<std::uint64_t>(64);
  }
#endif
};

#define FOLLY_DETAIL_HAS_SIMD_PLATFORM 1

template <typename T>
struct SimdAarch64Platform
    : SimdPlatformCommon<SimdAarch64PlatformSpecific<T>> {};

template <typename T>
using SimdPlatform = SimdAarch64Platform<T>;

#define FOLLY_DETAIL_HAS_SIMD_PLATFORM 1

#else

#define FOLLY_DETAIL_HAS_SIMD_PLATFORM 0

template <typename T>
using SimdPlatform = void;

#endif

} // namespace simd::detail
} // namespace folly
