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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <folly/CPortability.h>
#include <folly/Likely.h>

namespace folly {
namespace detail {

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
bool generic_checked_add(T* result, T a, T b) {
  if constexpr (std::is_signed_v<T>) {
    if (a >= 0) {
      if (FOLLY_UNLIKELY(std::numeric_limits<T>::max() - a < b)) {
        *result = {};
        return false;
      }
    } else if (FOLLY_UNLIKELY(b < std::numeric_limits<T>::min() - a)) {
      *result = {};
      return false;
    }
    *result = a + b;
    return true;
  } else {
    if (FOLLY_LIKELY(a <= std::numeric_limits<T>::max() - b)) {
      *result = a + b;
      return true;
    } else {
      *result = {};
      return false;
    }
  }
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
bool generic_checked_small_mul(T* result, T a, T b) {
  static_assert(sizeof(T) < sizeof(uint64_t), "Too large");
  uint64_t res = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
  constexpr uint64_t overflowMask = ~((1ULL << (sizeof(T) * 8)) - 1);
  if (FOLLY_UNLIKELY((res & overflowMask) != 0)) {
    *result = {};
    return false;
  }
  *result = static_cast<T>(res);
  return true;
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
std::enable_if_t<sizeof(T) < sizeof(uint64_t), bool> generic_checked_mul(
    T* result, T a, T b) {
  return generic_checked_small_mul(result, a, b);
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
std::enable_if_t<sizeof(T) == sizeof(uint64_t), bool> generic_checked_mul(
    T* result, T a, T b) {
  constexpr uint64_t halfBits = 32;
  constexpr uint64_t halfMask = (1ULL << halfBits) - 1ULL;
  uint64_t lhs_high = a >> halfBits;
  uint64_t lhs_low = a & halfMask;
  uint64_t rhs_high = b >> halfBits;
  uint64_t rhs_low = b & halfMask;

  if (FOLLY_LIKELY(lhs_high == 0 && rhs_high == 0)) {
    *result = lhs_low * rhs_low;
    return true;
  }

  if (FOLLY_UNLIKELY(lhs_high != 0 && rhs_high != 0)) {
    *result = {};
    return false;
  }

  uint64_t mid_bits1 = lhs_low * rhs_high;
  if (FOLLY_UNLIKELY(mid_bits1 >> halfBits != 0)) {
    *result = {};
    return false;
  }

  uint64_t mid_bits2 = lhs_high * rhs_low;
  if (FOLLY_UNLIKELY(mid_bits2 >> halfBits != 0)) {
    *result = {};
    return false;
  }

  uint64_t mid_bits = mid_bits1 + mid_bits2;
  if (FOLLY_UNLIKELY(mid_bits >> halfBits != 0)) {
    *result = {};
    return false;
  }

  uint64_t bot_bits = lhs_low * rhs_low;
  if (FOLLY_UNLIKELY(
          !generic_checked_add(result, bot_bits, mid_bits << halfBits))) {
    *result = {};
    return false;
  }
  return true;
}
} // namespace detail

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
bool checked_add(T* result, T a, T b) {
#if FOLLY_HAS_BUILTIN(__builtin_add_overflow)
  if (FOLLY_LIKELY(!__builtin_add_overflow(a, b, result))) {
    return true;
  }
  *result = {};
  return false;
#else
  return detail::generic_checked_add(result, a, b);
#endif
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
bool checked_add(T* result, T a, T b, T c) {
  T tmp{};
  if (FOLLY_UNLIKELY(!checked_add(&tmp, a, b))) {
    *result = {};
    return false;
  }
  if (FOLLY_UNLIKELY(!checked_add(&tmp, tmp, c))) {
    *result = {};
    return false;
  }
  *result = tmp;
  return true;
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
bool checked_add(T* result, T a, T b, T c, T d) {
  T tmp{};
  if (FOLLY_UNLIKELY(!checked_add(&tmp, a, b))) {
    *result = {};
    return false;
  }
  if (FOLLY_UNLIKELY(!checked_add(&tmp, tmp, c))) {
    *result = {};
    return false;
  }
  if (FOLLY_UNLIKELY(!checked_add(&tmp, tmp, d))) {
    *result = {};
    return false;
  }
  *result = tmp;
  return true;
}

template <typename T>
bool checked_div(T* result, T dividend, T divisor) {
  if (FOLLY_UNLIKELY(divisor == 0)) {
    *result = {};
    return false;
  }
  *result = dividend / divisor;
  return true;
}

template <typename T>
bool checked_mod(T* result, T dividend, T divisor) {
  if (FOLLY_UNLIKELY(divisor == 0)) {
    *result = {};
    return false;
  }
  *result = dividend % divisor;
  return true;
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
bool checked_mul(T* result, T a, T b) {
  assert(result != nullptr);
#if FOLLY_HAS_BUILTIN(__builtin_mul_overflow)
  if (FOLLY_LIKELY(!__builtin_mul_overflow(a, b, result))) {
    return true;
  }
  *result = {};
  return false;
#elif _MSC_VER && FOLLY_X64
  static_assert(sizeof(T) <= sizeof(unsigned __int64), "Too large");
  if (sizeof(T) < sizeof(uint64_t)) {
    return detail::generic_checked_mul(result, a, b);
  } else {
    unsigned __int64 high;
    unsigned __int64 low = _umul128(a, b, &high);
    if (FOLLY_LIKELY(high == 0)) {
      *result = static_cast<T>(low);
      return true;
    }
    *result = {};
    return false;
  }
#else
  return detail::generic_checked_mul(result, a, b);
#endif
}

template <typename T, typename = std::enable_if_t<std::is_unsigned<T>::value>>
bool checked_muladd(T* result, T base, T mul, T add) {
  T tmp{};
  if (FOLLY_UNLIKELY(!checked_mul(&tmp, base, mul))) {
    *result = {};
    return false;
  }
  if (FOLLY_UNLIKELY(!checked_add(&tmp, tmp, add))) {
    *result = {};
    return false;
  }
  *result = tmp;
  return true;
}

template <
    typename T,
    typename T2,
    typename = std::enable_if_t<std::is_pointer<T>::value>,
    typename = std::enable_if_t<std::is_unsigned<T2>::value>>
bool checked_add(T* result, T a, T2 b) {
  size_t out = 0;
  bool ret = checked_muladd(
      &out,
      static_cast<size_t>(b),
      sizeof(std::remove_pointer_t<T>),
      reinterpret_cast<size_t>(a));

  *result = reinterpret_cast<T>(out);
  return ret;
}
} // namespace folly
