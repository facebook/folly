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

#include <fmt/core.h>

#include <folly/math/ReproducibleAccumulator.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <ranges>
#include <vector>

#include <folly/BenchmarkUtil.h>
#include <folly/portability/GTest.h>

using folly::reproducible_accumulator;

template <typename T>
class ReproducibleAccumulatorTest : public ::testing::Test {
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
  static std::vector<T> makeIllConditionedVector(size_t n, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<T> large(T(1000), T(10000));
    std::uniform_real_distribution<T> perturb(T(-1), T(1));
    std::vector<T> v;
    v.reserve(2 * n);
    for (size_t i = 0; i < n; ++i) {
      T val = large(gen);
      v.push_back(val);
      v.push_back(-val + perturb(gen));
    }
    return v;
  }

  // Interleaves large and small values to stress precision retention.
  static std::vector<T> makeMixedMagnitudeVector(size_t n, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<T> big(T(1e4), T(1e5));
    std::uniform_real_distribution<T> small(T(-1e-4), T(1e-4));
    std::vector<T> v(n);
    for (size_t i = 0; i < n; ++i) {
      v[i] = (i % 10 == 0) ? big(gen) : small(gen);
    }
    return v;
  }
};

using FloatTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(ReproducibleAccumulatorTest, FloatTypes);

// Kahan compensated summation. makeUnpredictable prevents the compiler from
// substituting t with sum + y, which would defeat the compensation.
template <typename T>
T kahan_sum(std::vector<T> const& data) {
  T sum = 0;
  T c = 0;
  for (auto const& x : data) {
    T y = x - c;
    T t = sum + y;
    folly::makeUnpredictable(t);
    c = (t - sum) - y;
    sum = t;
  }
  return sum;
}

// High-precision reference sum using Kahan summation in long double.
template <typename T>
long double reference_sum(std::vector<T> const& data) {
  long double sum = 0;
  long double c = 0;
  for (auto const& x : data) {
    long double y = static_cast<long double>(x) - c;
    long double t = sum + y;
    folly::makeUnpredictable(t);
    c = (t - sum) - y;
    sum = t;
  }
  return sum;
}

// Naive summation in long double, cast back to T.
template <typename T>
T long_double_sum(std::vector<T> const& data) {
  long double sum = 0;
  for (auto const& x : data) {
    sum += static_cast<long double>(x);
  }
  return static_cast<T>(sum);
}

// Shuffling input order produces the same sum
TYPED_TEST(ReproducibleAccumulatorTest, ReproducibleAcrossShuffles) {
  using T = TypeParam;
  constexpr size_t N = 100'000;
  constexpr int kShuffles = 20;
  auto data = this->makeRandomVector(N, T(-1000), T(1000), 42);

  reproducible_accumulator<T> ref;
  for (auto const& x : data) {
    ref += x;
  }
  T const refVal = ref.value();

  std::mt19937 gen(123);
  for (int i = 0; i < kShuffles; ++i) {
    std::shuffle(data.begin(), data.end(), gen);
    reproducible_accumulator<T> rfa;
    for (auto const& x : data) {
      rfa += x;
    }
    EXPECT_EQ(refVal, rfa.value()) << "Mismatch on shuffle " << i;
  }
}

// Batch add (without cap) matches one-at-a-time
TYPED_TEST(ReproducibleAccumulatorTest, BatchAddMatchesSingle) {
  using T = TypeParam;
  auto data = this->makeRandomVector(50'000, T(-500), T(500), 99);

  reproducible_accumulator<T> single;
  for (auto const& x : data) {
    single += x;
  }

  reproducible_accumulator<T> batch;
  batch.add(data.begin(), data.end());

  EXPECT_EQ(single.value(), batch.value());
}

// Zero initialization
TYPED_TEST(ReproducibleAccumulatorTest, DefaultIsZero) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  EXPECT_EQ(T(0), rfa.value());
}

// Calling zero() resets the accumulator
TYPED_TEST(ReproducibleAccumulatorTest, ZeroResetsAccumulator) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(42);
  rfa.zero();
  EXPECT_EQ(T(0), rfa.value());
}

// Operator+= with scalar
TYPED_TEST(ReproducibleAccumulatorTest, PlusEqualsScalar) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(1);
  rfa += T(2);
  rfa += T(3);
  EXPECT_NEAR(T(6), rfa.value(), std::numeric_limits<T>::epsilon() * 10);
}

