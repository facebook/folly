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
#include <limits>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/lang/Assume.h>

namespace folly {

/// uint_division_result
///
/// The aggregate result of a division: both the quotient and the remainder.
///
/// @tparam Word  An unsigned integer type.
template <typename Word>
struct uint_division_result {
  /// value_type
  ///
  /// The word type held by this result.
  using value_type = Word;

  Word quotient;
  Word remainder;
};

/// uint_divisor
///
/// Accelerated division by a fixed divisor. Calculates quotient, remainder,
/// and divisibility.
///
/// Given a fixed divisor known ahead of time, this precomputes a magic
/// multiplier once at construction so that each subsequent division and
/// remainder is a handful of multiplies and shifts (inexpensive) rather than a
/// hardware division (expensive).
///
/// The technique works for any unsigned integer `Word` that has an unsigned
/// integer type of twice the width available to hold the intermediate product.
/// For `folly::uint128_t` (which has no double-width type) and for `uint64_t`
/// on platforms where there is no int128 support, `uint_divisor` is still
/// defined but falls back to native `/` and `%`.
///
/// A `uint_divisor` may be constructed speculatively and never used, so merely
/// constructing one with a zero divisor must not fail. Deferring failure until
/// an operation is invoked would require a failure path in every hot path
/// operation. For example, branch and `throw` or branch and `SIGFPE`. But the
/// design intention is to have fully-optimized hot paths. The alternatives are
/// either to support zero divisor or to declare it undefined behavior. But if
/// we declare it undefined behavior, we impose upon the caller to check, which
/// may affect hot paths as well.
///
/// Instead, zero divisor is supported everywhere exactly as if it were one. It
/// is defined behavior so that a caller which can tolerate this behavior can
/// avoid its own checks in the hot path.
///
/// In addition, there are special named operations which assume the divisor is
/// neither zero nor one. There are debug-build-only checks but in non-debug
/// builds these become compiler hints that allow the compiler to skip emission
/// of the zero/one special-case handling and generate faster code. Calling
/// these members in the zero divisor or one divisor case is forbidden and
/// undefined behavior.
///
/// Example:
///
///     folly::uint_divisor<uint32_t> divisor(7);
///     auto qr = divisor(100);
///     // qr.quotient == 14, qr.remainder == 2
///     divisor.div(100); // 14
///     divisor.rem(100); // 2
///     divisor.is_divisor_of(100); // false
///     100u / divisor;   // 14
///     100u % divisor;   // 2
///
/// From:
///   https://lemire.me/blog/2019/02/08/faster-remainders-when-the-divisor-is-a-constant-beating-compilers-and-libdivide/
///
/// CAUTION: Divisor Zero:
///
/// These properties hold whenever divisor != 0. But since the operations are
/// not mathematically defined operations when divisor == 0, these properties
/// are also undefined (neither hold nor do not hold) when divisor == 0.
///   quotient = dividend / divisor
///   remainder = dividend % divisor
///
/// In addition to these properties which are not defined when divisor == 0, we
/// have mathematical invariants where the mathematical operations are always
/// defined.
///
/// There is a fundamental inequality:
///   remainder < divisor
/// In its signed form it would be the more-familiar inequality:
///   |remainder| < |divisor|
///
/// This holds whenever divisor != 0, but fails when divisor == 0.
///
/// There is a fundamental equation:
///   dividend = divisor × quotient + remainder
///
/// This holds whenever divisor != 0. It also holds when both divisor == 0 and
/// dividend == 0. This can hold when divisor == 0 and dividend != 0, but it
/// depends on how we choose to define it. We could hypothetically define it so
/// that quotient = 0 and remainder = dividend. This would cause the fundamental
/// equation to hold in the divisor == 0 case. But this would require extra
/// checks in the hot paths. So we define the divisor == 0 case so that it
/// behaves like the divisor == 1 case: quotient = dividend and remainder = 0.
/// This causes the fundamental equation to fail in the case that divisor == 0
/// and dividend != 0. But it optimizes the implementation hot path.
///
/// Since the fundamental equation is violated in the case that divisor == 0 and
/// dividend != 0, caution is warranted.
template <typename Word>
class uint_divisor {
  static_assert(
      std::is_same_v<Word, folly::remove_cvref_t<Word>> &&
          folly::is_integral_v<Word> && folly::is_unsigned_v<Word> &&
          !std::is_same_v<Word, bool>,
      "Word must be an unsigned integer type");

