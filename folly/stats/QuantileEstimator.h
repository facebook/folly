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

#include <folly/stats/TDigest.h>
#include <folly/stats/detail/BufferedStat.h>

namespace folly {

struct QuantileEstimates {
  double sum;
  double count;

  // vector of {quantile, value}
  std::vector<std::pair<double, double>> quantiles;
};

/*
 * A QuantileEstimator that buffers writes for 1 second.
 */
template <typename ClockT = std::chrono::steady_clock>
class SimpleQuantileEstimator {
 public:
  using TimePoint = typename ClockT::time_point;

  SimpleQuantileEstimator();

  QuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedDigest_.flush(); }

  // Get point-in-time TDigest
  TDigest getDigest(TimePoint now = ClockT::now()) {
    return bufferedDigest_.get(now);
  }

 private:
  detail::BufferedDigest<TDigest, ClockT> bufferedDigest_;
};

/*
 * A QuantileEstimator that keeps values for nWindows * windowDuration (see
 * constructor). Values are buffered for windowDuration.
 */
template <typename ClockT = std::chrono::steady_clock>
class SlidingWindowQuantileEstimator {
 public:
  using TimePoint = typename ClockT::time_point;
  using Duration = typename ClockT::duration;

  SlidingWindowQuantileEstimator(Duration windowDuration, size_t nWindows = 60);

  QuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedSlidingWindow_.flush(); }

  // Get point-in-time TDigest
  TDigest getDigest(TimePoint now = ClockT::now()) {
    return TDigest::merge(bufferedSlidingWindow_.get(now));
  }

 private:
  detail::BufferedSlidingWindow<TDigest, ClockT> bufferedSlidingWindow_;
};

/*
 * Equivalent to (but more efficient than) a SimpleQuantileEstimator plus one
 * SlidingWindowQuantileEstimator for each requested window.
 */
template <typename ClockT = std::chrono::steady_clock>
class MultiSlidingWindowQuantileEstimator {
 public:
  using TimePoint = typename ClockT::time_point;
  using Duration = typename ClockT::duration;

  // Minimum granularity is in seconds, so we can buffer at least one second.
  using WindowDef = std::pair<std::chrono::seconds, size_t>;

  struct Digests {
    Digests(TDigest at, std::vector<TDigest> ws)
        : allTime(std::move(at)), windows(std::move(ws)) {}

    TDigest allTime;
    std::vector<TDigest> windows;
  };

  struct MultiQuantileEstimates {
    QuantileEstimates allTime;
    std::vector<QuantileEstimates> windows;
  };

  explicit MultiSlidingWindowQuantileEstimator(Range<const WindowDef*> defs);

  MultiQuantileEstimates estimateQuantiles(
      Range<const double*> quantiles, TimePoint now = ClockT::now());

  void addValue(double value, TimePoint now = ClockT::now());

  /// Flush buffered values
  void flush() { bufferedMultiSlidingWindow_.flush(); }

  // Get point-in-time TDigests
  Digests getDigests(TimePoint now = ClockT::now());

 private:
  detail::BufferedMultiSlidingWindow<TDigest, ClockT>
      bufferedMultiSlidingWindow_;
};

} // namespace folly

#include <folly/stats/QuantileEstimator-inl.h>
