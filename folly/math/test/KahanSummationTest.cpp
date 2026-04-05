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

#include <folly/math/KahanSummation.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <ranges>
#include <vector>

#include <folly/portability/GTest.h>

template <typename T>
class KahanSummationTest : public ::testing::Test {
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
  //
  // For double (eps ≈ 2.2e-16): a pair might be (1.8e15, -1.8e15 + 0.7).
  // The pair's true contribution to the sum is just 0.7, but after shuffling,
  // the running sum can reach ~1e20 where ULP ≈ 16, so each ±0.7
  // perturbation rounds away to nothing. For float the effect is even
  // stronger: values like (3.0e6, -3.0e6 + 1.5) produce perturbations of
  // 1.5 against a running sum of ~1e11 where ULP ≈ 16384.
  static_assert(1e20 + 1.8e15 + (-1.8e15 + 0.7) == 1e20);
  static_assert(1e11f + 3.0e6f + (-3.0e6f + 1.5f) == 1e11f);
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
TYPED_TEST_SUITE(KahanSummationTest, FloatTypes);

TYPED_TEST(KahanSummationTest, AccumulatorDefaultIsZero) {
  using T = TypeParam;
  folly::kahan_accumulator<T> acc;
  EXPECT_EQ(T(0), acc.value());
}

TYPED_TEST(KahanSummationTest, AccumulatorBasicSum) {
  using T = TypeParam;
  folly::kahan_accumulator<T> acc;
  acc += T(1);
  acc += T(2);
  acc += T(3);
  EXPECT_EQ(T(6), acc.value());
}

TYPED_TEST(KahanSummationTest, AccumulatorMinusEquals) {
  using T = TypeParam;
  folly::kahan_accumulator<T> acc;
  acc += T(10);
  acc -= T(3);
  EXPECT_EQ(T(7), acc.value());
}

// Kahan summation should produce O(eps) error on ill-conditioned data,
// verified against a high-precision reference sum in long double.
// The data is summed in generator order where (val, -val+δ) pairs are
// adjacent, so the running sum stays small. Even so, the condition number
// (Σ|xᵢ| / |Σxᵢ|) is large because individual values are O(1/ε).
TYPED_TEST(KahanSummationTest, AccuracyOnIllConditionedData) {
  using T = TypeParam;
  auto data = this->makeIllConditionedVector(50'000, 42);

  auto const to_ld = [](T x) { return static_cast<long double>(x); };
  long double const truth =
      folly::kahan_sum(data | std::views::transform(to_ld));

  T const result = folly::kahan_sum(data);
  T const error = std::abs(result - static_cast<T>(truth));

  T sumAbs = 0;
  for (auto const& x : data) {
    sumAbs += std::abs(x);
  }
  // Kahan error bound: (2u + (n+1)u²) · Σ|xᵢ|, where u = eps/2.
  // For practical n the leading 2u term dominates, so 3ε is conservative.
  T const bound = T(3) * std::numeric_limits<T>::epsilon() * sumAbs;
  EXPECT_LE(error, bound);
}

// The Neumaier variant handles the case where summands are much larger than
// the running sum — the key improvement over Kahan's original algorithm.
TYPED_TEST(KahanSummationTest, LargeSummandSmallRunningSum) {
  using T = TypeParam;
  // Start with a tiny sum, then add a large value followed by its negation
  // plus a small perturbation. Kahan's original algorithm may lose the
  // perturbation; the Neumaier variant preserves it.
  folly::kahan_accumulator<T> acc;
  acc += T(1);
  acc += T(1e15);
  acc += T(-1e15);
  // The exact answer is 1. The Neumaier variant recovers it.
  EXPECT_EQ(T(1), acc.value());
}

TYPED_TEST(KahanSummationTest, SumMatchesAccumulator) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 55);

  folly::kahan_accumulator<T> acc;
  for (auto const& x : data) {
    acc += x;
  }

