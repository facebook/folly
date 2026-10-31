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

#include <folly/Traits.h>
#include <folly/math/AccurateSummation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <ranges>
#include <vector>

#include <folly/math/KahanSummation.h>
#include <folly/portability/GTest.h>

template <typename T>
class AccurateSummationTest : public ::testing::Test {
 protected:
  static std::vector<T> makeRandomVector(size_t n, T lo, T hi, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<T> dist(lo, hi);
    std::vector<T> v(n);
    for (auto& x : v) {
      x = dist(gen);
    }
    return v;
  }

  // Pairs of large values that nearly cancel, leaving only small residuals.
  // The large-value range scales with 1/epsilon so that the condition number
  // (Σ|xᵢ| / |Σxᵢ|) grows with type precision, ensuring naive summation
  // loses nearly all significant digits regardless of whether T is float,
  // double, or long double.
  static std::vector<T> makeIllConditionedVector(size_t n, uint32_t seed) {
    std::mt19937 gen(seed);
    constexpr T eps = std::numeric_limits<T>::epsilon();
    std::uniform_real_distribution<T> large(T(0.25) / eps, T(0.5) / eps);
    std::uniform_real_distribution<T> perturb(T(-2), T(2));
    std::vector<T> v;
    v.reserve(2 * n);
    for (size_t i = 0; i < n; ++i) {
      T val = large(gen);
      v.push_back(val);
      v.push_back(-val + perturb(gen));
    }
    return v;
  }
};

using FloatTypes = ::testing::Types<float, double, long double>;
TYPED_TEST_SUITE(AccurateSummationTest, FloatTypes);

// NOTE: quadReferenceSum/referenceTruth below are mirrored in
// KahanSummationTest.cpp; keep the two copies in sync.

#if defined(__SIZEOF_FLOAT128__)
// Straightforward accumulation in IEEE-quad precision for reference truth.
// With 113 bits of mantissa the residual error is negligible versus the
// effects measured here, so plain summation suffices. __float128 is not
// std::floating_point, so folly::kahan_sum cannot take it; accumulate
// directly instead.
template <typename T>
__float128 quadReferenceSum(std::vector<T> const& data) {
  __float128 sum = 0;
  for (T x : data) {
    sum += static_cast<__float128>(x);
  }
  return sum;
}
#endif

// Reference truth for the accuracy comparisons below: compensated summation
// in long double, or IEEE-quad __float128 where T is already long double
// (long double is the widest hardware type, so it cannot serve as its own
// reference). Callers compile out the long double case on platforms without
// __float128 instead of skipping it at runtime.
template <typename T>
long double referenceTruth(std::vector<T> const& data) {
  if constexpr (std::is_same_v<T, long double>) {
#if defined(__SIZEOF_FLOAT128__)
    return static_cast<long double>(quadReferenceSum(data));
#else
    // Unreachable: the long double case is not instantiated on platforms
    // without __float128 (see the TruthTypes list above).
    return 0;
#endif
  } else {
    auto const to_ld = [](T x) { return static_cast<long double>(x); };
    return folly::kahan_sum(data | std::views::transform(to_ld));
  }
}

TYPED_TEST(AccurateSummationTest, SumIteratorMatchesRange) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 66);

  T const iterResult = folly::accurate_sum(data.begin(), data.end());
  T const rangeResult = folly::accurate_sum(data);

  EXPECT_EQ(iterResult, rangeResult);
}

TYPED_TEST(AccurateSummationTest, EmptyRangeReturnsZero) {
  using T = TypeParam;
  std::vector<T> empty;
  EXPECT_EQ(T(0), folly::accurate_sum(empty));
  EXPECT_EQ(T(0), folly::accurate_sum(empty.begin(), empty.end()));
}

TYPED_TEST(AccurateSummationTest, SingleElement) {
  using T = TypeParam;
  std::vector<T> data = {T(42.5)};
  EXPECT_EQ(T(42.5), folly::accurate_sum(data));
}