// Operator-= with scalar
TYPED_TEST(ReproducibleAccumulatorTest, MinusEqualsScalar) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(10);
  rfa -= T(3);
  EXPECT_NEAR(T(7), rfa.value(), std::numeric_limits<T>::epsilon() * 100);
}

// Operator= from scalar
TYPED_TEST(ReproducibleAccumulatorTest, AssignFromScalar) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa = T(42);
  EXPECT_NEAR(T(42), rfa.value(), std::numeric_limits<T>::epsilon() * 100);
}

// Equality and inequality operators
TYPED_TEST(ReproducibleAccumulatorTest, EqualityOperators) {
  using T = TypeParam;
  reproducible_accumulator<T> a, b;
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);

  a += T(1);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
}

// Unary negation
TYPED_TEST(ReproducibleAccumulatorTest, UnaryNegation) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(5);
  rfa += T(3);
  auto neg = -rfa;
  EXPECT_NEAR(
      -rfa.value(), neg.value(), std::numeric_limits<T>::epsilon() * 100);
}

// Adding two accumulators
TYPED_TEST(ReproducibleAccumulatorTest, AddAccumulators) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 55);

  reproducible_accumulator<T> all;
  for (auto const& x : data) {
    all += x;
  }

  reproducible_accumulator<T> first, second;
  for (size_t i = 0; i < data.size() / 2; ++i) {
    first += data[i];
  }
  for (size_t i = data.size() / 2; i < data.size(); ++i) {
    second += data[i];
  }
  first += second;

  EXPECT_NEAR(
      all.value(),
      first.value(),
      std::abs(all.value()) * std::numeric_limits<T>::epsilon() * 100);
}

// NaN propagation
TYPED_TEST(ReproducibleAccumulatorTest, NaNPropagation) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(1);
  rfa += std::numeric_limits<T>::quiet_NaN();
  EXPECT_TRUE(std::isnan(rfa.value()));
}

// Infinity handling
TYPED_TEST(ReproducibleAccumulatorTest, InfinityHandling) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(1);
  rfa += std::numeric_limits<T>::infinity();
  EXPECT_TRUE(std::isinf(rfa.value()));
  EXPECT_GT(rfa.value(), T(0));
}

// Negative infinity
TYPED_TEST(ReproducibleAccumulatorTest, NegativeInfinity) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(1);
  rfa += -std::numeric_limits<T>::infinity();
  EXPECT_TRUE(std::isinf(rfa.value()));
  EXPECT_LT(rfa.value(), T(0));
}

// Summing zeros gives zero
TYPED_TEST(ReproducibleAccumulatorTest, SumOfZeros) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  for (int i = 0; i < 1000; ++i) {
    rfa += T(0);
  }
  EXPECT_EQ(T(0), rfa.value());
}

// Error bound is non-negative and finite
TYPED_TEST(ReproducibleAccumulatorTest, ErrorBoundIsValid) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-1000), T(1000), 33);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }
  T const sum = rfa.value();
  T const bound =
      reproducible_accumulator<T>::error_bound(data.size(), T(1000), sum);

  EXPECT_GE(bound, T(0));
  EXPECT_FALSE(std::isnan(bound));
  EXPECT_FALSE(std::isinf(bound));
}

// Error bound actually bounds the error vs naive summation
TYPED_TEST(ReproducibleAccumulatorTest, ErrorBoundBoundsActualError) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-1000), T(1000), 44);

  long double const truth = reference_sum(data);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }
  T const sum = rfa.value();
  T const bound =
      reproducible_accumulator<T>::error_bound(data.size(), T(1000), sum);

  T const actualError = std::abs(static_cast<T>(truth) - sum);
  EXPECT_LE(actualError, bound);
}

// Range-based add matches iterator-pair add
TYPED_TEST(ReproducibleAccumulatorTest, AddPointerCount) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 66);

  reproducible_accumulator<T> iterBased;
  iterBased.add(data.begin(), data.end());

  reproducible_accumulator<T> rangeBased;
  rangeBased.add(data);

  EXPECT_EQ(iterBased.value(), rangeBased.value());
}