 private:
  template <typename W>
  using wide_word_try = uint_bits_t<sizeof(W) * 8 * 2>;
  using wide_word = detected_or_t<Word, wide_word_try, Word>;
  static constexpr bool is_fast = !std::is_same_v<Word, wide_word>;

  // Cast the complement back to wide_word: for narrow types the bitwise
  // complement is subject to integer promotion to int, which would otherwise
  // yield a signed -1 rather than the all-ones value.
  static constexpr wide_word max_wide = static_cast<wide_word>(~wide_word(0));

  /// mulhi
  ///
  /// Returns the high `Word`-width bits of the product of the double-width `x`
  /// and the single-width `y`, i.e. `(x * y) >> (2 * width(Word))`, computed
  /// without a quadruple-width type.
  FOLLY_ERASE static constexpr Word mulhi(wide_word const x, Word const y) {
    constexpr int shift = std::numeric_limits<Word>::digits;
    wide_word const yd = y;
    wide_word const bottom =
        (static_cast<wide_word>(static_cast<Word>(x)) * yd) >> shift;
    wide_word const top = static_cast<wide_word>(x >> shift) * yd;
    return static_cast<Word>((bottom + top) >> shift);
  }

  // Zero and one have identical observable behavior and calculation state.
  FOLLY_ERASE static constexpr Word normalize_divisor(Word const divisor) {
    return static_cast<Word>(divisor | static_cast<Word>(divisor == 0));
  }

  struct calc_fast_base {
    FOLLY_ERASE static constexpr wide_word make_multiplier(Word const divisor) {
      return static_cast<wide_word>(max_wide / divisor + 1);
    }

    wide_word mult_{make_multiplier(Word(1))};

    FOLLY_ERASE constexpr Word fast_quotient(Word const dividend) const {
      Word const raw = mulhi(mult_, dividend);
      Word const use_dividend =
          static_cast<Word>(Word(0) - static_cast<Word>(mult_ == 0));
      return static_cast<Word>(
          (raw & ~use_dividend) | (dividend & use_dividend));
    }

    FOLLY_ERASE static constexpr Word fast_remainder(
        Word const dividend, Word const quotient, Word const divisor) {
      return static_cast<Word>(dividend - quotient * divisor);
    }

    FOLLY_ERASE constexpr Word fast_remainder(
        Word const dividend, Word const divisor) const {
      if constexpr (sizeof(Word) < sizeof(std::uint32_t)) {
        return mulhi(static_cast<wide_word>(mult_ * dividend), divisor);
      } else {
        return fast_remainder(dividend, fast_quotient(dividend), divisor);
      }
    }

    FOLLY_ERASE constexpr void assume_divisor_gt1() const {
      if (mult_ == 0) {
        folly::assume_unreachable();
      }
    }

    FOLLY_ERASE constexpr calc_fast_base() = default;
    FOLLY_ERASE constexpr explicit calc_fast_base(Word const divisor)
        : mult_{make_multiplier(divisor)} {}

    FOLLY_ERASE constexpr void assert_matching_divisor(
        [[maybe_unused]] Word const divisor) const {
      [[maybe_unused]] Word const reversed_divisor =
          mult_ == 0 ? Word(1) : static_cast<Word>(max_wide / mult_ + 1);
      assert(normalize_divisor(divisor) == reversed_divisor);
    }

    FOLLY_ERASE constexpr void assert_divisor_gt1(
        [[maybe_unused]] Word const divisor) const {
      assert(divisor > 1);
      assert_matching_divisor(divisor);
    }

    FOLLY_ERASE constexpr uint_division_result<Word> divrem_unchecked(
        Word const dividend, Word const divisor) const {
      Word const quotient = fast_quotient(dividend);
      return {quotient, fast_remainder(dividend, quotient, divisor)};
    }

    FOLLY_ERASE constexpr Word div_unchecked(
        Word const dividend, Word const) const {
      return fast_quotient(dividend);
    }

    FOLLY_ERASE constexpr Word rem_unchecked(
        Word const dividend, Word const divisor) const {
      return fast_remainder(dividend, divisor);
    }

    FOLLY_ERASE constexpr bool is_divisor_of_unchecked(
        Word const dividend, Word const) const {
      wide_word const product = static_cast<wide_word>(mult_ * dividend);
      return product <= static_cast<wide_word>(mult_ - 1);
    }

