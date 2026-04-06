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

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>

#include <folly/lang/Hint.h>

namespace folly {

/// Kahan (Kahan-Babushka-Neumaier) compensated summation accumulator.
///
/// Achieves O(eps) error independent of the number of summands (compared to
/// O(n*eps) for naive summation).
///
/// This implements the Neumaier improvement over Kahan's original algorithm.
/// In addition to maintaining a running compensation term (as in Kahan), it
/// also handles the case where the new summand is larger in magnitude than the
/// running sum. Standard Kahan only compensates correctly when |sum| >= |x|;
/// Neumaier checks which is larger and applies the compensation from the
/// correct side.
///
/// Performance note: despite the branch, this is ~1.85x faster than Kahan's
/// original algorithm on modern out-of-order CPUs. The reason is the
/// loop-carried data dependency chain. In Kahan's original, c must be
/// subtracted from x before adding to sum, creating a 4-operation serial chain
/// per iteration: c -> y=x-c -> t=sum+y -> c=(t-sum)-y. In Neumaier, x is
/// added directly to sum (no dependency on c), so the sum chain is just 1
/// operation (sum -> t=sum+x -> sum), and the c chain updates independently.
/// In benchmarks Neumaier runs at ~10 cycles/element vs Kahan's ~18. For
/// Kahan's original to win, the branch misprediction rate would need to exceed
/// (18-10)/15 > 53%, which modern predictors never sustain — even a purely
/// random branch outcome yields ~50% at worst.
///
/// @tparam T  A floating-point type (float, double, or long double).
template <std::floating_point T>
class kahan_accumulator {
  T sum_{};
  T c_{};

 public:
  /// Default-constructs to zero.
  constexpr kahan_accumulator() = default;

  /// Adds a single value using compensated summation.
  ///
  /// @param x  The value to add.
  /// @return   Reference to this accumulator.
  kahan_accumulator& operator+=(T const x) {
    T t = sum_ + x;
    // Make t opaque to the optimizer. Without this, the compiler could
    // substitute t = sum_ + x into the branches below, simplifying the
    // compensation terms to zero and reducing this to naive summation.
    compiler_must_not_predict(t);
    if (std::abs(sum_) >= std::abs(x)) {
      c_ += (sum_ - t) + x;
    } else {
      c_ += (x - t) + sum_;
    }
    sum_ = t;
    return *this;
  }

  /// Subtracts a single value using compensated summation.
  ///
  /// @param x  The value to subtract.
  /// @return   Reference to this accumulator.
  kahan_accumulator& operator-=(T const x) { return *this += -x; }

  /// Returns the accumulated sum (including the compensation term).
  constexpr T value() const { return sum_ + c_; }
};

/// Computes the Kahan compensated sum over an iterator range.
///
/// For random-access iterators with 128+ elements, uses multiple independent
/// accumulators to exploit instruction-level parallelism. For smaller inputs
/// or non-random-access iterators, falls back to the serial path.
///
/// @tparam I  An input iterator whose value type is a floating-point type.
/// @tparam S  A sentinel for I.
/// @param first  Iterator to the first element.
/// @param last   Sentinel past the last element.
/// @return       The compensated sum of all elements in [first, last).
template <std::input_iterator I, std::sentinel_for<I> S>
  requires std::floating_point<std::iter_value_t<I>>
[[gnu::flatten]] std::iter_value_t<I> kahan_sum(I first, S last) {
  using T = std::iter_value_t<I>;
  if constexpr (std::random_access_iterator<I>) {
    constexpr int K = 4;
    constexpr std::iter_difference_t<I> kILPThreshold = 128;
    auto const n = last - first;
    if (n >= kILPThreshold) {
      kahan_accumulator<T> acc;
      std::array<kahan_accumulator<T>, K - 1> accs;
      auto const full = n - (n % K);
      for (std::iter_difference_t<I> i = 0; i < full; i += K) {
        acc += first[i];
        accs[0] += first[i + 1];
        accs[1] += first[i + 2];
        accs[2] += first[i + 3];
      }
      for (auto i = full; i < n; ++i) {
        acc += first[i];
      }
      for (int k = 0; k < K - 1; ++k) {
        acc += accs[k].value();
      }
      return acc.value();
    }
  }
  kahan_accumulator<T> acc;
  for (; first != last; ++first) {
    acc += *first;
  }
  return acc.value();
}

/// Computes the Kahan compensated sum over a range.
///
/// @tparam R  An input range whose value type is a floating-point type.
/// @param range  The range to sum.
/// @return       The compensated sum of all elements in the range.
template <std::ranges::input_range R>
  requires std::floating_point<std::ranges::range_value_t<R>>
std::ranges::range_value_t<R> kahan_sum(R&& range) {
  return kahan_sum(std::ranges::begin(range), std::ranges::end(range));
}

} // namespace folly