// Manual set_max_abs_val + unsafe_add + renorm matches safe add
TYPED_TEST(ReproducibleAccumulatorTest, ManualOpsMatchSafe) {
  using T = TypeParam;
  auto data = this->makeRandomVector(1'000, T(-50), T(50), 88);

  reproducible_accumulator<T> safe;
  for (auto const& x : data) {
    safe += x;
  }

  using RFA = reproducible_accumulator<T>;
  RFA manual;
  manual.set_max_abs_val(T(50));
  constexpr auto endurance = RFA::endurance();
  size_t count = 0;
  for (auto const& x : data) {
    manual.unsafe_add(x);
    if (++count == endurance) {
      manual.renorm();
      count = 0;
    }
  }
  manual.renorm();

  EXPECT_EQ(safe.value(), manual.value());
}

// Subtracting two accumulators
TYPED_TEST(ReproducibleAccumulatorTest, SubtractAccumulators) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 56);

  reproducible_accumulator<T> all;
  for (auto const& x : data) {
    all += x;
  }

  // Split into two halves, add both, then subtract the second half
  reproducible_accumulator<T> first, second;
  for (size_t i = 0; i < data.size() / 2; ++i) {
    first += data[i];
  }
  for (size_t i = data.size() / 2; i < data.size(); ++i) {
    second += data[i];
  }

  // (first + second) - second should equal first
  reproducible_accumulator<T> combined;
  for (auto const& x : data) {
    combined += x;
  }
  combined -= second;

  EXPECT_NEAR(
      first.value(),
      combined.value(),
      std::abs(first.value()) * std::numeric_limits<T>::epsilon() * 100);
}

// Batch add on empty range is a no-op
TYPED_TEST(ReproducibleAccumulatorTest, BatchAddEmptyRange) {
  using T = TypeParam;
  std::vector<T> empty;

  reproducible_accumulator<T> rfa;
  rfa += T(42);
  T const before = rfa.value();

  rfa.add(empty.begin(), empty.end());
  EXPECT_EQ(before, rfa.value());

  rfa.add(empty);
  EXPECT_EQ(before, rfa.value());
}

// Copy constructor produces an independent, equal copy
TYPED_TEST(ReproducibleAccumulatorTest, CopyConstructor) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(1);
  rfa += T(2);
  rfa += T(3);

  auto copy = rfa;
  EXPECT_EQ(rfa.value(), copy.value());
  EXPECT_TRUE(rfa == copy);

  // Mutating the copy doesn't affect the original
  copy += T(100);
  EXPECT_NE(rfa.value(), copy.value());
}

// Copy assignment produces an independent, equal copy
TYPED_TEST(ReproducibleAccumulatorTest, CopyAssignment) {
  using T = TypeParam;
  reproducible_accumulator<T> a, b;
  a += T(10);
  a += T(20);

  b = a;
  EXPECT_EQ(a.value(), b.value());
  EXPECT_TRUE(a == b);

  b += T(5);
  EXPECT_NE(a.value(), b.value());
}

// Batch add into a non-empty accumulator
TYPED_TEST(ReproducibleAccumulatorTest, BatchAddIntoNonEmpty) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 71);

  // One-at-a-time for all elements
  reproducible_accumulator<T> ref;
  for (auto const& x : data) {
    ref += x;
  }

  // First half one-at-a-time, second half via batch
  reproducible_accumulator<T> mixed;
  for (size_t i = 0; i < data.size() / 2; ++i) {
    mixed += data[i];
  }
  mixed.add(data.begin() + data.size() / 2, data.end());

  EXPECT_EQ(ref.value(), mixed.value());
}

// Very small (min-normal) values are handled correctly
TYPED_TEST(ReproducibleAccumulatorTest, SmallValues) {
  using T = TypeParam;
  T const tiny = std::numeric_limits<T>::min();

  reproducible_accumulator<T> rfa;
  for (int i = 0; i < 1000; ++i) {
    rfa += tiny;
  }

  EXPECT_GT(rfa.value(), T(0));
  EXPECT_NEAR(T(1000) * tiny, rfa.value(), T(1000) * tiny * T(0.01));
}

// Single-element add() method
TYPED_TEST(ReproducibleAccumulatorTest, SingleElementAdd) {
  using T = TypeParam;
  reproducible_accumulator<T> viaOperator;
  viaOperator += T(42);

  reproducible_accumulator<T> viaAdd;
  viaAdd.add(T(42));

  EXPECT_EQ(viaOperator.value(), viaAdd.value());
}

