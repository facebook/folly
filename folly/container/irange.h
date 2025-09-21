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
#include <concepts>
#include <ranges>
#include <type_traits>

namespace folly {

namespace detail {
template <typename T>
concept IRangeNonBoolIntegral = std::integral<T> && !std::same_as<T, bool>;
}

/// Creates an integer range for the half-open interval [begin, end)
/// If end<=begin, then the range is empty.
/// The range has the type of the `end` integer; `begin` integer is
/// cast to this type.
///
/// Instead of
///   for (int i = 3; i < 10; ++i) {
/// use
///   for (const auto i : irange(3, 10)) {
template <
    detail::IRangeNonBoolIntegral Integer1,
    detail::IRangeNonBoolIntegral Integer2>
constexpr std::ranges::iota_view<
    std::common_type_t<Integer1, Integer2>,
    std::common_type_t<Integer1, Integer2>>
irange(Integer1 begin, Integer2 end) {
  // If end<=begin then the range is empty; we can achieve this effect by
  // choosing the larger of {begin, end} as the loop terminator
  using otype = std::common_type_t<Integer1, Integer2>;
  return {
      static_cast<otype>(begin),
      std::max(static_cast<otype>(begin), static_cast<otype>(end))};
}

/// Creates an integer range for the half-open interval [0, end)
///
/// Instead of
///   for (int i = 0; i < 10; ++i) {
/// use
///   for (const auto i : irange(10)) {
///
/// NOTE! This behaviour differs from `iota_view(N)` which iterates
/// starts at `N` and loops over the full range of valus of `N`'s data
/// type forever.
template <detail::IRangeNonBoolIntegral Integer>
constexpr std::ranges::iota_view<Integer, Integer> irange(Integer end) {
  return {Integer(), end};
}

} // namespace folly
