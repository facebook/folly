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

#include <folly/math/Division.h>

#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>
#include <vector>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>

namespace {

template <typename T>
T randomWord(std::mt19937_64& rng) {
  if constexpr (sizeof(T) <= sizeof(std::uint64_t)) {
    return static_cast<T>(rng());
  } else {
    return static_cast<T>(
        (static_cast<T>(rng()) << 64) | static_cast<T>(rng()));
  }
}

template <typename T>
void expectMatchesNative(T const dividend, T const divisor) {
  T const nativeDivisor = divisor == 0 ? T{1} : divisor;
  auto const expectedQuotient = static_cast<T>(dividend / nativeDivisor);
  auto const expectedRemainder = static_cast<T>(dividend % nativeDivisor);
  bool const expectedIsDivisorOf = expectedRemainder == 0;

  folly::uint_divisor<T> const divider(divisor);
  auto const qr = divider.divrem(dividend);
  EXPECT_EQ(expectedQuotient, qr.quotient);
  EXPECT_EQ(expectedRemainder, qr.remainder);
  EXPECT_EQ(expectedQuotient, divider.div(dividend));
  EXPECT_EQ(expectedRemainder, divider.rem(dividend));
  EXPECT_EQ(expectedIsDivisorOf, divider.is_divisor_of(dividend));

  typename folly::uint_divisor<T>::calc const calculator(divisor);
  auto const calcQr = calculator.divrem(dividend, divisor);
  EXPECT_EQ(expectedQuotient, calcQr.quotient);
  EXPECT_EQ(expectedRemainder, calcQr.remainder);
  EXPECT_EQ(expectedQuotient, calculator.div(dividend, divisor));
  EXPECT_EQ(expectedRemainder, calculator.rem(dividend, divisor));
  EXPECT_EQ(expectedIsDivisorOf, calculator.is_divisor_of(dividend, divisor));

  if (divisor > 1) {
    auto const qrGt1 = divider.divrem_gt1(dividend);
    EXPECT_EQ(expectedQuotient, qrGt1.quotient);
    EXPECT_EQ(expectedRemainder, qrGt1.remainder);
    EXPECT_EQ(expectedQuotient, divider.div_gt1(dividend));
    EXPECT_EQ(expectedRemainder, divider.rem_gt1(dividend));
    EXPECT_EQ(expectedIsDivisorOf, divider.is_divisor_of_gt1(dividend));

    auto const calcQrGt1 = calculator.divrem_gt1(dividend, divisor);
    EXPECT_EQ(expectedQuotient, calcQrGt1.quotient);
    EXPECT_EQ(expectedRemainder, calcQrGt1.remainder);
    EXPECT_EQ(expectedQuotient, calculator.div_gt1(dividend, divisor));
    EXPECT_EQ(expectedRemainder, calculator.rem_gt1(dividend, divisor));
    EXPECT_EQ(
        expectedIsDivisorOf, calculator.is_divisor_of_gt1(dividend, divisor));
  }
}

} // namespace

template <typename T>
class UintDivisorTest : public ::testing::Test {
 protected:
  // Divisors spanning small values, powers of two, and values near the type
  // maximum, so both branches of the magic-constant round-trip are exercised.
  static std::vector<T> divisors() {
    std::vector<T> ds = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 100, 255};
    constexpr T max = std::numeric_limits<T>::max();
    for (T d : {max, T(max - 1), T(max / 2), T(max / 3), T(max / 7)}) {
      if (d != 0) {
        ds.push_back(d);
      }
    }
    return ds;
  }

  // Dividends spanning zero, small values, and values near the type maximum.
  static std::vector<T> dividends() {
    std::vector<T> ns = {0, 1, 2, 3, 42, 100, 255};
    constexpr T max = std::numeric_limits<T>::max();
    for (T n : {max, T(max - 1), T(max / 2), T(max - 3)}) {
      ns.push_back(n);
    }
    return ns;
  }
};

template <typename Bits>
using uint_bits_test_types = ::testing::Types<folly::uint_bits_t<Bits::value>>;

template <typename Bits>
using maybe_uint_bits_test_types =
    folly::detected_or_t<::testing::Types<>, uint_bits_test_types, Bits>;

template <std::size_t... Bits>
using detected_uint_types = folly::type_list_concat_t<
    ::testing::Types,
    maybe_uint_bits_test_types<folly::index_constant<Bits>>...>;