// Batch add with constant data matches one-at-a-time
TYPED_TEST(ReproducibleAccumulatorTest, BatchAddConstantData) {
  using T = TypeParam;
  constexpr size_t N = 1000;
  std::vector<T> data(N, T(100));

  reproducible_accumulator<T> single;
  for (auto const& x : data) {
    single += x;
  }

  reproducible_accumulator<T> batch;
  batch.add(data.begin(), data.end());

  EXPECT_EQ(single.value(), batch.value());
}

// Negation of a zero accumulator stays zero
TYPED_TEST(ReproducibleAccumulatorTest, NegateZero) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  auto neg = -rfa;
  EXPECT_EQ(T(0), neg.value());
}

// Double negation roundtrip: -(-x) == x
TYPED_TEST(ReproducibleAccumulatorTest, DoubleNegation) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-500), T(500), 91);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }

  auto roundtrip = -(-rfa);
  EXPECT_TRUE(rfa == roundtrip);
  EXPECT_EQ(rfa.value(), roundtrip.value());
}

// Opposing infinities produce NaN
TYPED_TEST(ReproducibleAccumulatorTest, OpposingInfinities) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += std::numeric_limits<T>::infinity();
  rfa += -std::numeric_limits<T>::infinity();
  EXPECT_TRUE(std::isnan(rfa.value()));
}

// Batch add is reproducible across shuffles
TYPED_TEST(ReproducibleAccumulatorTest, BatchAddReproducibleAcrossShuffles) {
  using T = TypeParam;
  constexpr size_t N = 50'000;
  constexpr int kShuffles = 10;
  auto data = this->makeRandomVector(N, T(-1000), T(1000), 73);

  reproducible_accumulator<T> ref;
  ref.add(data.begin(), data.end());
  T const refVal = ref.value();

  std::mt19937 gen(456);
  for (int i = 0; i < kShuffles; ++i) {
    std::shuffle(data.begin(), data.end(), gen);
    reproducible_accumulator<T> rfa;
    rfa.add(data.begin(), data.end());
    EXPECT_EQ(refVal, rfa.value()) << "Mismatch on shuffle " << i;
  }
}

// unsafe_add without exceeding endurance matches safe add
TYPED_TEST(ReproducibleAccumulatorTest, UnsafeAddWithinEndurance) {
  using T = TypeParam;
  using RFA = reproducible_accumulator<T>;
  constexpr auto endurance = RFA::endurance();

  auto data = this->makeRandomVector(endurance, T(-50), T(50), 101);

  RFA safe;
  for (auto const& x : data) {
    safe += x;
  }

  RFA manual;
  manual.set_max_abs_val(T(50));
  for (auto const& x : data) {
    manual.unsafe_add(x);
  }
  manual.renorm();

  EXPECT_EQ(safe.value(), manual.value());
}

// unsafe_add with multiple renorm cycles matches safe add
TYPED_TEST(ReproducibleAccumulatorTest, UnsafeAddMultipleCycles) {
  using T = TypeParam;
  using RFA = reproducible_accumulator<T>;
  constexpr auto endurance = RFA::endurance();

  auto data = this->makeRandomVector(endurance * 5, T(-200), T(200), 102);

  RFA safe;
  for (auto const& x : data) {
    safe += x;
  }

  RFA manual;
  manual.set_max_abs_val(T(200));
  size_t count = 0;
  for (auto const& x : data) {
    manual.unsafe_add(x);
    if (++count == endurance) {
      manual.renorm();
      count = 0;
    }
  }
  manual.renorm();

  EXPECT_EQ(safe.value(), manual.value());
}

// unsafe_add is reproducible across shuffles
TYPED_TEST(ReproducibleAccumulatorTest, UnsafeAddReproducibleAcrossShuffles) {
  using T = TypeParam;
  using RFA = reproducible_accumulator<T>;
  constexpr auto endurance = RFA::endurance();
  constexpr size_t N = 10'000;
  constexpr int kShuffles = 10;
  auto data = this->makeRandomVector(N, T(-100), T(100), 103);

  auto unsafe_sum = [&](std::vector<T> const& v) {
    RFA rfa;
    rfa.set_max_abs_val(T(100));
    size_t count = 0;
    for (auto const& x : v) {
      rfa.unsafe_add(x);
      if (++count == endurance) {
        rfa.renorm();
        count = 0;
      }
    }
    rfa.renorm();
    return rfa.value();
  };

  T const refVal = unsafe_sum(data);

  std::mt19937 gen(789);
  for (int i = 0; i < kShuffles; ++i) {
    std::shuffle(data.begin(), data.end(), gen);
    EXPECT_EQ(refVal, unsafe_sum(data)) << "Mismatch on shuffle " << i;
  }
}

