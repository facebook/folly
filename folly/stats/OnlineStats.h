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

#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <type_traits>

#pragma once

namespace folly {

// Robust and efficient online computation of statistics,
// using Welford's method for variance.
// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm

template <typename T>
class OnlineStats {
 public:
  template <
      class Iterator,
      class C = typename std::iterator_traits<Iterator>::value_type>
  OnlineStats(Iterator first, Iterator last) {
    for (auto it = first; it != last; ++it) {
      add((C)*it);
    }
  }

  OnlineStats() {}

  /// Add a new sample
  void add(T value) {
    max_ = std::max(max_, value);
    min_ = std::min(min_, value);
    ++count_;
    const double delta = value - mean_;
    mean_ += delta / count_;
    const double delta2 = value - mean_;
    M2_ += delta * delta2;
  }

  /// Merge other stats, as if samples were added one by one, but in O(1).
  void merge(const OnlineStats<T>& other) {
    if (other.count_ == 0) {
      return;
    }
    max_ = std::max(max_, other.max_);
    min_ = std::min(min_, other.min_);
    const int64_t new_size = count_ + other.count_;
    const double new_mean =
        (mean_ * count_ + other.mean_ * other.count_) / new_size;
    // Each cumulant must be corrected.
    //   * from: sum((x_i - mean_)²)
    //   * to:   sum((x_i - new_mean)²)
    auto delta = [new_mean](const OnlineStats<T>& stats) {
      return stats.count_ *
          (new_mean * (new_mean - 2 * stats.mean_) + stats.mean_ * stats.mean_);
    };
    M2_ = M2_ + delta(*this) + other.M2_ + delta(other);
    mean_ = new_mean;
    count_ = new_size;
  }

  unsigned long long count() const { return count_; }

  T minimum() const {
    return calculateNanSafe_([this]() { return min_; });
  }

  T maximum() const {
    return calculateNanSafe_([this]() { return max_; });
  }

  double mean() const {
    return calculateNanSafe_([this]() { return mean_; });
  }

  double unbiasedVariance() {
    return calculateNanSafe_([this]() { return var_(0); });
  }

  double biasedVariance() {
    return calculateNanSafe_([this]() { return var_(1); });
  }

  double unbiasedStandardDeviation() {
    return calculateNanSafe_([this]() { return std_(0); });
  }

  double biasedStandardDeviation() {
    return calculateNanSafe_([this]() { return std_(1); });
  }

  std::tuple<double, double> unbiasedVarianceStandardDeviation() {
    return varStd_(0);
  }
  std::tuple<double, double> biasedVarianceStandardDeviation() {
    return varStd_(1);
  }

 private:
  static constexpr T nan() { return std::numeric_limits<T>::quiet_NaN(); }

  double calculateNanSafe_(std::function<double()> f) const {
    if (count_ == 0) {
      return nan();
    }
    return f();
  }

  double var_(bool bias) const { return M2_ / (count_ - bias); }

  double std_(bool bias) const { return std::sqrt(var_(bias)); }

  std::tuple<double, double> varStd_(bool bias) const {
    if (count_ == 0) {
      return {nan(), nan()};
    }
    double variance = var_(bias);
    double standard_deviation = std::sqrt(variance);
    return {variance, standard_deviation};
  }

  unsigned long long count_ = 0;
  double mean_ = 0;
  double M2_ = 0;

  T min_ = std::numeric_limits<T>::max();
  T max_ = std::numeric_limits<T>::min();
};

} // namespace folly
