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
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

#include <folly/Portability.h>

namespace folly {

/// numbers
///
/// mimic: std::numbers, C++20 (partial)
namespace numbers {

namespace detail {
template <typename T>
using enable_if_floating_t =
    std::enable_if_t<std::is_floating_point<T>::value, T>;
}

/// e_v
///
/// mimic: std::numbers::e_v, C++20
template <typename T>
FOLLY_INLINE_VARIABLE constexpr T e_v = detail::enable_if_floating_t<T>(
    2.71828182845904523536028747135266249775724709369995L);

/// ln2_v
///
/// mimic: std::numbers::ln2_v, C++20
template <typename T>
FOLLY_INLINE_VARIABLE constexpr T ln2_v = detail::enable_if_floating_t<T>(
    0.69314718055994530941723212145817656807550013436025L);

/// e
///
/// mimic: std::numbers::e, C++20
FOLLY_INLINE_VARIABLE constexpr double e = e_v<double>;

/// ln2
///
/// mimic: std::numbers::ln2, C++20
FOLLY_INLINE_VARIABLE constexpr double ln2 = ln2_v<double>;

} // namespace numbers

/// floating_point_integral_constant
///
/// Like std::integral_constant but for floating-point types holding integral
/// values representable in an integral type.
template <typename T, typename S, S Value>
struct floating_point_integral_constant {
  using value_type = T;
  static constexpr value_type value = static_cast<value_type>(Value);
  constexpr operator value_type() const noexcept { return value; }
  constexpr value_type operator()() const noexcept { return value; }
};
#if FOLLY_CPLUSPLUS < 201703L
template <typename T, typename S, S Value>
constexpr typename floating_point_integral_constant<T, S, Value>::value_type
    floating_point_integral_constant<T, S, Value>::value;
#endif

// TLDR: Prefer using operator< for ordering. And when
// a and b are equivalent objects, we return b to make
// sorting stable.
// See http://stepanovpapers.com/notes.pdf for details.
template <typename T, typename... Ts>
constexpr T constexpr_max(T a, Ts... ts) {
  T list[] = {ts..., a}; // 0-length arrays are illegal
  for (auto i = 0u; i < sizeof...(Ts); ++i) {
    a = list[i] < a ? a : list[i];
  }
  return a;
}

// When a and b are equivalent objects, we return a to
// make sorting stable.
template <typename T, typename... Ts>
constexpr T constexpr_min(T a, Ts... ts) {
  T list[] = {ts..., a}; // 0-length arrays are illegal
  for (auto i = 0u; i < sizeof...(Ts); ++i) {
    a = list[i] < a ? list[i] : a;
  }
  return a;
}

template <typename T, typename Less>
constexpr T const& constexpr_clamp(
    T const& v, T const& lo, T const& hi, Less less) {
  T const& a = less(v, lo) ? lo : v;
  T const& b = less(hi, a) ? hi : a;
  return b;
}
template <typename T>
constexpr T const& constexpr_clamp(T const& v, T const& lo, T const& hi) {
  return constexpr_clamp(v, lo, hi, std::less<T>{});
}

template <typename T>
constexpr bool constexpr_isnan(T const t) {
  return t != t; // NOLINT
}

namespace detail {

template <typename T, typename = void>
struct constexpr_abs_helper {};

template <typename T>
struct constexpr_abs_helper<
    T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static constexpr T go(T t) { return t < static_cast<T>(0) ? -t : t; }
};

template <typename T>
struct constexpr_abs_helper<
    T,
    typename std::enable_if<
        std::is_integral<T>::value && !std::is_same<T, bool>::value &&
        std::is_unsigned<T>::value>::type> {
  static constexpr T go(T t) { return t; }
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

/// constexpr_mult
///
/// Multiply two values, allowing for constexpr floating-pooint overflow to
/// infinity.
template <typename T>
constexpr T constexpr_mult(T const a, T const b) {
  using lim = std::numeric_limits<T>;
  if (constexpr_isnan(a) || constexpr_isnan(b)) {
    return constexpr_isnan(a) ? a : b;
  }
  if (std::is_floating_point<T>::value) {
    constexpr auto inf = lim::infinity();
    auto const ax = constexpr_abs(a);
    auto const bx = constexpr_abs(b);
    if ((ax == T(0) && bx == inf) || (bx == T(0) && ax == inf)) {
      return lim::quiet_NaN();
    }
    // floating-point multiplication overflow, ie where multiplication of two
    // finite values overflows to infinity of either sign, is not constexpr per
    // gcc
    // floating-point division overflow, ie where division of two finite values
    // overflows to infinity of either sign, is not constexpr per gcc
    // floating-point division by zero is not constexpr per any compiler, but we
    // use it in the checks for the other two conditions
    if (ax != inf && bx != inf && T(1) < bx && lim::max() / bx < ax) {
      auto const a_neg = static_cast<bool>(a < T(0));
      auto const b_neg = static_cast<bool>(b < T(0));
      auto const sign = a_neg == b_neg ? T(1) : T(-1);
      return sign * inf;
    }
  }
  return a * b;
}

/// constexpr_pow
///
/// Calculates the value of base raised to the exponent exp.
///
/// Supports only integral exponents.
///
/// mimic: std::pow (C++26)
template <
    typename T,
    typename E,
    std::enable_if_t<
        std::is_integral<E>::value && !std::is_same<E, bool>::value,
        int> = 0>
constexpr T constexpr_pow(T const base, E const exp) {
  if (std::is_floating_point<T>::value) {
    if (exp < E(0)) {
      return T(1) / constexpr_pow(base, -exp);
    }
    if (exp == E(0)) {
      return T(1);
    }
    if (constexpr_isnan(base)) {
      return base;
    }
  }
  assert(!(exp < E(0)) && "negative exponent with integral base");
  if (exp == E(0)) {
    return T(1);
  }
  if (exp == E(1)) {
    return base;
  }
  auto const div = constexpr_pow(base, exp / E(2));
  auto const rem = exp % E(2) == E(0) ? T(1) : base;
  return constexpr_mult(constexpr_mult(div, div), rem);
}

/// constexpr_find_last_set
///
/// Return the 1-based index of the most significant bit which is set.
/// For x > 0, constexpr_find_last_set(x) == 1 + floor(log2(x)).
template <typename T>
constexpr std::size_t constexpr_find_last_set(T const t) {
  using U = std::make_unsigned_t<T>;
  return t == T(0) ? 0 : 1 + constexpr_log2(static_cast<U>(t));
}

namespace detail {
template <typename U>
constexpr std::size_t constexpr_find_first_set_(
    std::size_t s, std::size_t a, U const u) {
  return s == 0 ? a
                : constexpr_find_first_set_(
                      s / 2, a + s * bool((u >> a) % (U(1) << s) == U(0)), u);
}
} // namespace detail

/// constexpr_find_first_set
///
/// Return the 1-based index of the least significant bit which is set.
/// For x > 0, the exponent in the largest power of two which does not divide x.
template <typename T>
constexpr std::size_t constexpr_find_first_set(T t) {
  using U = std::make_unsigned_t<T>;
  using size = std::integral_constant<std::size_t, sizeof(T) * 4>;
  return t == T(0)
      ? 0
      : 1 + detail::constexpr_find_first_set_(size{}, 0, static_cast<U>(t));
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
      ? T(constexpr_clamp(
          static_cast<M>(a) + static_cast<M>(b),
          static_cast<M>(L::min()),
          static_cast<M>(L::max()))) :
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
      ? T(constexpr_clamp(
          static_cast<M>(a) - static_cast<M>(b),
          static_cast<M>(L::min()),
          static_cast<M>(L::max()))) :
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

// clamp_cast<> provides sane numeric conversions from float point numbers to
// integral numbers, and between different types of integral numbers. It helps
// to avoid unexpected bugs introduced by bad conversion, and undefined behavior
// like overflow when casting float point numbers to integral numbers.
//
// When doing clamp_cast<Dst>(value), if `value` is in valid range of Dst,
// it will give correct result in Dst, equal to `value`.
//
// If `value` is outside the representable range of Dst, it will be clamped to
// MAX or MIN in Dst, instead of being undefined behavior.
//
// Float NaNs are converted to 0 in integral type.
//
// Here's some comparison with static_cast<>:
// (with FB-internal gcc-5-glibc-2.23 toolchain)
//
// static_cast<int32_t>(NaN) = 6
// clamp_cast<int32_t>(NaN) = 0
//
// static_cast<int32_t>(9999999999.0f) = -348639895
// clamp_cast<int32_t>(9999999999.0f) = 2147483647
//
// static_cast<int32_t>(2147483647.0f) = -348639895
// clamp_cast<int32_t>(2147483647.0f) = 2147483647
//
// static_cast<uint32_t>(4294967295.0f) = 0
// clamp_cast<uint32_t>(4294967295.0f) = 4294967295
//
// static_cast<uint32_t>(-1) = 4294967295
// clamp_cast<uint32_t>(-1) = 0
//
// static_cast<int16_t>(32768u) = -32768
// clamp_cast<int16_t>(32768u) = 32767

template <typename Dst, typename Src>
constexpr typename std::enable_if<std::is_integral<Src>::value, Dst>::type
constexpr_clamp_cast(Src src) {
  static_assert(
      std::is_integral<Dst>::value && sizeof(Dst) <= sizeof(int64_t),
      "constexpr_clamp_cast can only cast into integral type (up to 64bit)");

  using L = std::numeric_limits<Dst>;
  // clang-format off
  return
    // Check if Src and Dst have same signedness.
    std::is_signed<Src>::value == std::is_signed<Dst>::value
    ? (
      // Src and Dst have same signedness. If sizeof(Src) <= sizeof(Dst),
      // we can safely convert Src to Dst without any loss of accuracy.
      sizeof(Src) <= sizeof(Dst) ? Dst(src) :
      // If Src is larger in size, we need to clamp it to valid range in Dst.
      Dst(constexpr_clamp(src, Src(L::min()), Src(L::max()))))
    // Src and Dst have different signedness.
    // Check if it's signed -> unsigend cast.
    : std::is_signed<Src>::value && std::is_unsigned<Dst>::value
    ? (
      // If src < 0, the result should be 0.
      src < 0 ? Dst(0) :
      // Otherwise, src >= 0. If src can fit into Dst, we can safely cast it
      // without loss of accuracy.
      sizeof(Src) <= sizeof(Dst) ? Dst(src) :
      // If Src is larger in size than Dst, we need to ensure the result is
      // at most Dst MAX.
      Dst(constexpr_min(src, Src(L::max()))))
    // It's unsigned -> signed cast.
    : (
      // Since Src is unsigned, and Dst is signed, Src can fit into Dst only
      // when sizeof(Src) < sizeof(Dst).
      sizeof(Src) < sizeof(Dst) ? Dst(src) :
      // If Src does not fit into Dst, we need to ensure the result is at most
      // Dst MAX.
      Dst(constexpr_min(src, Src(L::max()))));
  // clang-format on
}

namespace detail {
// Upper/lower bound values that could be accurately represented in both
// integral and float point types.
constexpr double kClampCastLowerBoundDoubleToInt64F = -9223372036854774784.0;
constexpr double kClampCastUpperBoundDoubleToInt64F = 9223372036854774784.0;
constexpr double kClampCastUpperBoundDoubleToUInt64F = 18446744073709549568.0;

constexpr float kClampCastLowerBoundFloatToInt32F = -2147483520.0f;
constexpr float kClampCastUpperBoundFloatToInt32F = 2147483520.0f;
constexpr float kClampCastUpperBoundFloatToUInt32F = 4294967040.0f;

// This works the same as constexpr_clamp, but the comparison are done in Src
// to prevent any implicit promotions.
template <typename D, typename S>
constexpr D constexpr_clamp_cast_helper(S src, S sl, S su, D dl, D du) {
  return src < sl ? dl : (src > su ? du : D(src));
}
} // namespace detail

template <typename Dst, typename Src>
constexpr typename std::enable_if<std::is_floating_point<Src>::value, Dst>::type
constexpr_clamp_cast(Src src) {
  static_assert(
      std::is_integral<Dst>::value && sizeof(Dst) <= sizeof(int64_t),
      "constexpr_clamp_cast can only cast into integral type (up to 64bit)");

  using L = std::numeric_limits<Dst>;
  // clang-format off
  return
    // Special case: cast NaN into 0.
    constexpr_isnan(src) ? Dst(0) :
    // using `sizeof(Src) > sizeof(Dst)` as a heuristic that Dst can be
    // represented in Src without loss of accuracy.
    // see: https://en.wikipedia.org/wiki/Floating-point_arithmetic
    sizeof(Src) > sizeof(Dst) ?
      detail::constexpr_clamp_cast_helper(
          src, Src(L::min()), Src(L::max()), L::min(), L::max()) :
    // sizeof(Src) < sizeof(Dst) only happens when doing cast of
    // 32bit float -> u/int64_t.
    // Losslessly promote float into double, change into double -> u/int64_t.
    sizeof(Src) < sizeof(Dst) ? (
      src >= 0.0
      ? constexpr_clamp_cast<Dst>(
            constexpr_clamp_cast<std::uint64_t>(double(src)))
      : constexpr_clamp_cast<Dst>(
            constexpr_clamp_cast<std::int64_t>(double(src)))) :
    // The following are for sizeof(Src) == sizeof(Dst).
    std::is_same<Src, double>::value && std::is_same<Dst, int64_t>::value ?
      detail::constexpr_clamp_cast_helper(
          double(src),
          detail::kClampCastLowerBoundDoubleToInt64F,
          detail::kClampCastUpperBoundDoubleToInt64F,
          L::min(),
          L::max()) :
    std::is_same<Src, double>::value && std::is_same<Dst, uint64_t>::value ?
      detail::constexpr_clamp_cast_helper(
          double(src),
          0.0,
          detail::kClampCastUpperBoundDoubleToUInt64F,
          L::min(),
          L::max()) :
    std::is_same<Src, float>::value && std::is_same<Dst, int32_t>::value ?
      detail::constexpr_clamp_cast_helper(
          float(src),
          detail::kClampCastLowerBoundFloatToInt32F,
          detail::kClampCastUpperBoundFloatToInt32F,
          L::min(),
          L::max()) :
      detail::constexpr_clamp_cast_helper(
          float(src),
          0.0f,
          detail::kClampCastUpperBoundFloatToUInt32F,
          L::min(),
          L::max());
  // clang-format on
}

} // namespace folly
