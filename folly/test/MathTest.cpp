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

#include <folly/Math.h>

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::detail;

namespace {

// Workaround for https://llvm.org/bugs/show_bug.cgi?id=16404,
// issues with __int128 multiplication and UBSAN
template <typename T>
T mul(T lhs, T rhs) {
  if (rhs < 0) {
    rhs = -rhs;
    lhs = -lhs;
  }
  T accum = 0;
  while (rhs != 0) {
    if ((rhs & 1) != 0) {
      accum += lhs;
    }
    lhs += lhs;
    rhs >>= 1;
  }
  return accum;
}

template <typename T, typename B>
T referenceDivFloor(T numer, T denom) {
  // rv = largest integral value <= numer / denom
  B n = numer;
  B d = denom;
  if (d < 0) {
    d = -d;
    n = -n;
  }
  B r = n / d;
  while (mul(r, d) > n) {
    --r;
  }
  while (mul(r + 1, d) <= n) {
    ++r;
  }
  T rv = static_cast<T>(r);
  assert(static_cast<B>(rv) == r);
  return rv;
}

template <typename T, typename B>
T referenceDivCeil(T numer, T denom) {
  // rv = smallest integral value >= numer / denom
  B n = numer;
  B d = denom;
  if (d < 0) {
    d = -d;
    n = -n;
  }
  B r = n / d;
  while (mul(r, d) < n) {
    ++r;
  }
  while (mul(r - 1, d) >= n) {
    --r;
  }
  T rv = static_cast<T>(r);
  assert(static_cast<B>(rv) == r);
  return rv;
}

template <typename T, typename B>
T referenceDivRoundAway(T numer, T denom) {
  if ((numer < 0) != (denom < 0)) {
    return referenceDivFloor<T, B>(numer, denom);
  } else {
    return referenceDivCeil<T, B>(numer, denom);
  }
}

template <typename T>
std::vector<T> cornerValues() {
  std::vector<T> rv;
  for (T i = 1; i < 24; ++i) {
    rv.push_back(i);
    rv.push_back(T(std::numeric_limits<T>::max() / i));
    rv.push_back(T(std::numeric_limits<T>::max() - i));
    rv.push_back(T(std::numeric_limits<T>::max() / T(2) - i));
    if (std::is_signed<T>::value) {
      rv.push_back(-i);
      rv.push_back(T(std::numeric_limits<T>::min() / i));
      rv.push_back(T(std::numeric_limits<T>::min() + i));
      rv.push_back(T(std::numeric_limits<T>::min() / T(2) + i));
    }
  }
  return rv;
}

template <typename A, typename B, typename C>
void runDivTests() {
  using T = decltype(static_cast<A>(1) / static_cast<B>(1));
  auto numers = cornerValues<A>();
  numers.push_back(0);
  auto denoms = cornerValues<B>();
  for (A n : numers) {
    for (B d : denoms) {
      if (std::is_signed<T>::value && n == std::numeric_limits<T>::min() &&
          d == static_cast<T>(-1)) {
        // n / d overflows in two's complement
        continue;
      }
      EXPECT_EQ(divCeil(n, d), (referenceDivCeil<T, C>(n, d))) << n << "/" << d;
      EXPECT_EQ(divFloor(n, d), (referenceDivFloor<T, C>(n, d)))
          << n << "/" << d;
      EXPECT_EQ(divTrunc(n, d), n / d) << n << "/" << d;
      EXPECT_EQ(divRoundAway(n, d), (referenceDivRoundAway<T, C>(n, d)))
          << n << "/" << d;
      T nn = n;
      T dd = d;
      EXPECT_EQ(divCeilBranchless(nn, dd), divCeilBranchful(nn, dd));
      EXPECT_EQ(divFloorBranchless(nn, dd), divFloorBranchful(nn, dd));
      EXPECT_EQ(divRoundAwayBranchless(nn, dd), divRoundAwayBranchful(nn, dd));
    }
  }
}
} // namespace