// Non-default FOLD template parameter
TEST(ReproducibleAccumulatorFoldTest, Fold4Double) {
  constexpr size_t N = 50'000;
  std::mt19937 gen(42);
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
  std::vector<double> data(N);
  for (auto& x : data) {
    x = dist(gen);
  }

  reproducible_accumulator<double, 4> ref;
  for (auto const& x : data) {
    ref += x;
  }
  double const refVal = ref.value();

  std::shuffle(data.begin(), data.end(), gen);
  reproducible_accumulator<double, 4> rfa;
  for (auto const& x : data) {
    rfa += x;
  }
  EXPECT_EQ(refVal, rfa.value());
}

TEST(ReproducibleAccumulatorFoldTest, Fold2Float) {
  constexpr size_t N = 50'000;
  std::mt19937 gen(42);
  std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
  std::vector<float> data(N);
  for (auto& x : data) {
    x = dist(gen);
  }

  reproducible_accumulator<float, 2> ref;
  for (auto const& x : data) {
    ref += x;
  }
  float const refVal = ref.value();

  std::shuffle(data.begin(), data.end(), gen);
  reproducible_accumulator<float, 2> rfa;
  for (auto const& x : data) {
    rfa += x;
  }
  EXPECT_EQ(refVal, rfa.value());
}

// Explicit conversion operator
TYPED_TEST(ReproducibleAccumulatorTest, ExplicitConversion) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(7);
  rfa += T(3);

  EXPECT_EQ(static_cast<T>(rfa), rfa.value());
  EXPECT_EQ(T(rfa), rfa.value());

  reproducible_accumulator<T> rfa2;
  rfa2 += T(123.456);
  EXPECT_EQ(static_cast<T>(rfa2), rfa2.value());
  EXPECT_EQ(T(rfa2), rfa2.value());
}

// Implicit scalar constructor
TYPED_TEST(ReproducibleAccumulatorTest, ImplicitScalarConstructor) {
  using T = TypeParam;

  reproducible_accumulator<T> rfa = T(5.0);
  reproducible_accumulator<T> ref;
  ref += T(5.0);
  EXPECT_EQ(rfa.value(), ref.value());

  reproducible_accumulator<T> rfa_neg = T(-3.0);
  reproducible_accumulator<T> ref_neg;
  ref_neg += T(-3.0);
  EXPECT_EQ(rfa_neg.value(), ref_neg.value());
}

// C++20 ranges add()
TYPED_TEST(ReproducibleAccumulatorTest, RangesAdd) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 200);

  reproducible_accumulator<T> viaRange;
  viaRange.add(data);

  reproducible_accumulator<T> viaIter;
  viaIter.add(data.begin(), data.end());

  EXPECT_EQ(viaRange.value(), viaIter.value());

  std::vector<int> ints = {1, 2, 3, 4, 5};
  auto transformed = ints |
      std::ranges::views::transform([](int i) -> T { return T(i) * T(2); });

  reproducible_accumulator<T> viaView;
  viaView.add(transformed);

  reproducible_accumulator<T> viaManual;
  for (int i : ints) {
    viaManual += T(i) * T(2);
  }
  EXPECT_EQ(viaView.value(), viaManual.value());

  std::vector<T> empty;
  reproducible_accumulator<T> rfa2;
  rfa2 += T(42);
  T const before = rfa2.value();
  rfa2.add(empty);
  EXPECT_EQ(before, rfa2.value());
}

// Binary operator+
TYPED_TEST(ReproducibleAccumulatorTest, BinaryOperatorPlus) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 210);

  reproducible_accumulator<T> rfa1, rfa2;
  for (size_t i = 0; i < data.size() / 2; ++i) {
    rfa1 += data[i];
  }
  for (size_t i = data.size() / 2; i < data.size(); ++i) {
    rfa2 += data[i];
  }

  auto sum = rfa1 + rfa2;
  auto expected = rfa1;
  expected += rfa2;
  EXPECT_EQ(sum.value(), expected.value());

  reproducible_accumulator<T> a;
  a += T(10);
  auto aPlus3 = a + T(3.0);
  auto aExpected = a;
  aExpected += T(3.0);
  EXPECT_EQ(aPlus3.value(), aExpected.value());

  auto threePlusA = T(3.0) + a;
  EXPECT_EQ(aPlus3.value(), threePlusA.value());
}

