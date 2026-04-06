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

#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>

#include <folly/math/KahanSummation.h>

namespace folly {

namespace detail {

/// Maps a floating-point type to a wider type with more mantissa precision,
/// or to itself if no wider type is available.
///
/// Promotions are guarded by std::numeric_limits::digits comparisons, so
/// they are suppressed on platforms where the "wider" type does not actually
/// have more precision (e.g., MSVC or some ARM targets where
/// long double == double).
template <typename T>
struct wider_type {
  using type = T;
};

template <>
struct wider_type<float> {
  using type = std::conditional_t<
      (std::numeric_limits<double>::digits >
       std::numeric_limits<float>::digits),
      double,
      float>;
};

template <>
struct wider_type<double> {
  using type = std::conditional_t<
      (std::numeric_limits<long double>::digits >
       std::numeric_limits<double>::digits),
      long double,
      double>;
};

template <typename T>
using wider_type_t = typename wider_type<T>::type;

template <typename T>
inline constexpr bool has_wider_type_v = !std::is_same_v<T, wider_type_t<T>>;

/// Maximum element count for which naive wider-type summation is at least as
/// accurate as Kahan summation in the original type.
///
/// Derivation:
///   Naive wider-type error:  (n - 1) * eps_wide  * sum(|xi|)
///   Kahan same-type error:   2       * eps_narrow * sum(|xi|)
///
///   Wider type wins when (n - 1) * eps_wide < 2 * eps_narrow, i.e.:
///     n < 2 * eps_narrow / eps_wide + 1
///       = 2 * 2^(-narrow_digits) / 2^(-wide_digits) + 1
///       = 2^(wide_digits - narrow_digits + 1) + 1
///
///   Concrete values:
///     float  -> double      (53 - 24 = 29 extra bits):  2^30 + 1 ~= 1 billion
///     double -> long double (64 - 53 = 11 extra bits):  2^12 + 1 = 4097
///
/// We use half the theoretical maximum as a conservative margin.
template <typename T>
inline constexpr size_t accurate_sum_threshold = []() consteval {
  if constexpr (has_wider_type_v<T>) {
    constexpr int narrow_digits = std::numeric_limits<T>::digits;
    constexpr int wide_digits = std::numeric_limits<wider_type_t<T>>::digits;
    return size_t{1} << (wide_digits - narrow_digits);
  } else {
    return size_t{0};
  }
}();

} // namespace detail

/// Computes an accurate sum over an iterator range.
///
/// When the element count is available (sized sentinel) and small enough,
/// this accumulates in a wider floating-point type for speed. Otherwise,
/// it falls back to Kahan compensated summation.
///
/// @tparam I  An input iterator whose value type is a floating-point type.
/// @tparam S  A sentinel for I.
/// @param first  Iterator to the first element.
/// @param last   Sentinel past the last element.
/// @return       An accurately computed sum of all elements in [first, last).
template <std::input_iterator I, std::sentinel_for<I> S>
  requires std::floating_point<std::iter_value_t<I>>
std::iter_value_t<I> accurate_sum(I first, S last) {
  using T = std::iter_value_t<I>;

  if constexpr (detail::has_wider_type_v<T>) {
    if constexpr (std::sized_sentinel_for<S, I>) {
      auto const n = static_cast<size_t>(last - first);
      if (n <= detail::accurate_sum_threshold<T>) {
        using W = detail::wider_type_t<T>;
        W sum{};
        for (; first != last; ++first) {
          sum += static_cast<W>(*first);
        }
        return static_cast<T>(sum);
      }
    }
  }

  return kahan_sum(std::move(first), std::move(last));
}

/// Computes an accurate sum over a range.
///
/// When the range is sized and small enough, this accumulates in a wider
/// floating-point type for speed. Otherwise, it falls back to Kahan
/// compensated summation.
///
/// @tparam R  An input range whose value type is a floating-point type.
/// @param range  The range to sum.
/// @return       An accurately computed sum of all elements in the range.
template <std::ranges::input_range R>
  requires std::floating_point<std::ranges::range_value_t<R>>
std::ranges::range_value_t<R> accurate_sum(R&& range) {
  return accurate_sum(std::ranges::begin(range), std::ranges::end(range));
}

} // namespace folly