using UnsignedTypes = detected_uint_types<8, 16, 32, 64, 128>;
TYPED_TEST_SUITE(UintDivisorTest, UnsignedTypes);

TYPED_TEST(UintDivisorTest, MatchesNativeQuotientAndRemainder) {
  using T = TypeParam;
  for (T d : this->divisors()) {
    folly::uint_divisor<T> divisor(d);
    for (T n : this->dividends()) {
      auto qr = divisor(n);
      EXPECT_EQ(static_cast<T>(n / d), qr.quotient);
      EXPECT_EQ(static_cast<T>(n % d), qr.remainder);
    }
  }
}

TYPED_TEST(UintDivisorTest, CalculatorMatchesNativeQuotientAndRemainder) {
  using T = TypeParam;
  for (T d : this->divisors()) {
    typename folly::uint_divisor<T>::calc calculator(d);
    for (T n : this->dividends()) {
      auto qr = calculator.divrem(n, d);
      EXPECT_EQ(static_cast<T>(n / d), qr.quotient);
      EXPECT_EQ(static_cast<T>(n % d), qr.remainder);
      EXPECT_EQ(qr.quotient, calculator.div(n, d));
      EXPECT_EQ(qr.remainder, calculator.rem(n, d));
      EXPECT_EQ(qr.remainder == 0, calculator.is_divisor_of(n, d));
    }
  }
}

TEST(UintDivisorTest, ExhaustiveUint8MatchesNative) {
  using T = std::uint8_t;
  constexpr auto max = std::numeric_limits<T>::max();
  for (unsigned divisor = 0; divisor <= max; ++divisor) {
    for (unsigned dividend = 0; dividend <= max; ++dividend) {
      expectMatchesNative(static_cast<T>(dividend), static_cast<T>(divisor));
    }
  }
}

TYPED_TEST(UintDivisorTest, RandomizedMatchesNative) {
  using T = TypeParam;
  std::mt19937_64 rng(0x7e57d1a150ULL + sizeof(T));
  for (size_t i = 0; i < 20000; ++i) {
    T const divisor = randomWord<T>(rng);
    expectMatchesNative(randomWord<T>(rng), divisor);
  }
}

TYPED_TEST(UintDivisorTest, AccessorsAgree) {
  using T = TypeParam;
  for (T d : this->divisors()) {
    folly::uint_divisor<T> divisor(d);
    for (T n : this->dividends()) {
      auto qr = divisor(n);
      auto qr2 = divisor.divrem(n);
      EXPECT_EQ(qr.quotient, qr2.quotient);
      EXPECT_EQ(qr.remainder, qr2.remainder);
      EXPECT_EQ(qr.quotient, divisor.div(n));
      EXPECT_EQ(qr.remainder, divisor.rem(n));
      EXPECT_EQ(qr.remainder == 0, divisor.is_divisor_of(n));
    }
  }
}

TYPED_TEST(UintDivisorTest, OperatorsMatchAccessors) {
  using T = TypeParam;
  for (T d : this->divisors()) {
    folly::uint_divisor<T> divisor(d);
    for (T n : this->dividends()) {
      EXPECT_EQ(divisor.div(n), n / divisor);
      EXPECT_EQ(divisor.rem(n), n % divisor);

      T q = n;
      q /= divisor;
      EXPECT_EQ(divisor.div(n), q);

      T r = n;
      r %= divisor;
      EXPECT_EQ(divisor.rem(n), r);
    }
  }
}

TYPED_TEST(UintDivisorTest, DivideByOne) {
  using T = TypeParam;
  folly::uint_divisor<T> divisor(1);
  for (T n : this->dividends()) {
    auto qr = divisor(n);
    EXPECT_EQ(n, qr.quotient);
    EXPECT_EQ(T(0), qr.remainder);
    EXPECT_TRUE(divisor.is_divisor_of(n));
  }
}

// A zero divisor is supported in every code path and yields the dividend as
// the quotient and zero as the remainder, on both the optimized and the
// native-fallback path.
TYPED_TEST(UintDivisorTest, DivideByZero) {
  using T = TypeParam;
  folly::uint_divisor<T> divisor(0);
  for (T n : this->dividends()) {
    auto qr = divisor(n);
    EXPECT_EQ(n, qr.quotient);
    EXPECT_EQ(T(0), qr.remainder);
    EXPECT_EQ(n, n / divisor);
    EXPECT_EQ(T(0), n % divisor);
    EXPECT_TRUE(divisor.is_divisor_of(n));
  }
}