// Binary operator-
TYPED_TEST(ReproducibleAccumulatorTest, BinaryOperatorMinus) {
  using T = TypeParam;
  auto data = this->makeRandomVector(5'000, T(-100), T(100), 220);

  reproducible_accumulator<T> rfa1, rfa2;
  for (size_t i = 0; i < data.size() / 2; ++i) {
    rfa1 += data[i];
  }
  for (size_t i = data.size() / 2; i < data.size(); ++i) {
    rfa2 += data[i];
  }

  auto diff = rfa1 - rfa2;
  auto expected = rfa1;
  expected -= rfa2;
  EXPECT_EQ(diff.value(), expected.value());

  reproducible_accumulator<T> a;
  a += T(10);
  auto aMinus3 = a - T(3.0);
  auto aExpected = a;
  aExpected -= T(3.0);
  EXPECT_EQ(aMinus3.value(), aExpected.value());

  auto threeMinusA = T(3.0) - a;
  auto negAMinus3 = -(a - T(3.0));
  EXPECT_NEAR(
      threeMinusA.value(),
      negAMinus3.value(),
      std::abs(threeMinusA.value()) * std::numeric_limits<T>::epsilon() * 100);
}

// fmt::formatter specialization
TYPED_TEST(ReproducibleAccumulatorTest, FmtFormatter) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa;
  rfa += T(3.14);
  rfa += T(2.72);

  EXPECT_EQ(fmt::format("{}", rfa), fmt::format("{}", rfa.value()));
  EXPECT_EQ(fmt::format("{:.6f}", rfa), fmt::format("{:.6f}", rfa.value()));
}

// Accuracy vs Kahan and long double: uniform random data
TYPED_TEST(ReproducibleAccumulatorTest, AccuracyVsKahanAndLongDoubleUniform) {
  using T = TypeParam;
  constexpr size_t N = 100'000;
  auto data = this->makeRandomVector(N, T(-1000), T(1000), 501);

  long double const truth = reference_sum(data);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }

  T const reproError = std::abs(rfa.value() - static_cast<T>(truth));
  T const kahanError = std::abs(kahan_sum(data) - static_cast<T>(truth));
  T const ldError = std::abs(long_double_sum(data) - static_cast<T>(truth));

  EXPECT_LE(reproError, kahanError)
      << "reproducible_accumulator error " << reproError
      << " exceeds Kahan error " << kahanError;
  EXPECT_LE(reproError, ldError)
      << "reproducible_accumulator error " << reproError
      << " exceeds long double error " << ldError;
}

// Accuracy vs Kahan and long double: ill-conditioned data (large cancellation)
TYPED_TEST(
    ReproducibleAccumulatorTest, AccuracyVsKahanAndLongDoubleIllConditioned) {
  using T = TypeParam;
  constexpr size_t N = 50'000;
  auto data = this->makeIllConditionedVector(N, 502);

  long double const truth = reference_sum(data);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }

  T const reproError = std::abs(rfa.value() - static_cast<T>(truth));
  T const kahanError = std::abs(kahan_sum(data) - static_cast<T>(truth));
  T const ldError = std::abs(long_double_sum(data) - static_cast<T>(truth));

  EXPECT_LE(reproError, kahanError)
      << "reproducible_accumulator error " << reproError
      << " exceeds Kahan error " << kahanError;
  EXPECT_LE(reproError, ldError)
      << "reproducible_accumulator error " << reproError
      << " exceeds long-double error " << ldError;
}

// Accuracy vs Kahan and long double: mixed magnitudes
TYPED_TEST(
    ReproducibleAccumulatorTest, AccuracyVsKahanAndLongDoubleMixedMagnitude) {
  using T = TypeParam;
  constexpr size_t N = 100'000;
  auto data = this->makeMixedMagnitudeVector(N, 503);

  long double const truth = reference_sum(data);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }

  T const reproError = std::abs(rfa.value() - static_cast<T>(truth));
  T const kahanError = std::abs(kahan_sum(data) - static_cast<T>(truth));
  T const ldError = std::abs(long_double_sum(data) - static_cast<T>(truth));

  EXPECT_LE(reproError, kahanError)
      << "reproducible_accumulator error " << reproError
      << " exceeds Kahan error " << kahanError;
  EXPECT_LE(reproError, ldError)
      << "reproducible_accumulator error " << reproError
      << " exceeds long double error " << ldError;
}

