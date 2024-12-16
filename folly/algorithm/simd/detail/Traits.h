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

#include <folly/CPortability.h>
#include <folly/Memory.h>
#include <folly/Traits.h>
#include <folly/container/span.h>

#include <concepts>
#include <type_traits>

namespace folly::simd::detail {

template <typename T>
auto findSimdFriendlyEquivalent() {
  static_assert(std::is_same_v<T, remove_cvref_t<T>>);
  if constexpr (std::is_enum_v<T>) {
    return findSimdFriendlyEquivalent<std::underlying_type_t<T>>();
  } else if constexpr (std::is_pointer_v<T>) {
    // We use signed numbers for pointers because x86 support for signed
    // numbers is better and we can get away with it, in terms of correctness.
    return int_bits_t<sizeof(T) * 8>{};
  } else if constexpr (std::is_floating_point_v<T>) {
    if constexpr (sizeof(T) == 4) {
      return float{};
    } else {
      return double{};
    }
  } else if constexpr (std::is_signed_v<T>) {
    return int_bits_t<sizeof(T) * 8>{};
  } else if constexpr (std::is_unsigned_v<T>) {
    return uint_bits_t<sizeof(T) * 8>{};
  }
}

template <typename T>
constexpr bool has_simd_friendly_equivalent_scalar = !std::is_void_v<
    decltype(findSimdFriendlyEquivalent<std::remove_const_t<T>>())>;

template <typename T>
using simd_friendly_equivalent_scalar_t = std::enable_if_t<
    has_simd_friendly_equivalent_scalar<T>,
    like_t<T, decltype(findSimdFriendlyEquivalent<std::remove_const_t<T>>())>>;

template <typename T>
constexpr bool has_integral_simd_friendly_equivalent_scalar_v =
    std::is_integral_v< // void will return false
        decltype(findSimdFriendlyEquivalent<std::remove_const_t<T>>())>;

template <typename T>
using unsigned_simd_friendly_equivalent_scalar_t = std::enable_if_t<
    has_integral_simd_friendly_equivalent_scalar_v<T>,
    like_t<T, uint_bits_t<sizeof(T) * 8>>>;

template <typename R>
using span_for = decltype(folly::span(std::declval<const R&>()));

struct AsSimdFriendlyFn {
  template <typename T, std::size_t extent>
  FOLLY_ERASE auto operator()(folly::span<T, extent> s) const
      -> folly::span<simd_friendly_equivalent_scalar_t<T>, extent> {
    return reinterpret_span_cast<simd_friendly_equivalent_scalar_t<T>>(s);
  }

  template <typename R>
  FOLLY_ERASE auto operator()(R&& r) const
      -> decltype(operator()(span_for<R>(r))) {
    return operator()(folly::span(r));
  }

  template <typename T>
  FOLLY_ERASE constexpr auto operator()(T x) const
      -> simd_friendly_equivalent_scalar_t<T> {
    using res_t = simd_friendly_equivalent_scalar_t<T>;
    if constexpr (!std::is_pointer_v<T>) {
      return static_cast<res_t>(x);
    } else {
      return reinterpret_cast<res_t>(x);
    }
  }
};
inline constexpr AsSimdFriendlyFn asSimdFriendly;

struct AsSimdFriendlyUintFn {
  template <typename T, std::size_t extent>
  FOLLY_ERASE auto operator()(folly::span<T, extent> s) const
      -> folly::span<unsigned_simd_friendly_equivalent_scalar_t<T>, extent> {
    return reinterpret_span_cast<unsigned_simd_friendly_equivalent_scalar_t<T>>(
        s);
  }

  template <typename R>
  FOLLY_ERASE auto operator()(R&& r) const
      -> decltype(operator()(span_for<R>(r))) {
    return operator()(folly::span(r));
  }

  template <typename T>
  FOLLY_ERASE constexpr auto operator()(T x) const
      -> unsigned_simd_friendly_equivalent_scalar_t<T> {
    using res_t = unsigned_simd_friendly_equivalent_scalar_t<T>;
    if constexpr (!std::is_pointer_v<T>) {
      return static_cast<res_t>(x);
    } else {
      return reinterpret_cast<res_t>(x);
    }
  }
};
inline constexpr AsSimdFriendlyUintFn asSimdFriendlyUint;

} // namespace folly::simd::detail
