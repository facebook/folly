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
#include <folly/Range.h>
#include <folly/detail/SimdForEach.h>
#include <folly/lang/Bits.h>

#if FOLLY_X64
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

// This file is not supposed to be included by users.
// It should be included in CPP file which exposes apis.
// It is a header file to test different platforms.

// All funcitons are force inline because they are merged into big hiddend
// noinline functions
namespace folly {
namespace detail {

#if FOLLY_X64

struct StringSplitSse2Platform {
  using reg_t = __m128i;

  static constexpr int kCardinal = 16;
  static constexpr int kMmaskBitsPerElement = 1;

  // We can actually use aligned loads but our Intel people don't recommend
  FOLLY_ALWAYS_INLINE
  static reg_t loadu(const char* p) {
    return _mm_loadu_si128(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ALWAYS_INLINE
  static reg_t unsafeLoadu(const char* p) {
    return _mm_loadu_si128(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_ALWAYS_INLINE
  static std::uint16_t equal(reg_t reg, char x) {
    return _mm_movemask_epi8(_mm_cmpeq_epi8(reg, _mm_set1_epi8(x)));
  }
};

#if defined(__AVX2__)

struct StringSplitAVX2Platform {
  using reg_t = __m256i;

  static constexpr int kCardinal = 32;
  static constexpr int kMmaskBitsPerElement = 1;

  // We can actually use aligned loads but our Intel people don't recommend
  FOLLY_ALWAYS_INLINE
  static reg_t loadu(const char* p) {
    return _mm256_loadu_si256(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ALWAYS_INLINE
  static reg_t unsafeLoadu(const char* p) {
    return _mm256_loadu_si256(reinterpret_cast<const reg_t*>(p));
  }

  FOLLY_ALWAYS_INLINE
  static std::uint32_t equal(reg_t reg, char x) {
    return _mm256_movemask_epi8(_mm256_cmpeq_epi8(reg, _mm256_set1_epi8(x)));
  }
};

using StringSplitCurrentPlatform = StringSplitAVX2Platform;

#else
using StringSplitCurrentPlatform = StringSplitSse2Platform;
#endif

#elif FOLLY_AARCH64

struct StringSplitAarch64Platform {
  using reg_t = uint8x16_t;

  static constexpr int kCardinal = 16;
  static constexpr int kMmaskBitsPerElement = 4;

  FOLLY_ALWAYS_INLINE
  static reg_t loadu(const char* p) {
    return vld1q_u8(reinterpret_cast<const std::uint8_t*>(p));
  }

  FOLLY_DISABLE_SANITIZERS
  FOLLY_ALWAYS_INLINE
  static reg_t unsafeLoadu(const char* p) {
    return vld1q_u8(reinterpret_cast<const std::uint8_t*>(p));
  }

  FOLLY_ALWAYS_INLINE
  static std::uint64_t equal(reg_t reg, char x) {
    reg_t test = vceqq_u8(reg, vdupq_n_u8(static_cast<std::uint8_t>(x)));

    // we can do any with horizontal max but that didn't help here
    //
    // based on:
    // https://github.com/jfalcou/eve/blob/5264e20c51aeca17675e67abf236ce1ead781c52/include/eve/detail/function/simd/arm/neon/movemask.hpp#L119
    // pack 4 bits into uint64
    uint16x8_t u16s = vreinterpretq_u16_u8(test);
    u16s = vshrq_n_u16(u16s, 4);
    uint8x8_t packed = vmovn_u16(u16s);
    return vget_lane_u64(vreinterpret_u64_u8(packed), 0);
  }
};

using StringSplitCurrentPlatform = StringSplitAarch64Platform;

#else

using StringSplitCurrentPlatform = void;

#endif

template <bool ignoreEmpty, typename Container>
void splitByCharScalar(char sep, folly::StringPiece what, Container& res) {
  const char* prev = what.data();
  const char* f = prev;
  const char* l = what.data() + what.size();

  auto emplaceBack = [&](const char* sf, const char* sl) mutable {
    if (ignoreEmpty && sf == sl) {
      return;
    }
    res.emplace_back(sf, sl - sf);
  };

  while (f != l) {
    const char* next = f + 1;
    if (*f == sep) {
      if (!ignoreEmpty || (prev != f)) {
        emplaceBack(prev, f);
      }
      prev = next;
    }
    f = next;
  }
  emplaceBack(prev, f);
}

template <typename Platform, bool ignoreEmpty>
struct PlatformSimdSplitByChar {
  using reg_t = typename Platform::reg_t;

  // These are aligned loads but there is no point in generating
  // aligned load instructions, so we call loadu.
  FOLLY_ALWAYS_INLINE
  reg_t loada(const char* ptr, simd_detail::ignore_none) const {
    return Platform::loadu(ptr);
  }

  FOLLY_ALWAYS_INLINE
  reg_t loada(const char* ptr, simd_detail::ignore_extrema) const {
    return Platform::unsafeLoadu(ptr);
  }

  template <typename Uint>
  FOLLY_ALWAYS_INLINE Uint setLowerNBits(int n) const {
    if (sizeof(Uint) == 8 && n == 64) {
      return static_cast<Uint>(-1);
    }
    return static_cast<Uint>((std::uint64_t{1} << n) - 1);
  }

  template <typename Uint>
  FOLLY_ALWAYS_INLINE Uint
  clear(Uint mmask, simd_detail::ignore_extrema ignore) const {
    Uint clearFirst =
        ~setLowerNBits<Uint>(ignore.first * Platform::kMmaskBitsPerElement);
    Uint clearLast = setLowerNBits<Uint>(
        (Platform::kCardinal - ignore.last) * Platform::kMmaskBitsPerElement);
    return mmask & clearFirst & clearLast;
  }

  template <typename Uint>
  FOLLY_ALWAYS_INLINE Uint clear(Uint mmask, simd_detail::ignore_none) const {
    return mmask;
  }

  template <typename Container>
  FOLLY_ALWAYS_INLINE void emplaceBack(
      Container& res, const char* f, const char* l) const {
    if (ignoreEmpty && f == l) {
      return;
    }
    res.emplace_back(f, l - f);
  }

  template <typename Uint, typename Container>
  FOLLY_ALWAYS_INLINE void outputStringsFoMmask(
      Uint mmask,
      const char* pos,
      const char*& prev,
      Container& res) const { // reserve was not beneficial on benchmarks.
    while (mmask) {
      auto counted = folly::findFirstSet(mmask) - 1;
      mmask >>= counted;
      mmask >>= Platform::kMmaskBitsPerElement;
      auto firstSet = counted / Platform::kMmaskBitsPerElement;

      const char* split = pos + firstSet;
      pos = split + 1;
      emplaceBack(res, prev, split);
      prev = pos;
    }
  }

  template <typename Container>
  struct ForEachDelegate {
    const PlatformSimdSplitByChar& self;
    char sep;
    const char*& prev;
    Container& res;

    template <typename Ignore, typename UnrollIndex>
    FOLLY_ALWAYS_INLINE bool step(
        const char* ptr, Ignore ignore, UnrollIndex) const {
      reg_t loaded = self.loada(ptr, ignore);
      auto mmask = Platform::equal(loaded, sep);
      mmask = self.clear(mmask, ignore);
      self.outputStringsFoMmask(mmask, ptr, prev, res);
      return false;
    }
  };

  template <typename Container>
  FOLLY_ALWAYS_INLINE void operator()(
      char sep, folly::StringPiece what, Container& res) const {
    const char* prev = what.data();
    ForEachDelegate<Container> delegate{*this, sep, prev, res};
    simd_detail::simdForEachAligning</*unrolling*/ 1>(
        Platform::kCardinal, what.data(), what.data() + what.size(), delegate);
    emplaceBack(res, prev, what.data() + what.size());
  }
};

template <bool ignoreEmpty>
struct PlatformSimdSplitByChar<void, ignoreEmpty> {
  template <typename Container>
  FOLLY_ALWAYS_INLINE void operator()(
      char sep, folly::StringPiece what, Container& res) const {
    return splitByCharScalar<ignoreEmpty>(sep, what, res);
  }
};

} // namespace detail
} // namespace folly