  // The ILP path in kahan_sum reorders additions, so results may differ
  // by a few ULPs from serial accumulation. Both are O(eps)-accurate.
  T const serial = acc.value();
  T const batch = folly::kahan_sum(data);
  T const tol = std::numeric_limits<T>::epsilon() * std::abs(serial) * T(3);
  EXPECT_NEAR(serial, batch, tol);
}

TYPED_TEST(KahanSummationTest, SumMatchesAccumulatorSmall) {
  using T = TypeParam;
  // Below the ILP threshold, kahan_sum should match serial accumulation
  // exactly.
  auto data = this->makeRandomVector(64, T(-100), T(100), 55);

  folly::kahan_accumulator<T> acc;
  for (auto const& x : data) {
    acc += x;
  }

  EXPECT_EQ(acc.value(), folly::kahan_sum(data));
}

TYPED_TEST(KahanSummationTest, SumIteratorMatchesRange) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 66);

  T const iterResult = folly::kahan_sum(data.begin(), data.end());
  T const rangeResult = folly::kahan_sum(data);

  EXPECT_EQ(iterResult, rangeResult);
}

TYPED_TEST(KahanSummationTest, EmptyRangeReturnsZero) {
  using T = TypeParam;
  std::vector<T> empty;
  EXPECT_EQ(T(0), folly::kahan_sum(empty));
  EXPECT_EQ(T(0), folly::kahan_sum(empty.begin(), empty.end()));
}

TYPED_TEST(KahanSummationTest, SingleElement) {
  using T = TypeParam;
  std::vector<T> data = {T(42.5)};
  EXPECT_EQ(T(42.5), folly::kahan_sum(data));
}

TYPED_TEST(KahanSummationTest, TransformView) {
  using T = TypeParam;
  std::vector<int> ints = {1, 2, 3, 4, 5};
  auto transformed =
      ints | std::views::transform([](int i) -> T { return T(i) * T(2); });

  folly::kahan_accumulator<T> acc;
  for (int i : ints) {
    acc += T(i) * T(2);
  }

  EXPECT_EQ(acc.value(), folly::kahan_sum(transformed));
}

// Verify that the ill-conditioned data actually stresses naive summation:
// with value magnitudes scaled to 1/ε, naive summation loses nearly all
// significant digits while compensated summation remains accurate.
TYPED_TEST(KahanSummationTest, NaiveSummationFailsOnIllConditionedData) {
  using T = TypeParam;
  if constexpr (std::is_same_v<T, long double>) {
    GTEST_SKIP() << "No wider type for reference truth";
  }
  auto data = this->makeIllConditionedVector(50'000, 42);

  // Shuffle to break the paired structure — when (val, -val+δ) pairs are
  // adjacent, the accumulator stays small and naive summation works fine.
  // In practice data arrives in arbitrary order; shuffling exposes the
  // catastrophic cancellation that compensated summation handles.
  std::mt19937 rng(123);
  std::shuffle(data.begin(), data.end(), rng);

  auto const to_ld = [](T x) { return static_cast<long double>(x); };
  long double const truth =
      folly::kahan_sum(data | std::views::transform(to_ld));

  T naive = 0;
  for (auto x : data) {
    naive += x;
  }

  T const compensated = folly::kahan_sum(data);
  T const naiveErr = std::abs(naive - static_cast<T>(truth));
  T const compErr = std::abs(compensated - static_cast<T>(truth));
  EXPECT_GT(naiveErr, compErr * T(10));
}

// Hand-crafted analytically ill-conditioned input: v = {2/ε, 1, 1, ..., -2/ε}.
// True sum is exactly k (the number of ones). Naive summation returns 0
// because each +1 is below the ULP of 2/ε (which is 2) and gets rounded away.
// Compensated summation must recover the exact answer.
TYPED_TEST(KahanSummationTest, ExactRecoveryOnAnalyticalIllConditionedInput) {
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

  EXPECT_EQ(folly::kahan_sum(v), T(k));

  T naive = 0;
  for (auto x : v) {
    naive += x;
  }
  EXPECT_NE(naive, T(k));
}