TYPED_TEST(UintDivisorTest, CalculatorDivideByZero) {
  using T = TypeParam;
  typename folly::uint_divisor<T>::calc calculator(0);
  for (T n : this->dividends()) {
    auto qr = calculator.divrem(n, 0);
    EXPECT_EQ(n, qr.quotient);
    EXPECT_EQ(T(0), qr.remainder);
    EXPECT_EQ(n, calculator.div(n, 0));
    EXPECT_EQ(T(0), calculator.rem(n, 0));
    EXPECT_TRUE(calculator.is_divisor_of(n, 0));
  }
}

TYPED_TEST(UintDivisorTest, ConstexprUsage) {
  using T = TypeParam;
  constexpr folly::uint_divisor<T> divisor(T{7});
  constexpr auto qr = divisor(T{100});
  static_assert(qr.quotient == T{14}, "quotient");
  static_assert(qr.remainder == T{2}, "remainder");
  static_assert(divisor.div(T{100}) == T{14}, "div");
  static_assert(divisor.rem(T{100}) == T{2}, "rem");
  static_assert(!divisor.is_divisor_of(T{100}), "is divisor of false");
  static_assert(divisor.is_divisor_of(T{98}), "is divisor of true");
  static_assert(
      divisor.divrem_gt1(T{100}).quotient == T{14}, "divrem gt1 quotient");
  static_assert(
      divisor.divrem_gt1(T{100}).remainder == T{2}, "divrem gt1 remainder");
  static_assert(divisor.div_gt1(T{100}) == T{14}, "div gt1");
  static_assert(divisor.rem_gt1(T{100}) == T{2}, "rem gt1");
  static_assert(!divisor.is_divisor_of_gt1(T{100}), "is divisor of gt1 false");
  static_assert(divisor.is_divisor_of_gt1(T{98}), "is divisor of gt1 true");
  static_assert(T{100} / divisor == T{14}, "operator/");
  static_assert(T{100} % divisor == T{2}, "operator%");

  constexpr folly::uint_divisor<T> divPow2(T{16});
  static_assert(divPow2.div(T{67}) == T{4}, "div pow2");
  static_assert(divPow2.rem(T{67}) == T{3}, "rem pow2");

  constexpr typename folly::uint_divisor<T>::calc calculator(T{7});
  constexpr auto calcQr = calculator.divrem(T{100}, T{7});
  static_assert(calcQr.quotient == T{14}, "calc quotient");
  static_assert(calcQr.remainder == T{2}, "calc remainder");
  static_assert(calculator.div(T{100}, T{7}) == T{14}, "calc div");
  static_assert(calculator.rem(T{100}, T{7}) == T{2}, "calc rem");
  static_assert(
      !calculator.is_divisor_of(T{100}, T{7}), "calc is divisor of false");
  static_assert(
      calculator.is_divisor_of(T{98}, T{7}), "calc is divisor of true");
  static_assert(
      calculator.divrem_gt1(T{100}, T{7}).quotient == T{14},
      "calc divrem gt1 quotient");
  static_assert(
      calculator.divrem_gt1(T{100}, T{7}).remainder == T{2},
      "calc divrem gt1 remainder");
  static_assert(calculator.div_gt1(T{100}, T{7}) == T{14}, "calc div gt1");
  static_assert(calculator.rem_gt1(T{100}, T{7}) == T{2}, "calc rem gt1");
  static_assert(
      !calculator.is_divisor_of_gt1(T{100}, T{7}),
      "calc is divisor of gt1 false");
  static_assert(
      calculator.is_divisor_of_gt1(T{98}, T{7}), "calc is divisor of gt1 true");

  EXPECT_EQ(T{14}, qr.quotient);
  EXPECT_EQ(T{2}, qr.remainder);
}

TYPED_TEST(UintDivisorTest, CalculatorStoresOnlyCalculationState) {
  using T = TypeParam;
  EXPECT_LE(sizeof(typename folly::uint_divisor<T>::calc), 2 * sizeof(T));
}

TYPED_TEST(UintDivisorTest, DefaultConstructedIsUsableAfterAssignment) {
  using T = TypeParam;
  // A default-constructed divider has divisor 0; reassigning gives it a
  // divisor.
  folly::uint_divisor<T> divisor;
  divisor = folly::uint_divisor<T>(T{9});
  EXPECT_EQ(T{11}, divisor.div(T{99}));
  EXPECT_EQ(T{0}, divisor.rem(T{99}));
}