// Lazy renorm: many one-at-a-time deposits spanning multiple renorm cycles
// must produce the same result as batch add.
TYPED_TEST(ReproducibleAccumulatorTest, LazyRenormAcrossMultipleCycles) {
  using T = TypeParam;
  using RFA = reproducible_accumulator<T>;
  constexpr size_t N = RFA::endurance() * 5 + 7;
  auto data = this->makeRandomVector(N, T(-100), T(100), 400);

  RFA one_at_a_time;
  for (auto const& x : data) {
    one_at_a_time += x;
  }

  RFA batch;
  batch.add(data.begin(), data.end());

  EXPECT_EQ(one_at_a_time.value(), batch.value());
}

// Pending deposits are flushed correctly before accumulator merge.
TYPED_TEST(ReproducibleAccumulatorTest, FlushBeforeMerge) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 401);

  reproducible_accumulator<T> ref;
  for (auto const& x : data) {
    ref += x;
  }

  // Split with pending (unflushed) deposits in both halves
  reproducible_accumulator<T> a, b;
  size_t half = data.size() / 2;
  for (size_t i = 0; i < half; ++i) {
    a += data[i];
  }
  for (size_t i = half; i < data.size(); ++i) {
    b += data[i];
  }
  a += b;

  EXPECT_NEAR(
      ref.value(),
      a.value(),
      std::abs(ref.value()) * std::numeric_limits<T>::epsilon() * 100);
}

// Pending deposits are flushed correctly before negation.
TYPED_TEST(ReproducibleAccumulatorTest, FlushBeforeNegate) {
  using T = TypeParam;
  auto data = this->makeRandomVector(1'000, T(-50), T(50), 402);

  reproducible_accumulator<T> rfa;
  for (auto const& x : data) {
    rfa += x;
  }
  T const val = rfa.value();

  auto neg = -rfa;
  EXPECT_NEAR(
      -val,
      neg.value(),
      std::abs(val) * std::numeric_limits<T>::epsilon() * 100);
}

// operator== flushes pending deposits on both sides.
TYPED_TEST(ReproducibleAccumulatorTest, FlushBeforeEquals) {
  using T = TypeParam;
  auto data = this->makeRandomVector(1'000, T(-50), T(50), 403);

  reproducible_accumulator<T> a, b;
  for (auto const& x : data) {
    a += x;
    b += x;
  }
  // Both have pending deposits; equality should still hold
  EXPECT_TRUE(a == b);
}

// Mixing one-at-a-time deposits with batch add produces correct results.
TYPED_TEST(ReproducibleAccumulatorTest, MixOneAtATimeAndBatchAdd) {
  using T = TypeParam;
  auto data = this->makeRandomVector(10'000, T(-100), T(100), 404);

  reproducible_accumulator<T> ref;
  for (auto const& x : data) {
    ref += x;
  }

  // First half one-at-a-time (leaves pending deposits), then batch
  reproducible_accumulator<T> mixed;
  size_t half = data.size() / 2;
  for (size_t i = 0; i < half; ++i) {
    mixed += data[i];
  }
  mixed.add(data.begin() + half, data.end());

  EXPECT_EQ(ref.value(), mixed.value());
}

// Values spanning different bin ranges trigger bin updates correctly
// even with the fast exponent check optimization.
TYPED_TEST(ReproducibleAccumulatorTest, FastBinCheckWithVaryingMagnitudes) {
  using T = TypeParam;
  reproducible_accumulator<T> rfa_fast, rfa_batch;

  // Start with small values, then add progressively larger ones
  // to force bin shifts through the fast-check path
  std::vector<T> data;
  data.reserve(3000);
  for (int i = 0; i < 1000; ++i) {
    data.push_back(T(0.001) * T(i + 1));
  }
  for (int i = 0; i < 1000; ++i) {
    data.push_back(T(1000.0) * T(i + 1));
  }
  for (int i = 0; i < 1000; ++i) {
    data.push_back(T(0.001) * T(i + 1));
  }

  for (auto const& x : data) {
    rfa_fast += x;
  }
  rfa_batch.add(data.begin(), data.end());

  EXPECT_EQ(rfa_fast.value(), rfa_batch.value());
}