TEST(Bits, divTestInt8) {
  runDivTests<int8_t, int8_t, int64_t>();
  runDivTests<int8_t, uint8_t, int64_t>();
  runDivTests<int8_t, int16_t, int64_t>();
  runDivTests<int8_t, uint16_t, int64_t>();
  runDivTests<int8_t, int32_t, int64_t>();
  runDivTests<int8_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<int8_t, int64_t, __int128>();
  runDivTests<int8_t, uint64_t, __int128>();
#endif
}
TEST(Bits, divTestInt16) {
  runDivTests<int16_t, int8_t, int64_t>();
  runDivTests<int16_t, uint8_t, int64_t>();
  runDivTests<int16_t, int16_t, int64_t>();
  runDivTests<int16_t, uint16_t, int64_t>();
  runDivTests<int16_t, int32_t, int64_t>();
  runDivTests<int16_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<int16_t, int64_t, __int128>();
  runDivTests<int16_t, uint64_t, __int128>();
#endif
}
TEST(Bits, divTestInt32) {
  runDivTests<int32_t, int8_t, int64_t>();
  runDivTests<int32_t, uint8_t, int64_t>();
  runDivTests<int32_t, int16_t, int64_t>();
  runDivTests<int32_t, uint16_t, int64_t>();
  runDivTests<int32_t, int32_t, int64_t>();
  runDivTests<int32_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<int32_t, int64_t, __int128>();
  runDivTests<int32_t, uint64_t, __int128>();
#endif
}
#if FOLLY_HAVE_INT128_T
TEST(Bits, divTestInt64) {
  runDivTests<int64_t, int8_t, __int128>();
  runDivTests<int64_t, uint8_t, __int128>();
  runDivTests<int64_t, int16_t, __int128>();
  runDivTests<int64_t, uint16_t, __int128>();
  runDivTests<int64_t, int32_t, __int128>();
  runDivTests<int64_t, uint32_t, __int128>();
  runDivTests<int64_t, int64_t, __int128>();
  runDivTests<int64_t, uint64_t, __int128>();
}
#endif
TEST(Bits, divTestUint8) {
  runDivTests<uint8_t, int8_t, int64_t>();
  runDivTests<uint8_t, uint8_t, int64_t>();
  runDivTests<uint8_t, int16_t, int64_t>();
  runDivTests<uint8_t, uint16_t, int64_t>();
  runDivTests<uint8_t, int32_t, int64_t>();
  runDivTests<uint8_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<uint8_t, int64_t, __int128>();
  runDivTests<uint8_t, uint64_t, __int128>();
#endif
}
TEST(Bits, divTestUint16) {
  runDivTests<uint16_t, int8_t, int64_t>();
  runDivTests<uint16_t, uint8_t, int64_t>();
  runDivTests<uint16_t, int16_t, int64_t>();
  runDivTests<uint16_t, uint16_t, int64_t>();
  runDivTests<uint16_t, int32_t, int64_t>();
  runDivTests<uint16_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<uint16_t, int64_t, __int128>();
  runDivTests<uint16_t, uint64_t, __int128>();
#endif
}
TEST(Bits, divTestUint32) {
  runDivTests<uint32_t, int8_t, int64_t>();
  runDivTests<uint32_t, uint8_t, int64_t>();
  runDivTests<uint32_t, int16_t, int64_t>();
  runDivTests<uint32_t, uint16_t, int64_t>();
  runDivTests<uint32_t, int32_t, int64_t>();
  runDivTests<uint32_t, uint32_t, int64_t>();
#if FOLLY_HAVE_INT128_T
  runDivTests<uint32_t, int64_t, __int128>();
  runDivTests<uint32_t, uint64_t, __int128>();
#endif
}
#if FOLLY_HAVE_INT128_T
TEST(Bits, divTestUint64) {
  runDivTests<uint64_t, int8_t, __int128>();
  runDivTests<uint64_t, uint8_t, __int128>();
  runDivTests<uint64_t, int16_t, __int128>();
  runDivTests<uint64_t, uint16_t, __int128>();
  runDivTests<uint64_t, int32_t, __int128>();
  runDivTests<uint64_t, uint32_t, __int128>();
  runDivTests<uint64_t, int64_t, __int128>();
  runDivTests<uint64_t, uint64_t, __int128>();
}
#endif

FOLLY_CREATE_FREE_INVOKER(midpoint_invoke, midpoint);