// accurate_sum should produce O(eps) error on ill-conditioned data,
// verified against a high-precision reference sum in long double.
TYPED_TEST(AccurateSummationTest, AccuracyOnIllConditionedData) {
  using T = TypeParam;
  auto data = this->makeIllConditionedVector(50'000, 42);

  auto const to_ld = [](T x) { return static_cast<long double>(x); };
  long double const truth =
      folly::kahan_sum(data | std::views::transform(to_ld));

  T const result = folly::accurate_sum(data);
  T const error = std::abs(result - static_cast<T>(truth));

  T sumAbs = 0;
  for (auto const& x : data) {
    sumAbs += std::abs(x);
  }
  // accurate_sum uses Kahan or wider-type accumulation, both with
  // error ≤ O(eps) · Σ|xᵢ|. 3ε is a conservative bound.
  T const bound = T(3) * std::numeric_limits<T>::epsilon() * sumAbs;
  EXPECT_LE(error, bound);
}

// accurate_sum (now using Neumaier internally) should be at least as accurate
// as Kahan. Skipped for long double where the reference truth is computed in
// long double via Kahan, making Kahan error trivially 0.
// Tests below compare against reference truth computed in a strictly wider
// type. long double has no wider hardware type, so the long double case
// exists only where IEEE-quad __float128 truth exists; otherwise the case
// does not exist at all instead of skipping at runtime.
#if defined(__SIZEOF_FLOAT128__)
constexpr bool kHasQuadReferenceTruth = true;
#else
// No floating-point type is wider than long double on this platform (e.g.
// MSVC, where long double == double).
constexpr bool kHasQuadReferenceTruth = false;
#endif

template <typename T>
class AccurateSummationTruthTest : public AccurateSummationTest<T> {};

// ::testing::Types is an alias, which can never name a template, so it
// cannot be the output of type_list_concat_t. Concatenate into
// folly::tag_t instead and convert back, so the public alias remains the
// only spelling of the list type itself.
template <typename List>
struct ToGtestTypes;
template <typename... Ts>
struct ToGtestTypes<folly::tag_t<Ts...>> {
  using type = ::testing::Types<Ts...>;
};
template <typename List>
using ToGtestTypesT = typename ToGtestTypes<List>::type;

using AccurateSummationTruthTypes = ToGtestTypesT<folly::type_list_concat_t<
    folly::tag_t,
    ::testing::Types<float, double>,
    std::conditional_t<
        kHasQuadReferenceTruth,
        ::testing::Types<long double>,
        ::testing::Types<>>>>;
TYPED_TEST_SUITE(AccurateSummationTruthTest, AccurateSummationTruthTypes);

TYPED_TEST(AccurateSummationTruthTest, AtLeastAsAccurateAsKahan) {
  using T = TypeParam;
  auto data = this->makeIllConditionedVector(50'000, 99);

  long double const truth = referenceTruth(data);

  T const accurateError =
      std::abs(folly::accurate_sum(data) - static_cast<T>(truth));
  T const kahanError = std::abs(folly::kahan_sum(data) - static_cast<T>(truth));

  // Note: for long double, accurate_sum falls back to kahan_sum (no wider
  // type for its fast path), so both errors are identical and this holds
  // trivially; it guards against future divergence.
  EXPECT_LE(accurateError, kahanError + std::numeric_limits<T>::epsilon())
      << "accurate_sum error " << accurateError << " exceeds Kahan error "
      << kahanError;
}

// Verify that the ill-conditioned data actually stresses naive summation:
// with value magnitudes scaled to 1/ε, naive summation loses nearly all
// significant digits while compensated summation remains accurate.
TYPED_TEST(
    AccurateSummationTruthTest, NaiveSummationFailsOnIllConditionedData) {
  using T = TypeParam;
  auto data = this->makeIllConditionedVector(50'000, 42);

  // Shuffle to break the paired structure — when (val, -val+δ) pairs are
  // adjacent, the accumulator stays small and naive summation works fine.
  // In practice data arrives in arbitrary order; shuffling exposes the
  // catastrophic cancellation that compensated summation handles.
  std::mt19937 rng(123);
  std::shuffle(data.begin(), data.end(), rng);

  long double const truth = referenceTruth(data);

  T naive = 0;
  for (auto x : data) {
    naive += x;
  }

  T const compensated = folly::accurate_sum(data);
  T const naiveErr = std::abs(naive - static_cast<T>(truth));
  T const compErr = std::abs(compensated - static_cast<T>(truth));
  EXPECT_GT(naiveErr, compErr * T(10));
}

// Hand-crafted analytically ill-conditioned input: v = {2/ε, 1, 1, ..., -2/ε}.
// True sum is exactly k (the number of ones). Naive summation returns 0
// because each +1 is below the ULP of 2/ε (which is 2) and gets rounded away.
// Compensated summation must recover the exact answer.
TYPED_TEST(
    AccurateSummationTest, ExactRecoveryOnAnalyticalIllConditionedInput) {
  using T = TypeParam;
  constexpr T eps = std::numeric_limits<T>::epsilon();
  constexpr T c = T(2) / eps;
  constexpr int k = 100;

  std::vector<T> v;
  v.reserve(k + 2);
  v.push_back(c);
  for (int i = 0; i < k; ++i) {
    v.push_back(T(1));
  }
  v.push_back(-c);

  EXPECT_EQ(folly::accurate_sum(v), T(k));

  T naive = 0;
  for (auto x : v) {
    naive += x;
  }
  EXPECT_NE(naive, T(k));
}
