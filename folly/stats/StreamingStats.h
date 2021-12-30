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

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <type_traits>

#include <folly/lang/Exception.h>

namespace folly {

// Robust and efficient online computation of statistics,
// using Welford's method for variance.
// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm

template <typename SampleDataType, typename StatsType = double>
class StreamingStats final {
  // Caclulated statistic result has to be floating point type
  static_assert(std::is_floating_point_v<StatsType>);

 public:
  struct StreamingState {
    size_t count = 0;
    StatsType mean = 0;
    StatsType m2 = 0;
    SampleDataType min = std::numeric_limits<SampleDataType>::max();
    SampleDataType max = std::numeric_limits<SampleDataType>::lowest();
  };

  template <class Iterator>
  StreamingStats(Iterator first, Iterator last) noexcept {
    add(first, last);
  }

  explicit StreamingStats(StreamingState state)
      : count_(state.count),
        mean_(state.mean),
        m2_(state.m2),
        min_(state.min),
        max_(state.max) {}

  StreamingStats() = default;

  ~StreamingStats() = default;

  /// Add sample data via iteratation
  template <class Iterator>
  void add(Iterator first, Iterator last) noexcept {
    for (auto it = first; it != last; ++it) {
      add(*it);
    }
  }

  /// Add a single sample
  void add(SampleDataType value) noexcept {
    max_ = std::max(max_, value);
    min_ = std::min(min_, value);
    ++count_;
    StatsType const delta = value - mean_;
    mean_ += delta / count_;
    StatsType const delta2 = value - mean_;
    m2_ += delta * delta2;
  }

  /// Merge with an existing StreamingStats object
  void merge(StreamingStats const& other) {
    if (other.count_ == 0) {
      return;
    }
    max_ = std::max(max_, other.max_);
    min_ = std::min(min_, other.min_);
    size_t const new_size = count_ + other.count_;
    StatsType const new_mean =
        (mean_ * count_ + other.mean_ * other.count_) / new_size;
    // Each cumulant must be corrected.
    //   * from: sum((x_i - mean_)²)
    //   * to:   sum((x_i - new_mean)²)
    auto delta = [&](auto const& stats) {
      return stats.count_ *
          (new_mean * (new_mean - 2 * stats.mean_) + stats.mean_ * stats.mean_);
    };
    m2_ = m2_ + delta(*this) + other.m2_ + delta(other);
    mean_ = new_mean;
    count_ = new_size;
  }

  size_t count() const noexcept { return count_; }

  SampleDataType minimum() const {
    checkMinimumDataSize(1);
    return min_;
  }

  SampleDataType maximum() const {
    checkMinimumDataSize(1);
    return max_;
  }

  StatsType mean() const {
    checkMinimumDataSize(1);
    return mean_;
  }

  StatsType m2() const {
    checkMinimumDataSize(1);
    return m2_;
  }

  StatsType populationVariance() const {
    checkMinimumDataSize(2);
    return var_(0);
  }

  StatsType sampleVariance() const {
    checkMinimumDataSize(2);
    return var_(1);
  }

  StatsType populationStandardDeviation() const {
    checkMinimumDataSize(2);
    return std_(0);
  }

  StatsType sampleStandardDeviation() const {
    checkMinimumDataSize(2);
    return std_(1);
  }

  StreamingState state() const {
    StreamingState state;
    state.count = count_;
    state.m2 = m2_;
    state.max = max_;
    state.mean = mean_;
    state.min = min_;
    return state;
  }

 private:
  void checkMinimumDataSize(size_t const minElements) const {
    if (count_ < minElements) {
      throw_exception<std::logic_error>("stats: unavailable with no samples");
    }
  }

  StatsType var_(size_t bias) const noexcept { return m2_ / (count_ - bias); }

  StatsType std_(size_t bias) const noexcept { return std::sqrt(var_(bias)); }

  size_t count_ = 0;
  StatsType mean_ = 0;
  StatsType m2_ = 0;

  SampleDataType min_ = std::numeric_limits<SampleDataType>::max();
  SampleDataType max_ = std::numeric_limits<SampleDataType>::lowest();
};

} // namespace folly
