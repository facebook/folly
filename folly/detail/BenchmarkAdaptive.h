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

#include <folly/Benchmark.h>

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace folly {
namespace detail {

inline constexpr const char* kANSIBold = "\033[1m";
inline constexpr const char* kANSIBoldGreen = "\033[1;32m";
inline constexpr const char* kANSIBoldYellow = "\033[1;33m";
inline constexpr const char* kANSIBoldRed = "\033[1;31m";
inline constexpr const char* kANSIReset = "\033[0m";

// Confidence interval for a percentile estimate.
struct PercentileCI {
  double lo;
  double hi;
  double estimate;

  double relWidth() const {
    if (estimate == 0) {
      LOG(WARNING) << "relWidth called with zero estimate";
      return 100.0;
    }
    return (hi - lo) / estimate * 100.0;
  }
};

// Sample container with statistical methods. Sorts on construction.
class SortedSamples {
 public:
  explicit SortedSamples(std::vector<double> samples)
      : samples_(std::move(samples)) {
    std::sort(samples_.begin(), samples_.end());
  }

  size_t size() const { return samples_.size(); }

  // Returns the value at the given percentile (0-100).
  // Uses linear interpolation between floor and ceil indices.
  double percentile(double pct) const {
    CHECK_GE(samples_.size(), 1) << "percentile requires at least 1 sample";
    double pos = (samples_.size() - 1) * pct / 100.0;
    size_t lo = static_cast<size_t>(std::floor(pos));
    size_t hi = static_cast<size_t>(std::ceil(pos));
    double frac = pos - lo;
    return samples_[lo] * (1.0 - frac) + samples_[hi] * frac;
  }

  // Returns CI bounds and estimate for the given percentile.
  //
  // Normal approximation to binomial distribution of order statistics.  The
  // expected position of the p-quantile is (n-1)*p with SE = sqrt(n*p*(1-p)).
  // Conservative for small n, but adequate for n >= 20 that's typical here.
  PercentileCI percentileCI(double pct, double zScore = 1.96) const {
    CHECK_GE(samples_.size(), 2) << "percentileCI requires at least 2 samples";
    size_t n = samples_.size();
    double q = pct / 100.0;

    // SE of order statistic position under binomial distribution
    double positionSE = std::sqrt(n * q * (1 - q));
    double pos = (n - 1) * q;

    // Convert position bounds back to percentiles
    double loPct = std::max(0.0, (pos - zScore * positionSE) / (n - 1) * 100.0);
    double hiPct =
        std::min(100.0, (pos + zScore * positionSE) / (n - 1) * 100.0);

    return {percentile(loPct), percentile(hiPct), percentile(pct)};
  }

  // Convenience method: returns CI relative width, or 100.0 if < 2 samples.
  double percentileCIRelWidth(double pct, double zScore = 1.96) const {
    return samples_.size() >= 2 ? percentileCI(pct, zScore).relWidth() : 100.0;
  }

 private:
  std::vector<double> samples_;
};

// Epsilon for stability comparisons (1 picosecond).
// Avoids spurious instability from floating-point precision on sub-ns values.
constexpr double kStabilityEpsilonNs = 0.001;

// Split-half stability stats for detecting oscillation.
struct StabilityStats {
  PercentileCI firstHalf;
  PercentileCI secondHalf;
  bool isStable;
};

// Compute split-half stability: are the first-half and second-half estimates
// within each other's CIs (with epsilon tolerance)?  Needs at least 4 samples.
StabilityStats computeStabilityStats(
    const std::vector<double>& samples, double percentile);

struct AdaptiveOptions {
  int64_t sliceUsec;
  double targetPercentile;
  double targetPrecisionPct;
  size_t minSamples;
  double minSecs;
  int32_t maxSecs;
  bool verbose;

  std::string toString() const {
    std::ostringstream oss;
    oss << "sliceUsec=" << sliceUsec << " targetPercentile=" << targetPercentile
        << " targetPrecisionPct=" << targetPrecisionPct << " minSamples="
        << minSamples << " minSecs=" << minSecs << " maxSecs=" << maxSecs;
    return oss.str();
  }
};

struct AdaptiveResult {
  std::vector<BenchmarkResult> results;
  size_t totalRounds = 0;
};

AdaptiveResult runBenchmarksAdaptive(
    const std::vector<const BenchmarkRegistration*>&,
    const BenchmarkFun& baseline,
    const BenchmarkFun& suspenderBaseline,
    const AdaptiveOptions&);

// Given current `iterCount` and the duration of the last sample, compute a new
// `iterCount` that would make the next sample closer to `target` duration.
// Monotonically increasing: returns `max(iterCount, candidate)`.
size_t recalibrateIterCount(
    size_t iterCount,
    std::chrono::nanoseconds target,
    std::chrono::nanoseconds dur);

} // namespace detail
} // namespace folly