TEST(MidpointTest, MidpointTest) {
  EXPECT_EQ(midpoint<int8_t>(2, 4), 3);
  EXPECT_EQ(midpoint<int8_t>(3, 4), 3);
  EXPECT_EQ(midpoint<int8_t>(-2, 2), 0);
  EXPECT_EQ(midpoint<int8_t>(-4, -2), -3);
  EXPECT_EQ(midpoint<int8_t>(102, 104), 103);
  EXPECT_EQ(midpoint<int8_t>(126, 126), 126);
  EXPECT_EQ(midpoint<int8_t>(-104, -102), -103);

  // Perform some simple tests. Note that because these are small integers
  // they can be represented exactly, so we do not have floating-point error.
  EXPECT_EQ(midpoint(2.0, 4.0), 3.0);
  EXPECT_EQ(midpoint(-2.0, 2.0), 0.0);
  EXPECT_EQ(midpoint(-2.1, 2.1), 0.0);
  EXPECT_EQ(midpoint(-4.0, -2.0), -3.0);
  EXPECT_EQ(midpoint(102.0, 104.0), 103.0);
  EXPECT_EQ(midpoint(-104.0, -102.0), -103.0);

  // Double
  EXPECT_EQ(midpoint(2.0, 4.0), 3.0);
  EXPECT_EQ(midpoint(0.0, 0.4), 0.2);
  EXPECT_EQ(midpoint(0.0, -0.0), 0.0);
  EXPECT_EQ(midpoint(9e9, -9e9), 0.0);
  EXPECT_EQ(
      midpoint(
          std::numeric_limits<double>::max(),
          std::numeric_limits<double>::max()),
      std::numeric_limits<double>::max());
  EXPECT_TRUE(std::isnan(midpoint(
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity())));

  // Float
  EXPECT_EQ(midpoint(2.0f, 4.0f), 3.0f);
  EXPECT_EQ(midpoint(0.0f, 0.4f), 0.2f);
  EXPECT_EQ(midpoint(0.0f, -0.0f), 0.0f);
  EXPECT_EQ(midpoint(9e9f, -9e9f), 0.0f);
  EXPECT_EQ(
      midpoint(
          std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
      std::numeric_limits<float>::max());
  EXPECT_TRUE(std::isnan(midpoint(
      -std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::infinity())));

  // Long double
  EXPECT_EQ(midpoint(2.0l, 4.0l), 3.0l);
  EXPECT_EQ(midpoint(0.0l, 0.4l), 0.2l);
  EXPECT_EQ(midpoint(0.0l, -0.0l), 0.0l);
  EXPECT_EQ(midpoint(9e9l, -9e9l), 0.0l);
  EXPECT_EQ(
      midpoint(
          std::numeric_limits<long double>::max(),
          std::numeric_limits<long double>::max()),
      std::numeric_limits<long double>::max());
  EXPECT_TRUE(std::isnan(midpoint(
      -std::numeric_limits<long double>::infinity(),
      std::numeric_limits<long double>::infinity())));

  EXPECT_TRUE(noexcept(midpoint(1, 2)));

  EXPECT_FALSE((is_invocable_v<midpoint_invoke, bool>));
  EXPECT_FALSE((is_invocable_v<midpoint_invoke, const bool>));
  EXPECT_FALSE((is_invocable_v<midpoint_invoke, volatile int>));

  constexpr auto MY_INT_MAX = std::numeric_limits<int>::max();
  constexpr auto MY_INT_MIN = std::numeric_limits<int>::min();
  constexpr auto MY_UINT_MAX = std::numeric_limits<unsigned int>::max();
  constexpr auto MY_SHRT_MAX = std::numeric_limits<short>::max();
  constexpr auto MY_SHRT_MIN = std::numeric_limits<short>::min();
  constexpr auto MY_SCHAR_MAX = std::numeric_limits<signed char>::max();
  constexpr auto MY_SCHAR_MIN = std::numeric_limits<signed char>::min();

  EXPECT_EQ(midpoint(0, 0), 0);
  EXPECT_EQ(midpoint(1, 1), 1);
  EXPECT_EQ(midpoint(0, 1), 0);
  EXPECT_EQ(midpoint(1, 0), 1);
  EXPECT_EQ(midpoint(0, 2), 1);
  EXPECT_EQ(midpoint(3, 2), 3);
  EXPECT_EQ(midpoint(-5, 4), -1);
  EXPECT_EQ(midpoint(5, -4), 1);
  EXPECT_EQ(midpoint(-5, -4), -5);
  EXPECT_EQ(midpoint(-4, -5), -4);
  EXPECT_EQ(midpoint(MY_INT_MIN, MY_INT_MAX), -1);
  EXPECT_EQ(midpoint(MY_INT_MAX, MY_INT_MIN), 0);
  EXPECT_EQ(midpoint(MY_INT_MAX, MY_INT_MAX), MY_INT_MAX);
  EXPECT_EQ(midpoint(MY_INT_MAX, MY_INT_MAX - 1), MY_INT_MAX);
  EXPECT_EQ(midpoint(MY_INT_MAX - 1, MY_INT_MAX - 1), MY_INT_MAX - 1);
  EXPECT_EQ(midpoint(MY_INT_MAX - 1, MY_INT_MAX), MY_INT_MAX - 1);
  EXPECT_EQ(midpoint(MY_INT_MAX, MY_INT_MAX - 2), MY_INT_MAX - 1);

  EXPECT_EQ(midpoint(0u, 0u), 0);
  EXPECT_EQ(midpoint(0u, 1u), 0);
  EXPECT_EQ(midpoint(1u, 0u), 1);
  EXPECT_EQ(midpoint(0u, 2u), 1);
  EXPECT_EQ(midpoint(3u, 2u), 3);
  EXPECT_EQ(midpoint(0u, MY_UINT_MAX), MY_UINT_MAX / 2);
  EXPECT_EQ(midpoint(MY_UINT_MAX, 0u), (MY_UINT_MAX / 2 + 1));
  EXPECT_EQ(midpoint(MY_UINT_MAX, MY_UINT_MAX), MY_UINT_MAX);
  EXPECT_EQ(midpoint(MY_UINT_MAX, MY_UINT_MAX - 1), MY_UINT_MAX);
  EXPECT_EQ(midpoint(MY_UINT_MAX - 1, MY_UINT_MAX - 1), MY_UINT_MAX - 1);
  EXPECT_EQ(midpoint(MY_UINT_MAX - 1, MY_UINT_MAX), MY_UINT_MAX - 1);
  EXPECT_EQ(midpoint(MY_UINT_MAX, MY_UINT_MAX - 2), MY_UINT_MAX - 1);

  EXPECT_EQ(midpoint<short>(0, 0), 0);
  EXPECT_EQ(midpoint<short>(0, 1), 0);
  EXPECT_EQ(midpoint<short>(1, 0), 1);
  EXPECT_EQ(midpoint<short>(0, 2), 1);
  EXPECT_EQ(midpoint<short>(3, 2), 3);
  EXPECT_EQ(midpoint<short>(-5, 4), -1);
  EXPECT_EQ(midpoint<short>(5, -4), 1);
  EXPECT_EQ(midpoint<short>(-5, -4), -5);
  EXPECT_EQ(midpoint<short>(-4, -5), -4);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MIN, MY_SHRT_MAX), -1);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX, MY_SHRT_MIN), 0);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX, MY_SHRT_MAX), MY_SHRT_MAX);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX, MY_SHRT_MAX - 1), MY_SHRT_MAX);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX - 1, MY_SHRT_MAX - 1), MY_SHRT_MAX - 1);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX - 1, MY_SHRT_MAX), MY_SHRT_MAX - 1);
  EXPECT_EQ(midpoint<short>(MY_SHRT_MAX, MY_SHRT_MAX - 2), MY_SHRT_MAX - 1);

  EXPECT_EQ(midpoint<signed char>(0, 0), 0);
  EXPECT_EQ(midpoint<signed char>(1, 1), 1);
  EXPECT_EQ(midpoint<signed char>(0, 1), 0);
  EXPECT_EQ(midpoint<signed char>(1, 0), 1);
  EXPECT_EQ(midpoint<signed char>(0, 2), 1);
  EXPECT_EQ(midpoint<signed char>(3, 2), 3);
  EXPECT_EQ(midpoint<signed char>(-5, 4), -1);
  EXPECT_EQ(midpoint<signed char>(5, -4), 1);
  EXPECT_EQ(midpoint<signed char>(-5, -4), -5);
  EXPECT_EQ(midpoint<signed char>(-4, -5), -4);
  EXPECT_EQ(midpoint<signed char>(MY_SCHAR_MIN, MY_SCHAR_MAX), -1);
  EXPECT_EQ(midpoint<signed char>(MY_SCHAR_MAX, MY_SCHAR_MIN), 0);
  EXPECT_EQ(midpoint<signed char>(MY_SCHAR_MAX, MY_SCHAR_MAX), MY_SCHAR_MAX);
  EXPECT_EQ(
      midpoint<signed char>(MY_SCHAR_MAX, MY_SCHAR_MAX - 1), MY_SCHAR_MAX);

  constexpr auto MY_SIZE_T_MAX = std::numeric_limits<size_t>::max();
  EXPECT_EQ(midpoint<size_t>(0, 0), 0);
  EXPECT_EQ(midpoint<size_t>(1, 1), 1);
  EXPECT_EQ(midpoint<size_t>(0, 1), 0);
  EXPECT_EQ(midpoint<size_t>(1, 0), 1);
  EXPECT_EQ(midpoint<size_t>(0, 2), 1);
  EXPECT_EQ(midpoint<size_t>(3, 2), 3);
  EXPECT_EQ(midpoint<size_t>((size_t)0, MY_SIZE_T_MAX), MY_SIZE_T_MAX / 2);
  EXPECT_EQ(
      midpoint<size_t>(MY_SIZE_T_MAX, (size_t)0), (MY_SIZE_T_MAX / 2 + 1));
  EXPECT_EQ(midpoint<size_t>(MY_SIZE_T_MAX, MY_SIZE_T_MAX), MY_SIZE_T_MAX);
  EXPECT_EQ(midpoint<size_t>(MY_SIZE_T_MAX, MY_SIZE_T_MAX - 1), MY_SIZE_T_MAX);
  EXPECT_EQ(
      midpoint<size_t>(MY_SIZE_T_MAX - 1, MY_SIZE_T_MAX - 1),
      MY_SIZE_T_MAX - 1);
  EXPECT_EQ(
      midpoint<size_t>(MY_SIZE_T_MAX - 1, MY_SIZE_T_MAX), MY_SIZE_T_MAX - 1);
  EXPECT_EQ(
      midpoint<size_t>(MY_SIZE_T_MAX, MY_SIZE_T_MAX - 2), MY_SIZE_T_MAX - 1);