    FOLLY_ERASE constexpr uint_division_result<Word> divrem_gt1_unchecked(
        Word const dividend, Word const divisor) const {
      assume_divisor_gt1();
      return divrem_unchecked(dividend, divisor);
    }

    FOLLY_ERASE constexpr Word div_gt1_unchecked(
        Word const dividend, Word const divisor) const {
      assume_divisor_gt1();
      return div_unchecked(dividend, divisor);
    }

    FOLLY_ERASE constexpr Word rem_gt1_unchecked(
        Word const dividend, Word const divisor) const {
      assume_divisor_gt1();
      return rem_unchecked(dividend, divisor);
    }

    FOLLY_ERASE constexpr bool is_divisor_of_gt1_unchecked(
        Word const dividend, Word const divisor) const {
      assume_divisor_gt1();
      return is_divisor_of_unchecked(dividend, divisor);
    }
  };

  struct calc_slow_base {
    FOLLY_ERASE constexpr calc_slow_base() = default;
    FOLLY_ERASE constexpr explicit calc_slow_base(Word const) {}

    FOLLY_ERASE static constexpr void assert_matching_divisor(Word const) {}

    FOLLY_ERASE static constexpr void assert_divisor_gt1(
        [[maybe_unused]] Word const divisor) {
      assert(divisor > 1);
    }

    FOLLY_ERASE static constexpr uint_division_result<Word> divrem_unchecked(
        Word const dividend, Word const divisor) {
      return {
          static_cast<Word>(dividend / divisor),
          static_cast<Word>(dividend % divisor)};
    }

    FOLLY_ERASE static constexpr Word div_unchecked(
        Word const dividend, Word const divisor) {
      return static_cast<Word>(dividend / divisor);
    }

    FOLLY_ERASE static constexpr Word rem_unchecked(
        Word const dividend, Word const divisor) {
      return static_cast<Word>(dividend % divisor);
    }

    FOLLY_ERASE static constexpr bool is_divisor_of_unchecked(
        Word const dividend, Word const divisor) {
      return dividend % divisor == 0;
    }

    FOLLY_ERASE static constexpr uint_division_result<Word>
    divrem_gt1_unchecked(Word const dividend, Word const divisor) {
      return divrem_unchecked(dividend, divisor);
    }

    FOLLY_ERASE static constexpr Word div_gt1_unchecked(
        Word const dividend, Word const divisor) {
      return div_unchecked(dividend, divisor);
    }

    FOLLY_ERASE static constexpr Word rem_gt1_unchecked(
        Word const dividend, Word const divisor) {
      return rem_unchecked(dividend, divisor);
    }

    FOLLY_ERASE static constexpr bool is_divisor_of_gt1_unchecked(
        Word const dividend, Word const divisor) {
      return is_divisor_of_unchecked(dividend, divisor);
    }
  };

  using calc_base = std::conditional_t<is_fast, calc_fast_base, calc_slow_base>;

 public:
  /// value_type
  ///
  /// The word type this divisor operates on.
  using value_type = Word;

  /// result_type
  ///
  /// The aggregate quotient-and-remainder result type.
  using result_type = uint_division_result<Word>;

  /// calc
  ///
  /// The compact calculation state for a fixed divisor.
  ///
  /// Unlike `uint_divisor`, this class stores only the calculation state. The
  /// caller supplies the divisor to each operation, and debug builds verify
  /// that it matches the state computed by the constructor. When the
  /// reciprocal algorithm is unavailable, `calc` is empty and uses the
  /// supplied divisor directly.
  class calc : private calc_base {
   private:
    using base = calc_base;

    friend class uint_divisor;

   public:
    /// value_type
    ///
    /// The word type this calculator operates on.
    using value_type = Word;

    /// result_type
    ///
    /// The aggregate quotient-and-remainder result type.
    using result_type = uint_division_result<Word>;

    /// calc
    ///
    /// Constructs a calculator for a divisor of zero.
    constexpr calc() = default;

    /// calc
    ///
    /// Constructs a calculator for the given fixed divisor.
    explicit constexpr calc(Word const divisor)
        : base(normalize_divisor(divisor)) {}

    /// divrem
    ///
    /// Returns the quotient and remainder of `dividend / divisor`.
    ///
    /// If this instance was constructed with a value different from `divisor`,
    /// the behavior is undefined.
    constexpr result_type divrem(
        Word const dividend, Word const divisor) const {
      base::assert_matching_divisor(divisor);
      Word const normalized_divisor = normalize_divisor(divisor);
      return base::divrem_unchecked(dividend, normalized_divisor);
    }

