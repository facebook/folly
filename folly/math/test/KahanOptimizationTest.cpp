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

// Verifies that folly::kahan_accumulator's defenses
// (compiler_must_not_predict + FOLLY_FLOAT_PRECISE / FOLLY_FLOAT_PRECISE_ATTR)
// preserve the Kahan compensation step, while unprotected implementations
// are vulnerable to compiler optimization (especially with -ffast-math).

#include <folly/math/KahanSummation.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include <folly/CPortability.h>
#include <folly/container/irange.h>
#include <folly/portability/GTest.h>

namespace {

// Unprotected Kahan summation.
FOLLY_NOINLINE double kahan_unprotected(const double* data, size_t n) {
  double sum = 0.0;
  double c = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double y = data[i] - c;
    double t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }
  return sum;
}

// Naive summation — no compensation at all.
FOLLY_NOINLINE double naive_sum(const double* data, size_t n) {
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) {
    sum += data[i];
  }
  return sum;
}

// Build an ill-conditioned dataset where naive summation fails badly:
// [1e16, 1.0, 1.0, ..., 1.0, -1e16]
// True sum = N (the number of 1.0s).
// Naive sum = 0 because each 1.0 is below the ULP of 1e16.
// Kahan sum = N because the compensation captures the lost 1.0s.
std::vector<double> makeIllConditionedData(size_t nOnes) {
  std::vector<double> data;
  data.reserve(nOnes + 2);
  data.push_back(1e16);
  for ([[maybe_unused]] auto i : folly::irange(nOnes)) {
    data.push_back(1.0);
  }
  data.push_back(-1e16);
  return data;
}

} // namespace

// Verifies that folly::kahan_sum (which uses compiler_must_not_predict and
// FOLLY_FLOAT_PRECISE / FOLLY_FLOAT_PRECISE_ATTR) recovers the compensated sum
// on ill-conditioned data regardless of compiler flags, where naive and
// unprotected-Kahan summation may not. The asm barrier and float-control
// pragmas/attributes are what keep the protected path exact even under
// -ffast-math.
//
// The naive and unprotected results are computed and reported for diagnostic
// context only: their errors vary by compiler and flags (unprotected Kahan may
// or may not survive optimization at @fbcode//mode/opt, and with -ffast-math
// its compensation is optimized away entirely), so they are not asserted on.
// The protected path is the contract, so it is the only assertion.
TEST(KahanOptimization, ProtectedKahanBeatsNaive) {
  constexpr size_t N = 10'000;
  auto const data = makeIllConditionedData(N);
  double const true_val = static_cast<double>(N);

  double const naive = naive_sum(data.data(), data.size());
  double const unprotected = kahan_unprotected(data.data(), data.size());
  double const protected_val = folly::kahan_sum(data);

  double const naive_err = std::abs(naive - true_val);
  double const unprotected_err = std::abs(unprotected - true_val);
  double const protected_err = std::abs(protected_val - true_val);

  std::fprintf(
      stderr,
      "\n=== Kahan Compensation Defense Report ===\n"
      "True value:              %.1f\n"
      "Naive sum:               %.1f  (error: %.1f)\n"
      "Kahan (unprotected):     %.1f  (error: %.1f)\n"
      "Kahan (folly protected): %.1f  (error: %.1f)\n"
      "=== End Report ===\n\n",
      true_val,
      naive,
      naive_err,
      unprotected,
      unprotected_err,
      protected_val,
      protected_err);

  EXPECT_EQ(protected_val, true_val)
      << "Protected Kahan should exactly recover the 1.0 additions";
}
