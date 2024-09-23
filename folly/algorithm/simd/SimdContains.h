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

#include <folly/algorithm/simd/detail/Traits.h>

#include <ranges>

namespace folly {
namespace simd_detail {

// no overloading for easier of profiling.

bool simdContainsU8(
    folly::span<const std::uint8_t> haystack, std::uint8_t needle);
bool simdContainsU16(
    folly::span<const std::uint16_t> haystack, std::uint16_t needle);
bool simdContainsU32(
    folly::span<const std::uint32_t> haystack, std::uint32_t needle);
bool simdContainsU64(
    folly::span<const std::uint64_t> haystack, std::uint64_t needle);

} // namespace simd_detail

struct simd_contains_fn {
  template <std::ranges::contiguous_range R>
    requires detail::has_integral_simd_friendly_equivalent<
        std::ranges::range_value_t<R>>
  bool operator()(R&& rng, std::ranges::range_value_t<R> x) const {
    auto castRng = detail::asSimdFriendlyUint(folly::span(rng));
    auto castX = detail::asSimdFriendlyUint(x);

    using T = decltype(castX);

    if constexpr (std::is_same_v<T, std::uint8_t>) {
      return simd_detail::simdContainsU8(castRng, castX);
    } else if constexpr (std::is_same_v<T, std::uint16_t>) {
      return simd_detail::simdContainsU16(castRng, castX);
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
      return simd_detail::simdContainsU32(castRng, castX);
    } else {
      static_assert(
          std::is_same_v<T, std::uint64_t>, "internal error, unknown type");
      return simd_detail::simdContainsU64(castRng, castX);
    }
  }
} inline constexpr simd_contains;

} // namespace folly