    /// div
    ///
    /// Returns just the quotient of `dividend / divisor`.
    ///
    /// If this instance was constructed with a value different from `divisor`,
    /// the behavior is undefined.
    constexpr Word div(Word const dividend, Word const divisor) const {
      base::assert_matching_divisor(divisor);
      Word const normalized_divisor = normalize_divisor(divisor);
      return base::div_unchecked(dividend, normalized_divisor);
    }

    /// rem
    ///
    /// Returns just the remainder of `dividend / divisor`.
    ///
    /// If this instance was constructed with a value different from `divisor`,
    /// the behavior is undefined.
    constexpr Word rem(Word const dividend, Word const divisor) const {
      base::assert_matching_divisor(divisor);
      Word const normalized_divisor = normalize_divisor(divisor);
      return base::rem_unchecked(dividend, normalized_divisor);
    }

    /// is_divisor_of
    ///
    /// Returns whether `divisor` is a divisor of `dividend`. Equivalent to but
    /// possibly faster than `calc.rem(dividend, divisor) == 0`.
    ///
    /// If this instance was constructed with a value different from `divisor`,
    /// the behavior is undefined.
    constexpr bool is_divisor_of(
        Word const dividend, Word const divisor) const {
      base::assert_matching_divisor(divisor);
      Word const normalized_divisor = normalize_divisor(divisor);
      return base::is_divisor_of_unchecked(dividend, normalized_divisor);
    }

    /// divrem_gt1
    ///
    /// Returns the quotient and remainder of `dividend / divisor`, optimized
    /// for divisors greater than one.
    ///
    /// The behavior is undefined unless this instance was constructed with
    /// `divisor` and `divisor > 1`.
    constexpr result_type divrem_gt1(
        Word const dividend, Word const divisor) const {
      base::assert_divisor_gt1(divisor);
      return base::divrem_gt1_unchecked(dividend, divisor);
    }

    /// div_gt1
    ///
    /// Returns just the quotient of `dividend / divisor`, optimized for
    /// divisors greater than one.
    ///
    /// The behavior is undefined unless this instance was constructed with
    /// `divisor` and `divisor > 1`.
    constexpr Word div_gt1(Word const dividend, Word const divisor) const {
      base::assert_divisor_gt1(divisor);
      return base::div_gt1_unchecked(dividend, divisor);
    }

    /// rem_gt1
    ///
    /// Returns just the remainder of `dividend / divisor`, optimized for
    /// divisors greater than one.
    ///
    /// The behavior is undefined unless this instance was constructed with
    /// `divisor` and `divisor > 1`.
    constexpr Word rem_gt1(Word const dividend, Word const divisor) const {
      base::assert_divisor_gt1(divisor);
      return base::rem_gt1_unchecked(dividend, divisor);
    }

    /// is_divisor_of_gt1
    ///
    /// Returns whether `divisor` is a divisor of `dividend`, optimized for
    /// divisors greater than one. Equivalent to but possibly faster than
    /// `calc.rem_gt1(dividend, divisor) == 0`.
    ///
    /// The behavior is undefined unless this instance was constructed with
    /// `divisor` and `divisor > 1`.
    constexpr bool is_divisor_of_gt1(
        Word const dividend, Word const divisor) const {
      base::assert_divisor_gt1(divisor);
      return base::is_divisor_of_gt1_unchecked(dividend, divisor);
    }
  };

 private:
  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] calc calc_{};
  Word divisor_{1};

 public:
  /// uint_divisor
  ///
  /// Constructs a divider that divides by zero. Provided so that instances can
  /// be default-constructed and assigned later.
  constexpr uint_divisor() = default;

  /// uint_divisor
  ///
  /// Constructs a divider for the given fixed divisor.
  ///
  /// @param divisor  The divisor. A zero divisor is permitted.
  explicit constexpr uint_divisor(Word const divisor)
      : calc_{divisor}, divisor_{normalize_divisor(divisor)} {}

  /// operator()
  ///
  /// Returns the quotient and remainder of `dividend / divisor`.
  ///
  /// @param dividend  The value to divide.
  constexpr result_type operator()(Word const dividend) const {
    return divrem(dividend);
  }

  /// divrem
  ///
  /// Returns the quotient and remainder of `dividend / divisor`.
  constexpr result_type divrem(Word const dividend) const {
    result_type const result = calc_.divrem_unchecked(dividend, divisor_);
    assert(result.quotient == static_cast<Word>(dividend / divisor_));
    assert(result.remainder == static_cast<Word>(dividend % divisor_));
    return result;
  }

