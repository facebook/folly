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

#include <folly/Memory.h>
#include <folly/Traits.h>
#include <folly/container/span.h>

#include <concepts>
#include <type_traits>

namespace folly::detail {

template <typename T>
auto findSimdFriendlyEquivalent() {
  if constexpr (std::is_enum_v<T>) {
    return findSimdFriendlyEquivalent<std::underlying_type_t<T>>();
  } else if constexpr (std::is_floating_point_v<T>) {
    if constexpr (sizeof(T) == 4) {
      return float{};
    } else {
      return double{};
    }
  } else if constexpr (std::is_signed_v<T>) {
    if constexpr (sizeof(T) == 1) {
      return std::int8_t{};
    } else if constexpr (sizeof(T) == 2) {
      return std::int16_t{};
    } else if constexpr (sizeof(T) == 4) {
      return std::int32_t{};
    } else if constexpr (sizeof(T) == 8) {
      return std::int64_t{};
    }
  } else if constexpr (std::is_unsigned_v<T>) {
    if constexpr (sizeof(T) == 1) {
      return std::uint8_t{};
    } else if constexpr (sizeof(T) == 2) {
      return std::uint16_t{};
    } else if constexpr (sizeof(T) == 4) {
      return std::uint32_t{};
    } else if constexpr (sizeof(T) == 8) {
      return std::uint64_t{};
    }
  }
}

template <typename T>
concept has_simd_friendly_equivalent =
    !std::is_same_v<void, decltype(findSimdFriendlyEquivalent<T>())>;

template <has_simd_friendly_equivalent T>
using simd_friendly_equivalent_t = folly::like_t< //
    T,
    decltype(findSimdFriendlyEquivalent<std::remove_const_t<T>>())>;

template <typename T>
concept has_integral_simd_friendly_equivalent =
    has_simd_friendly_equivalent<T> && // have to explicitly specify this for
                                       // subsumption to work
    std::integral<simd_friendly_equivalent_t<T>>;

template <has_integral_simd_friendly_equivalent T>
using integral_simd_friendly_equivalent = simd_friendly_equivalent_t<T>;

template <has_simd_friendly_equivalent T, std::size_t Extend>
auto asSimdFriendly(folly::span<T, Extend> s) {
  return folly::reinterpret_span_cast<simd_friendly_equivalent_t<T>>(s);
}

template <has_simd_friendly_equivalent T>
constexpr auto asSimdFriendly(T x) {
  return static_cast<simd_friendly_equivalent_t<T>>(x);
}

template <has_simd_friendly_equivalent T, std::size_t Extend>
auto asSimdFriendlyUint(folly::span<T, Extend> s) {
  return folly::reinterpret_span_cast<
      folly::like_t<T, uint_bits_t<sizeof(T) * 8>>>(s);
}

template <has_simd_friendly_equivalent T>
constexpr auto asSimdFriendlyUint(T x) {
  return static_cast<uint_bits_t<sizeof(T) * 8>>(x);
}

} // namespace folly::detail