#if FOLLY_HAVE_INT128_T
  const auto I128_MIN = std::numeric_limits<__int128_t>::min();
  const auto I128_MAX = std::numeric_limits<__int128_t>::max();
  EXPECT_EQ(midpoint<__int128_t>(0, 0), 0);
  EXPECT_EQ(midpoint<__int128_t>(1, 1), 1);
  EXPECT_EQ(midpoint<__int128_t>(0, 1), 0);
  EXPECT_EQ(midpoint<__int128_t>(1, 0), 1);
  EXPECT_EQ(midpoint<__int128_t>(0, 2), 1);
  EXPECT_EQ(midpoint<__int128_t>(3, 2), 3);
  EXPECT_EQ(midpoint<__int128_t>(-5, 4), -1);
  EXPECT_EQ(midpoint<__int128_t>(5, -4), 1);
  EXPECT_EQ(midpoint<__int128_t>(-5, -4), -5);
  EXPECT_EQ(midpoint<__int128_t>(-4, -5), -4);
  EXPECT_EQ(midpoint<__int128_t>(I128_MIN, I128_MAX), -1);
  EXPECT_EQ(midpoint<__int128_t>(I128_MAX, I128_MIN), 0);
  EXPECT_EQ(midpoint<__int128_t>(I128_MAX, I128_MAX), I128_MAX);
  EXPECT_EQ(midpoint<__int128_t>(I128_MAX, I128_MAX - 1), I128_MAX);
