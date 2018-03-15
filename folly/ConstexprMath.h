/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace folly {

// TLDR: Prefer using operator< for ordering. And when
// a and b are equivalent objects, we return b to make
// sorting stable.
// See http://stepanovpapers.com/notes.pdf for details.
template <typename T>
constexpr T constexpr_max(T a) {
  return a;
}
template <typename T, typename... Ts>
constexpr T constexpr_max(T a, T b, Ts... ts) {
  return b < a ? constexpr_max(a, ts...) : constexpr_max(b, ts...);
}

// When a and b are equivalent objects, we return a to
// make sorting stable.
template <typename T>
constexpr T constexpr_min(T a) {
  return a;
}
template <typename T, typename... Ts>
constexpr T constexpr_min(T a, T b, Ts... ts) {
  return b < a ? constexpr_min(b, ts...) : constexpr_min(a, ts...);
}

template <typename T, typename Less>
constexpr T const&
constexpr_clamp(T const& v, T const& lo, T const& hi, Less less) {
  return less(v, lo) ? lo : less(hi, v) ? hi : v;
}

template <typename T>
constexpr T const& constexpr_clamp(T const& v, T const& lo, T const& hi) {
  struct Less {
    constexpr bool operator()(T const& a, T const& b) const {
      return a < b;
    }
  };
  return constexpr_clamp(v, lo, hi, Less{});
}

namespace detail {

template <typename T, typename = void>
struct constexpr_abs_helper {};

template <typename T>
struct constexpr_abs_helper<
    T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static constexpr T go(T t) {
    return t < static_cast<T>(0) ? -t : t;
  }
};

template <typename T>
struct constexpr_abs_helper<
    T,
    typename std::enable_if<
        std::is_integral<T>::value && !std::is_same<T, bool>::value &&
        std::is_unsigned<T>::value>::type> {
  static constexpr T go(T t) {
    return t;
  }
};

template <typename T>
struct constexpr_abs_helper<
    T,
    typename std::enable_if<
        std::is_integral<T>::value && !std::is_same<T, bool>::value &&
        std::is_signed<T>::value>::type> {
  static constexpr typename std::make_unsigned<T>::type go(T t) {
    return typename std::make_unsigned<T>::type(t < static_cast<T>(0) ? -t : t);
  }
};
} // namespace detail

template <typename T>
constexpr auto constexpr_abs(T t)
    -> decltype(detail::constexpr_abs_helper<T>::go(t)) {
  return detail::constexpr_abs_helper<T>::go(t);
}

namespace detail {
template <typename T>
constexpr T constexpr_log2_(T a, T e) {
  return e == T(1) ? a : constexpr_log2_(a + T(1), e / T(2));
}

template <typename T>
constexpr T constexpr_log2_ceil_(T l2, T t) {
  return l2 + T(T(1) << l2 < t ? 1 : 0);
}

template <typename T>
constexpr T constexpr_square_(T t) {
  return t * t;
}
} // namespace detail

template <typename T>
constexpr T constexpr_log2(T t) {
  return detail::constexpr_log2_(T(0), t);
}

template <typename T>
constexpr T constexpr_log2_ceil(T t) {
  return detail::constexpr_log2_ceil_(constexpr_log2(t), t);
}

template <typename T>
constexpr T constexpr_ceil(T t, T round) {
  return round == T(0)
      ? t
      : ((t + (t < T(0) ? T(0) : round - T(1))) / round) * round;
}

template <typename T>
constexpr T constexpr_pow(T base, std::size_t exp) {
  return exp == 0
      ? T(1)
      : exp == 1 ? base
                 : detail::constexpr_square_(constexpr_pow(base, exp / 2)) *
              (exp % 2 ? base : T(1));
}

template <typename T>
constexpr T constexpr_add_overflow_clamped(T a, T b) {
  using L = std::numeric_limits<T>;
  using M = std::intmax_t;
  static_assert(
      !std::is_integral<T>::value || sizeof(T) <= sizeof(M),
      "Integral type too large!");
  // clang-format off
  return
    // don't do anything special for non-integral types.
    !std::is_integral<T>::value ? a + b :
    // for narrow integral types, just convert to intmax_t.
    sizeof(T) < sizeof(M)
      ? T(constexpr_clamp(M(a) + M(b), M(L::min()), M(L::max()))) :
    // when a >= 0, cannot add more than `MAX - a` onto a.
    !(a < 0) ? a + constexpr_min(b, T(L::max() - a)) :
    // a < 0 && b >= 0, `a + b` will always be in valid range of type T.
    !(b < 0) ? a + b :
    // a < 0 && b < 0, keep the result >= MIN.
               a + constexpr_max(b, T(L::min() - a));
  // clang-format on
}

template <typename T>
constexpr T constexpr_sub_overflow_clamped(T a, T b) {
  using L = std::numeric_limits<T>;
  using M = std::intmax_t;
  static_assert(
      !std::is_integral<T>::value || sizeof(T) <= sizeof(M),
      "Integral type too large!");
  // clang-format off
  return
    // don't do anything special for non-integral types.
    !std::is_integral<T>::value ? a - b :
    // for unsigned type, keep result >= 0.
    std::is_unsigned<T>::value ? (a < b ? 0 : a - b) :
    // for narrow signed integral types, just convert to intmax_t.
    sizeof(T) < sizeof(M)
      ? T(constexpr_clamp(M(a) - M(b), M(L::min()), M(L::max()))) :
    // (a >= 0 && b >= 0) || (a < 0 && b < 0), `a - b` will always be valid.
    (a < 0) == (b < 0) ? a - b :
    // MIN < b, so `-b` should be in valid range (-MAX <= -b <= MAX),
    // convert subtraction to addition.
    L::min() < b ? constexpr_add_overflow_clamped(a, T(-b)) :
    // -b = -MIN = (MAX + 1) and a <= -1, result is in valid range.
    a < 0 ? a - b :
    // -b = -MIN = (MAX + 1) and a >= 0, result > MAX.
            L::max();
  // clang-format on
}

} // namespace folly
