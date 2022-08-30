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

#include <string>

#include <folly/Conv.h>
#include <folly/GLog.h>
#include <folly/Likely.h>
#include <folly/Range.h>
#include <folly/SharedMutex.h>
#include <folly/lang/Align.h>
#include <folly/stats/DigestBuilder.h>

namespace folly {

template <class Q>
class QuantileHistogramBase {
 public:
  QuantileHistogramBase() = default;
  explicit QuantileHistogramBase(size_t) : QuantileHistogramBase() {}

  static constexpr std::array<double, Q::kNumQuantiles> quantiles() {
    return Q::kQuantiles;
  }

  /*
   * Combines the given histograms into a new histogram where the locations for
   * each tracked quantile is the weighted average of the corresponding quantile
   * locations. The min and max for the new histogram will be the smallest min
   * or the largest max of all of the given histograms.
   */
  static QuantileHistogramBase<Q> merge(
      Range<const ::folly::QuantileHistogramBase<Q>*> qhists);

  QuantileHistogramBase<Q> merge(Range<const double*> unsortedValues) const;

  void addValue(double value);

  /*
   * Estimates the value of the given quantile.
   */
  double estimateQuantile(double q) const;

  uint64_t count() const { return count_; }

  double min() const { return locations_.front(); }

  double max() const { return locations_.back(); }

  std::string debugString() const;

 private:
  static_assert(quantiles().size() >= 2, "Quantiles 0.0 and 1.0 are required.");
  static_assert(quantiles().front() == 0.0, "Quantile 0.0 is required.");
  static_assert(quantiles().back() == 1.0, "Quantile 1.0 is required.");

  // locations_ tracks min and max at the two ends.
  std::array<double, Q::kNumQuantiles> locations_{};
  uint64_t count_{0};

  inline size_t addValueImpl(
      double value, const std::array<double, Q::kNumQuantiles>& oldLocations);

  void dcheckSane() const;
};

class DefaultQuantiles {
 public:
  static constexpr size_t kNumQuantiles = 9;

  static constexpr std::array<double, kNumQuantiles> kQuantiles{
      0.0, 0.001, 0.01, 0.25, 0.5, 0.75, 0.99, 0.999, 1.0};
};

using QuantileHistogram = QuantileHistogramBase<DefaultQuantiles>;

// The CPUShardedQuantileHistogram class behaves similarly to QuantileHistogram
// except that it is thread-safe. Adding values is heavily optimized while any
// kind of inference will incur a heavy cost because all cpu-local shards must
// be merged.
template <class Q = DefaultQuantiles>
class CPUShardedQuantileHistogram {
 public:
  CPUShardedQuantileHistogram()
      : histBuilder_(
            /*bufferSize=*/hardware_destructive_interference_size /
                sizeof(double),
            /*digestSize=*/0) {}

  static constexpr std::array<double, Q::kNumQuantiles> quantiles();

  void addValue(double value);

  double estimateQuantile(double q);

  uint64_t count();

  double min();

  double max();

  std::string debugString();

 private:
  SharedMutex mtx_;
  QuantileHistogramBase<Q> mergedHist_;
  DigestBuilder<QuantileHistogramBase<Q>> histBuilder_;

  // Assumes mtx is held.
  void flush();
};

} // namespace folly

#include <folly/stats/QuantileHistogram-inl.h>