#endif

  // Test every possibility for signed char.
  for (int a = MY_SCHAR_MIN; a <= MY_SCHAR_MAX; ++a)
    for (int b = MY_SCHAR_MIN; b <= MY_SCHAR_MAX; ++b)
      EXPECT_EQ(midpoint(a, b), midpoint<int>(a, b));

  EXPECT_FALSE((is_invocable_v<midpoint_invoke, void>));
  EXPECT_FALSE((is_invocable_v<midpoint_invoke, int()>));
  EXPECT_FALSE((is_invocable_v<midpoint_invoke, int&>));

  constexpr std::array<int, 3> ca = {0, 1, 2};
  EXPECT_EQ(midpoint(ca.data(), ca.data() + 3), ca.data() + 1);

  constexpr std::array<int, 4> a = {0, 1, 2, 3};
  EXPECT_EQ(midpoint(a.data(), a.data()), a.data());
  EXPECT_EQ(midpoint(a.data(), a.data() + 1), a.data());
  EXPECT_EQ(midpoint(a.data(), a.data() + 2), a.data() + 1);
  EXPECT_EQ(midpoint(a.data(), a.data() + 3), a.data() + 1);
  EXPECT_EQ(midpoint(a.data(), a.data() + 4), a.data() + 2);
  EXPECT_EQ(midpoint(a.data() + 1, a.data()), a.data() + 1);
  EXPECT_EQ(midpoint(a.data() + 2, a.data()), a.data() + 1);
  EXPECT_EQ(midpoint(a.data() + 3, a.data()), a.data() + 2);
  EXPECT_EQ(midpoint(a.data() + 4, a.data()), a.data() + 2);
}
