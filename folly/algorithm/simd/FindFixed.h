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

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/algorithm/simd/Movemask.h>
#include <folly/algorithm/simd/detail/Traits.h>

#if FOLLY_X64
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

namespace folly {

namespace detail {

// Note: using std::same_as will just be slower to compile than is_same_v
template <typename T>
concept SimdFriendlyType =
    (std::is_same_v<std::int8_t, T> || std::is_same_v<std::uint8_t, T> ||
     std::is_same_v<std::int16_t, T> || std::is_same_v<std::uint16_t, T> ||
     std::is_same_v<std::int32_t, T> || std::is_same_v<std::uint32_t, T> ||
     std::is_same_v<std::int64_t, T> || std::is_same_v<std::uint64_t, T>);

} // namespace detail

template <typename T>
concept FollyFindFixedSupportedType = detail::SimdFriendlyType<T> ||
    (std::is_enum_v<T> && detail::SimdFriendlyType<std::underlying_type_t<T>>);

/*
 * # folly::findFixed
 *
 * A function to linear search in number of elements, known at compiled time.
 *
 * Example:
 *   std::vector<int> v {1, 3, 1, 2};
 *   std::span<const int, 4> vspan(v.data(), 4);
 *   auto m0 = folly::findFixed(vspan, 3); // m0 == 1;
 *   auto m1 = folly::findFixed(vspan, 5); // m0 == std::nullopt;
 *
 * Supported types:
 *  any 8,16,32,64 bit integers
 *  enums
 *
 * Max supported size of the range is 64 bytes.
 */
template <
    FollyFindFixedSupportedType T,
    std::convertible_to<T> U,
    std::size_t N>
constexpr std::optional<std::size_t> findFixed(std::span<const T, N> where, U x)
  requires(sizeof(T) * N <= 64);

// implementation ---------------------------------------------------------

namespace find_fixed_detail {

template <typename T>
constexpr std::optional<std::size_t> findFixedConstexpr(
    std::span<const T> where, T x) {
  std::size_t res = 0;
  for (T e : where) {
    if (e == x) {
      return res;
    }
    ++res;
  }
  return std::nullopt;
}

// clang just checks all elements one by one, without any vectorization.
// even for not very friendly to SIMD cases we could do better but for
// now only special powers of 2 were interesting.
template <typename T, std::size_t N>
std::optional<std::size_t> findFixedLetTheCompilerDoIt(
    std::span<const T, N> where, T x) {
  // this get's unrolled by both clang and gcc.
  // Experimenting with more complex ways of writing this code
  // didn't yield any results.
  return findFixedConstexpr(std::span<const T>(where), x);
}

#if FOLLY_X64
#if defined(__AVX2__)
constexpr std::size_t kMaxSimdRegister = 32;
#else
constexpr std::size_t kMaxSimdRegister = 16;
#endif
#elif FOLLY_AARCH64
constexpr std::size_t kMaxSimdRegister = 16;
#else
constexpr std::size_t kMaxSimdRegister = 1;
#endif

template <typename T>
std::optional<std::size_t> find8bytes(const T* from, T x);
template <typename T>
std::optional<std::size_t> find16bytes(const T* from, T x);
template <typename T>
std::optional<std::size_t> find32bytes(const T* from, T x);

template <typename T, std::size_t N>
std::optional<std::size_t> find2Overlaping(std::span<const T, N> where, T x);

template <typename T, std::size_t N>
std::optional<std::size_t> findSplitFirstRegister(
    std::span<const T, N> where, T x);

template <typename T, std::size_t N>
std::optional<std::size_t> findFixedDispatch(std::span<const T, N> where, T x) {
  constexpr std::size_t kNumBytes = N * sizeof(T);

  if constexpr (N == 0) {
    return std::nullopt;
  } else if constexpr (N <= 2 || kNumBytes < 8 || kMaxSimdRegister == 1) {
    return findFixedLetTheCompilerDoIt(where, x);
  } else if constexpr (kNumBytes == 8) {
    return find8bytes(where.data(), x);
  } else if constexpr (kNumBytes == 16) {
    return find16bytes(where.data(), x);
  } else if constexpr (kMaxSimdRegister >= 32 && kNumBytes == 32) {
    return find32bytes(where.data(), x);
  } else if constexpr (kMaxSimdRegister * 2 <= kNumBytes) {
    return findSplitFirstRegister(where, x);
  } else {
    // we can maybe do one better here probably with either out of bounds
    // loads or combined two register search but it's ok for now.
    return find2Overlaping(where, x);
  }
}

template <typename T, std::size_t N>
std::optional<std::size_t> find2Overlaping(std::span<const T, N> where, T x) {
  constexpr std::size_t kRegSize = std::bit_floor(N);

  std::span<const T, kRegSize> firstOverlap(where.data(), kRegSize);
  if (auto res = findFixed(firstOverlap, x)) {
    return res;
  }

  std::span<const T, kRegSize> secondOverlap(
      where.data() + (N - kRegSize), kRegSize);
  if (auto res = findFixed(secondOverlap, x)) {
    return *res + (N - kRegSize);
  }
  return std::nullopt;
}

template <typename T, std::size_t N>
std::optional<std::size_t> findSplitFirstRegister(
    std::span<const T, N> where, T x) {
  constexpr std::size_t kRegSize = kMaxSimdRegister / sizeof(T);

  std::span<const T, kRegSize> head(where.data(), kRegSize);
  if (auto res = findFixed(head, x)) {
    return res;
  }

  std::span<const T, N - kRegSize> tail(where.data() + kRegSize, N - kRegSize);
  if (auto res = findFixed(tail, x)) {
    return *res + kRegSize;
  }
  return std::nullopt;
}

template <typename Scalar, typename Reg>
std::optional<std::size_t> firstTrue(Reg reg) {
  auto [bits, bitsPerElement] = folly::simd::movemask<Scalar>(reg);
  if (bits) {
    return std::countr_zero(bits) / bitsPerElement();
  }
  return std::nullopt;
}

#if FOLLY_X64

template <typename T>
std::optional<std::size_t> find16ByteReg(__m128i reg, T x) {
  if constexpr (sizeof(T) == 1) {
    return firstTrue<T>(_mm_cmpeq_epi8(reg, _mm_set1_epi8(x)));
  } else if constexpr (sizeof(T) == 2) {
    return firstTrue<T>(_mm_cmpeq_epi16(reg, _mm_set1_epi16(x)));
  } else if constexpr (sizeof(T) == 4) {
    return firstTrue<T>(_mm_cmpeq_epi32(reg, _mm_set1_epi32(x)));
  }
}

template <typename T>
std::optional<std::size_t> find8bytes(const T* from, T x) {
  std::uint64_t reg;
  std::memcpy(&reg, from, 8);
  return find16ByteReg(_mm_set1_epi64x(reg), x);
}

template <typename T>
std::optional<std::size_t> find16bytes(const T* from, T x) {
  __m128i reg = _mm_loadu_si128(reinterpret_cast<const __m128i*>(from));
  return find16ByteReg(reg, x);
}

#if defined(__AVX2__)
template <typename T>
std::optional<std::size_t> find32ByteReg(__m256i reg, T x) {
  if constexpr (sizeof(T) == 1) {
    return firstTrue<T>(_mm256_cmpeq_epi8(reg, _mm256_set1_epi8(x)));
  } else if constexpr (sizeof(T) == 2) {
    return firstTrue<T>(_mm256_cmpeq_epi16(reg, _mm256_set1_epi16(x)));
  } else if constexpr (sizeof(T) == 4) {
    return firstTrue<T>(_mm256_cmpeq_epi32(reg, _mm256_set1_epi32(x)));
  } else if constexpr (sizeof(T) == 8) {
    return firstTrue<T>(_mm256_cmpeq_epi64(reg, _mm256_set1_epi64x(x)));
  }
}

template <typename T>
std::optional<std::size_t> find32bytes(const T* from, T x) {
  __m256i reg = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(from));
  return find32ByteReg(reg, x);
}

#endif
#endif

#if FOLLY_AARCH64

template <typename T>
std::optional<std::size_t> find8bytes(const T* from, T x) {
  if constexpr (std::same_as<T, std::uint8_t>) {
    return firstTrue<T>(vceq_u8(vld1_u8(from), vdup_n_u8(x)));
  } else if constexpr (std::same_as<T, std::uint16_t>) {
    return firstTrue<T>(vceq_u16(vld1_u16(from), vdup_n_u16(x)));
  } else {
    return firstTrue<T>(vceq_u32(vld1_u32(from), vdup_n_u32(x)));
  }
}

template <typename T>
std::optional<std::size_t> find16bytes(const T* from, T x) {
  if constexpr (std::same_as<T, std::uint8_t>) {
    return firstTrue<T>(vceqq_u8(vld1q_u8(from), vdupq_n_u8(x)));
  } else if constexpr (std::same_as<T, std::uint16_t>) {
    return firstTrue<T>(vceqq_u16(vld1q_u16(from), vdupq_n_u16(x)));
  } else if constexpr (std::same_as<T, std::uint32_t>) {
    return firstTrue<T>(vceqq_u32(vld1q_u32(from), vdupq_n_u32(x)));
  } else {
    return firstTrue<T>(vceqq_u64(vld1q_u64(from), vdupq_n_u64(x)));
  }
}

#endif

} // namespace find_fixed_detail

template <
    FollyFindFixedSupportedType T,
    std::convertible_to<T> U,
    std::size_t N>
constexpr std::optional<std::size_t> findFixed(std::span<const T, N> where, U x)
  requires(sizeof(T) * N <= 64)
{
  if constexpr (!std::is_same_v<T, U>) {
    return findFixed(where, static_cast<T>(x));
  } else if (std::is_constant_evaluated()) {
    return find_fixed_detail::findFixedConstexpr(std::span<const T>(where), x);
  } else {
    return find_fixed_detail::findFixedDispatch(
        simd::detail::asSimdFriendlyUint(where),
        simd::detail::asSimdFriendlyUint(x));
  }
}

} // namespace folly
