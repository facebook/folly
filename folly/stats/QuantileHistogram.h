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
#include <type_traits>

#include <folly/Conv.h>
#include <folly/GLog.h>
#include <folly/Likely.h>
#include <folly/Range.h>
#include <folly/SharedMutex.h>
#include <folly/lang/Align.h>
#include <folly/stats/DigestBuilder.h>

namespace folly {

class PredefinedQuantiles {
 public:
  class Default {
   public:
    static constexpr std::array<double, 11> kQuantiles{
        0.0, 0.001, 0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99, 0.999, 1.0};
  };

  class MinAndMax {
   public:
    static constexpr std::array<double, 2> kQuantiles{0.0, 1.0};
  };

  class Median {
   public:
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.5, 1.0};
  };

  class P01 {
   public:
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.01, 1.0};
  };

  class P99 {
   public:
    static constexpr std::array<double, 3> kQuantiles{0.0, 0.99, 1.0};
  };

  class MedianAndHigh {
   public:
    static constexpr std::array<double, 6> kQuantiles{
        0.0, 0.5, 0.9, 0.99, 0.999, 1.0};
  };

  class Quartiles {
   public:
    static constexpr std::array<double, 5> kQuantiles{
        0.0, 0.25, 0.5, 0.75, 1.0};
  };

  class Deciles {
   public:
    static constexpr std::array<double, 11> kQuantiles{
        0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
  };

  class Ventiles {
   public:
    static constexpr std::array<double, 21> kQuantiles{
        0.0,  0.05, 0.1,  0.15, 0.2,  0.25, 0.3,  0.35, 0.4,  0.45, 0.5,
        0.55, 0.6,  0.65, 0.7,  0.75, 0.8,  0.85, 0.9,  0.95, 1.0};
  };
};

template <class Q = PredefinedQuantiles::Default>
class QuantileHistogram {
 public:
  QuantileHistogram() = default;
  explicit QuantileHistogram(size_t) : QuantileHistogram() {}

  static constexpr decltype(Q::kQuantiles) quantiles() { return Q::kQuantiles; }

  /*
   * Combines the given histograms into a new histogram where the locations for
   * each tracked quantile is the weighted average of the corresponding quantile
   * locations. The min and max for the new histogram will be the smallest min
   * or the largest max of all of the given histograms.
   */
  static QuantileHistogram<Q> merge(
      Range<const ::folly::QuantileHistogram<Q>*> qhists);

  QuantileHistogram<Q> merge(Range<const double*> unsortedValues) const;

  void addValue(double value);

  /*
   * Estimates the value of the given quantile.
   */
  double estimateQuantile(double q) const;

  uint64_t count() const { return count_; }

  bool empty() const { return count_ == 0; }

  double min() const { return locations_.front(); }

  double max() const { return locations_.back(); }

  std::string debugString() const;

 private:
  static_assert(quantiles().size() >= 2, "Quantiles 0.0 and 1.0 are required.");
  static_assert(quantiles().front() == 0.0, "Quantile 0.0 is required.");
  static_assert(quantiles().back() == 1.0, "Quantile 1.0 is required.");

  // locations_ tracks min and max at the two ends.
  typename std::remove_const<decltype(Q::kQuantiles)>::type locations_{};
  uint64_t count_{0};

  inline size_t addValueImpl(
      double value, const decltype(Q::kQuantiles)& oldLocations);

  void dcheckSane() const;
};

// The CPUShardedQuantileHistogram class behaves similarly to QuantileHistogram
// except that it is thread-safe. Adding values is heavily optimized while any
// kind of inference will incur a heavy cost because all cpu-local shards must
// be merged.
template <class Q = PredefinedQuantiles::Default>
class CPUShardedQuantileHistogram {
 public:
  CPUShardedQuantileHistogram()
      : histBuilder_(
            /*bufferSize=*/hardware_destructive_interference_size /
                sizeof(double),
            /*digestSize=*/0) {}

  static constexpr decltype(Q::kQuantiles) quantiles() { return Q::kQuantiles; }

  void addValue(double value);

  double estimateQuantile(double q);

  uint64_t count();

  double min();

  double max();

  std::string debugString();

 private:
  mutable SharedMutex mtx_;
  QuantileHistogram<Q> mergedHist_;
  DigestBuilder<QuantileHistogram<Q>> histBuilder_;

  // Assumes mtx is held.
  void flush();
};

} // namespace folly

#include <folly/stats/QuantileHistogram-inl.h>