  /// div
  ///
  /// Returns just the quotient of `dividend / divisor`.
  constexpr Word div(Word const dividend) const {
    Word const quotient = calc_.div_unchecked(dividend, divisor_);
    assert(quotient == static_cast<Word>(dividend / divisor_));
    return quotient;
  }

  /// rem
  ///
  /// Returns just the remainder of `dividend / divisor`.
  constexpr Word rem(Word const dividend) const {
    Word const remainder = calc_.rem_unchecked(dividend, divisor_);
    assert(remainder == static_cast<Word>(dividend % divisor_));
    return remainder;
  }

  /// is_divisor_of
  ///
  /// Returns whether this divisor is a divisor of `dividend`. Equivalent to but
  /// possibly faster than `divisor.rem(dividend) == 0`.
  constexpr bool is_divisor_of(Word const dividend) const {
    bool const result = calc_.is_divisor_of_unchecked(dividend, divisor_);
    assert(result == (dividend % divisor_ == 0));
    return result;
  }

  /// divrem_gt1
  ///
  /// Returns the quotient and remainder, optimized for divisors greater than
  /// one.
  ///
  /// The behavior is undefined unless this instance was constructed with a
  /// divisor greater than one.
  constexpr result_type divrem_gt1(Word const dividend) const {
    assert(divisor_ > 1);
    result_type const result = calc_.divrem_gt1_unchecked(dividend, divisor_);
    assert(result.quotient == static_cast<Word>(dividend / divisor_));
    assert(result.remainder == static_cast<Word>(dividend % divisor_));
    return result;
  }

  /// div_gt1
  ///
  /// Returns just the quotient, optimized for divisors greater than one.
  ///
  /// The behavior is undefined unless this instance was constructed with a
  /// divisor greater than one.
  constexpr Word div_gt1(Word const dividend) const {
    assert(divisor_ > 1);
    Word const quotient = calc_.div_gt1_unchecked(dividend, divisor_);
    assert(quotient == static_cast<Word>(dividend / divisor_));
    return quotient;
  }

  /// rem_gt1
  ///
  /// Returns just the remainder, optimized for divisors greater than one.
  ///
  /// The behavior is undefined unless this instance was constructed with a
  /// divisor greater than one.
  constexpr Word rem_gt1(Word const dividend) const {
    assert(divisor_ > 1);
    Word const remainder = calc_.rem_gt1_unchecked(dividend, divisor_);
    assert(remainder == static_cast<Word>(dividend % divisor_));
    return remainder;
  }

  /// is_divisor_of_gt1
  ///
  /// Returns whether this divisor is a divisor of `dividend`, optimized for
  /// divisors greater than one. Equivalent to but possibly faster than
  /// `divisor.rem_gt1(dividend) == 0`.
  ///
  /// The behavior is undefined unless this instance was constructed with a
  /// divisor greater than one.
  constexpr bool is_divisor_of_gt1(Word const dividend) const {
    assert(divisor_ > 1);
    bool const result = calc_.is_divisor_of_gt1_unchecked(dividend, divisor_);
    assert(result == (dividend % divisor_ == 0));
    return result;
  }
};

/// operator/
///
/// Returns the quotient of `dividend / divisor`. Equivalent to
/// `divisor.div(dividend)`.
template <typename Word>
constexpr Word operator/(
    Word const dividend, uint_divisor<Word> const& divisor) {
  return divisor.div(dividend);
}

/// operator%
///
/// Returns the remainder of `dividend / divisor`. Equivalent to
/// `divisor.rem(dividend)`.
template <typename Word>
constexpr Word operator%(
    Word const dividend, uint_divisor<Word> const& divisor) {
  return divisor.rem(dividend);
}

/// operator/=
///
/// Replaces `dividend` with the quotient of `dividend / divisor` and returns
/// a reference to it.
template <typename Word>
constexpr Word& operator/=(Word& dividend, uint_divisor<Word> const& divisor) {
  return dividend = divisor.div(dividend);
}

/// operator%=
///
/// Replaces `dividend` with the remainder of `dividend / divisor` and returns
/// a reference to it.
template <typename Word>
constexpr Word& operator%=(Word& dividend, uint_divisor<Word> const& divisor) {
  return dividend = divisor.rem(dividend);
}

} // namespace folly
