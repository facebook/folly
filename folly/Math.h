/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

/**
 * Some arithmetic functions that seem to pop up or get hand-rolled a lot.
 * So far they are all focused on integer division.
 */

#pragma once

#include <stdint.h>

#include <cmath>
#include <limits>
#include <type_traits>

namespace folly {

namespace detail {

template <typename T>
inline constexpr T divFloorBranchless(T num, T denom) {
  // floor != trunc when the answer isn't exact and truncation went the
  // wrong way (truncation went toward positive infinity).  That happens
  // when the true answer is negative, which happens when num and denom
  // have different signs.  The following code compiles branch-free on
  // many platforms.
  return (num / denom) +
      ((num % denom) != 0 ? 1 : 0) *
      (std::is_signed<T>::value && (num ^ denom) < 0 ? -1 : 0);
}

template <typename T>
inline constexpr T divFloorBranchful(T num, T denom) {
  // First case handles negative result by preconditioning numerator.
  // Preconditioning decreases the magnitude of the numerator, which is
  // itself sign-dependent.  Second case handles zero or positive rational
  // result, where trunc and floor are the same.
  return std::is_signed<T>::value && (num ^ denom) < 0 && num != 0
      ? (num + (num > 0 ? -1 : 1)) / denom - 1
      : num / denom;
}

template <typename T>
inline constexpr T divCeilBranchless(T num, T denom) {
  // ceil != trunc when the answer isn't exact (truncation occurred)
  // and truncation went away from positive infinity.  That happens when
  // the true answer is positive, which happens when num and denom have
  // the same sign.
  return (num / denom) +
      ((num % denom) != 0 ? 1 : 0) *
      (std::is_signed<T>::value && (num ^ denom) < 0 ? 0 : 1);
}

template <typename T>
inline constexpr T divCeilBranchful(T num, T denom) {
  // First case handles negative or zero rational result, where trunc and ceil
  // are the same.
  // Second case handles positive result by preconditioning numerator.
  // Preconditioning decreases the magnitude of the numerator, which is
  // itself sign-dependent.
  return (std::is_signed<T>::value && (num ^ denom) < 0) || num == 0
      ? num / denom
      : (num + (num > 0 ? -1 : 1)) / denom + 1;
}

template <typename T>
inline constexpr T divRoundAwayBranchless(T num, T denom) {
  // away != trunc whenever truncation actually occurred, which is when
  // there is a non-zero remainder.  If the unrounded result is negative
  // then fixup moves it toward negative infinity.  If the unrounded
  // result is positive then adjustment makes it larger.
  return (num / denom) +
      ((num % denom) != 0 ? 1 : 0) *
      (std::is_signed<T>::value && (num ^ denom) < 0 ? -1 : 1);
}

template <typename T>
inline constexpr T divRoundAwayBranchful(T num, T denom) {
  // First case of second ternary operator handles negative rational
  // result, which is the same as divFloor.  Second case of second ternary
  // operator handles positive result, which is the same as divCeil.
  // Zero case is separated for simplicity.
  return num == 0 ? 0
                  : (num + (num > 0 ? -1 : 1)) / denom +
          (std::is_signed<T>::value && (num ^ denom) < 0 ? -1 : 1);
}

template <typename N, typename D>
using IdivResultType = typename std::enable_if<
    std::is_integral<N>::value && std::is_integral<D>::value &&
        !std::is_same<N, bool>::value && !std::is_same<D, bool>::value,
    decltype(N{1} / D{1})>::type;
} // namespace detail

#if defined(__arm__) && !FOLLY_AARCH64
constexpr auto kIntegerDivisionGivesRemainder = false;
#else
constexpr auto kIntegerDivisionGivesRemainder = true;
#endif

/**
 * Returns num/denom, rounded toward negative infinity.  Put another way,
 * returns the largest integral value that is less than or equal to the
 * exact (not rounded) fraction num/denom.
 *
 * The matching remainder (num - divFloor(num, denom) * denom) can be
 * negative only if denom is negative, unlike in truncating division.
 * Note that for unsigned types this is the same as the normal integer
 * division operator.  divFloor is equivalent to python's integral division
 * operator //.
 *
 * This function undergoes the same integer promotion rules as a
 * built-in operator, except that we don't allow bool -> int promotion.
 * This function is undefined if denom == 0.  It is also undefined if the
 * result type T is a signed type, num is std::numeric_limits<T>::min(),
 * and denom is equal to -1 after conversion to the result type.
 */
template <typename N, typename D>
inline constexpr detail::IdivResultType<N, D> divFloor(N num, D denom) {
  using R = decltype(num / denom);
  return detail::IdivResultType<N, D>(
      kIntegerDivisionGivesRemainder && std::is_signed<R>::value
          ? detail::divFloorBranchless<R>(num, denom)
          : detail::divFloorBranchful<R>(num, denom));
}

/**
 * Returns num/denom, rounded toward positive infinity.  Put another way,
 * returns the smallest integral value that is greater than or equal to
 * the exact (not rounded) fraction num/denom.
 *
 * This function undergoes the same integer promotion rules as a
 * built-in operator, except that we don't allow bool -> int promotion.
 * This function is undefined if denom == 0.  It is also undefined if the
 * result type T is a signed type, num is std::numeric_limits<T>::min(),
 * and denom is equal to -1 after conversion to the result type.
 */
template <typename N, typename D>
inline constexpr detail::IdivResultType<N, D> divCeil(N num, D denom) {
  using R = decltype(num / denom);
  return detail::IdivResultType<N, D>(
      kIntegerDivisionGivesRemainder && std::is_signed<R>::value
          ? detail::divCeilBranchless<R>(num, denom)
          : detail::divCeilBranchful<R>(num, denom));
}

/**
 * Returns num/denom, rounded toward zero.  If num and denom are non-zero
 * and have different signs (so the unrounded fraction num/denom is
 * negative), returns divCeil, otherwise returns divFloor.  If T is an
 * unsigned type then this is always equal to divFloor.
 *
 * Note that this is the same as the normal integer division operator,
 * at least since C99 (before then the rounding for negative results was
 * implementation defined).  This function is here for completeness and
 * as a place to hang this comment.
 *
 * This function undergoes the same integer promotion rules as a
 * built-in operator, except that we don't allow bool -> int promotion.
 * This function is undefined if denom == 0.  It is also undefined if the
 * result type T is a signed type, num is std::numeric_limits<T>::min(),
 * and denom is equal to -1 after conversion to the result type.
 */
template <typename N, typename D>
inline constexpr detail::IdivResultType<N, D> divTrunc(N num, D denom) {
  return detail::IdivResultType<N, D>(num / denom);
}

/**
 * Returns num/denom, rounded away from zero.  If num and denom are
 * non-zero and have different signs (so the unrounded fraction num/denom
 * is negative), returns divFloor, otherwise returns divCeil.  If T is
 * an unsigned type then this is always equal to divCeil.
 *
 * This function undergoes the same integer promotion rules as a
 * built-in operator, except that we don't allow bool -> int promotion.
 * This function is undefined if denom == 0.  It is also undefined if the
 * result type T is a signed type, num is std::numeric_limits<T>::min(),
 * and denom is equal to -1 after conversion to the result type.
 */
template <typename N, typename D>
inline constexpr detail::IdivResultType<N, D> divRoundAway(N num, D denom) {
  using R = decltype(num / denom);
  return detail::IdivResultType<N, D>(
      kIntegerDivisionGivesRemainder && std::is_signed<R>::value
          ? detail::divRoundAwayBranchless<R>(num, denom)
          : detail::divRoundAwayBranchful<R>(num, denom));
}

// clang-format off
// Disabling clang-formatting for midpoint to retain 1:1 correlation
// with LLVM

//  midpoint
//
//  mimic: std::numeric::midpoint, C++20
//  from:
//  https://github.com/llvm/llvm-project/blob/llvmorg-11.0.0/libcxx/include/numeric,
//  Apache 2.0 with LLVM exceptions

template <class _Tp>
constexpr std::enable_if_t<
    std::is_integral<_Tp>::value && !std::is_same<bool, _Tp>::value &&
        !std::is_null_pointer<_Tp>::value,
    _Tp>
midpoint(_Tp __a, _Tp __b) noexcept {
  using _Up = std::make_unsigned_t<_Tp>;
  constexpr _Up __bitshift = std::numeric_limits<_Up>::digits - 1;

  _Up __diff = _Up(__b) - _Up(__a);
  _Up __sign_bit = __b < __a;

  _Up __half_diff = (__diff / 2) + (__sign_bit << __bitshift) + (__sign_bit & __diff);

  return __a + __half_diff;
}

template <class _TPtr>
constexpr std::enable_if_t<
    std::is_pointer<_TPtr>::value &&
        std::is_object<std::remove_pointer_t<_TPtr>>::value &&
        !std::is_void<std::remove_pointer_t<_TPtr>>::value &&
        (sizeof(std::remove_pointer_t<_TPtr>) > 0),
    _TPtr>
midpoint(_TPtr __a, _TPtr __b) noexcept {
  return __a + midpoint(std::ptrdiff_t(0), __b - __a);
}

template <class _Fp>
constexpr std::enable_if_t<std::is_floating_point<_Fp>::value, _Fp> midpoint(
    _Fp __a,
    _Fp __b) noexcept {
  constexpr _Fp __lo = std::numeric_limits<_Fp>::min()*2;
  constexpr _Fp __hi = std::numeric_limits<_Fp>::max()/2;
  return std::abs(__a) <= __hi && std::abs(__b) <= __hi ?  // typical case: overflow is impossible
    (__a + __b)/2 :                                        // always correctly rounded
    std::abs(__a) < __lo ? __a + __b/2 :                   // not safe to halve a
    std::abs(__b) < __lo ? __a/2 + __b :                   // not safe to halve b
    __a/2 + __b/2;                                         // otherwise correctly rounded
}

// clang-format on

} // namespace folly
