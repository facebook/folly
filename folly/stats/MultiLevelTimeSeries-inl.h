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

#include <numeric>
#include <glog/logging.h>

#include <folly/ConstexprMath.h>

namespace folly {

template <typename VT, typename CT>
MultiLevelTimeSeries<VT, CT>::MultiLevelTimeSeries(
    size_t nBuckets, folly::Range<const Duration*> durations)
    : cachedTime_(), cachedSum_(0), cachedCount_(0) {
  CHECK_GT(durations.size(), 0u);

  levels_.reserve(durations.size());
  size_t i = 0;
  Duration prev{0};
  for (auto dur : durations) {
    if (dur == Duration(0)) {
      CHECK_EQ(i, durations.size() - 1);
    } else if (i > 0) {
      CHECK(prev < dur);
    }
    levels_.emplace_back(nBuckets, dur);
    prev = dur;
    i++;
  }

  cacheDuration_ = computeMaxCacheDuration();
}

/*
 * This is an optimization that allows MultiLevelTimeSeries to cache values
 * longer on the write path before flushing them to the underlying
 * BucketedTimeSeries. It preserves all existing semantics of the
 * timeseries and produces no observable changes to any user if one
 * follows the API contract correctly (e.g. always call `update()` before
 * reading the timeseries to get the latest data).
 * Specifically, if time point T belongs to bucket B, values added at T should
 * be reflected in bucket B. If readers are calling update() correctly, it
 * follows that reading of an entire level (BucketedTimeSeries) or an interval
 * of a level will observe exactly the same value as if every write is flushed
 * immediately with no caching.
 */
template <typename VT, typename CT>
typename MultiLevelTimeSeries<VT, CT>::Duration
MultiLevelTimeSeries<VT, CT>::computeMaxCacheDuration() {
  // Every integer divides 0, so we don't need to special case the first gcd
  // calculation. We also use it to indicate caching forever.
  size_t cacheTickCount = 0;
  for (const auto& level : levels_) {
    if (level.isAllTime()) {
      continue;
    }
    size_t actualBucketCnt = level.numBuckets();
    size_t durationTickCnt = level.duration().count();
    if (durationTickCnt % actualBucketCnt != 0) {
      // only cache one tick worth of updates when bucket size is a fraction
      // since the cache duration is counted by the number of ticks
      return Duration{1};
    }
    // gcd is associative
    cacheTickCount =
        std::gcd(cacheTickCount, durationTickCnt / actualBucketCnt);
  }
  return Duration{cacheTickCount};
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::addValue(
    TimePoint now, const ValueType& val) {
  addValueAggregated(now, val, 1);
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::addValue(
    TimePoint now, const ValueType& val, uint64_t times) {
  addValueAggregated(now, val * ValueType(times), times);
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::addValueAggregated(
    TimePoint now, const ValueType& total, uint64_t nsamples) {
  // if the only level we have is an all-time BucketedTimeseries (have
  // cacheDuration_ be 0), we will just cache the value forever, and only flush
  // on read (essentially when update() is called)
  if (cacheDuration_ != Duration{0} &&
      (now < cachedTime_ || now >= cachedTime_ + cacheDuration_)) {
    flush();
    // cachedTime_ represents the time point of the next flush for all the
    // cached values. All time points within the cache duration, should belong
    // to the same bucket as cachedTime_ to preserve the current semantics. This
    // is achieved by rounding _down_ `now` to align with cacheDuration_
    // boundaries, so from the BucketedTimeSeries's point of view, clock jumps
    // forward in multiples of cacheDuration_.
    cachedTime_ = TimePoint(Duration{
        (now.time_since_epoch().count() / cacheDuration_.count()) *
        cacheDuration_.count()});
  }

  for (size_t i = 0; i < levels_.size(); ++i) {
    if (!levels_[i].isAllTime()) {
      DCHECK_EQ(
          levels_[i].getBucketIdx(now), levels_[i].getBucketIdx(cachedTime_));
    }
  }

  // We have no control over how many different values get added to a time
  // series.
  // We also have no control over their value. We also want to keep some partial
  // ordering; meaning large numbers should stay large, and negative numbers
  // should stay negative. So use the constexpr_add_overflow_clamped so that
  // this never overflows
  cachedSum_ = constexpr_add_overflow_clamped(cachedSum_, total);
  cachedCount_ = constexpr_add_overflow_clamped(cachedCount_, nsamples);
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::update(TimePoint now) {
  flush();
  for (size_t i = 0; i < levels_.size(); ++i) {
    levels_[i].update(now);
  }
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::flush() {
  // update all the underlying levels
  if (cachedCount_ > 0) {
    for (size_t i = 0; i < levels_.size(); ++i) {
      levels_[i].addValueAggregated(cachedTime_, cachedSum_, cachedCount_);
    }
    cachedCount_ = 0;
    cachedSum_ = 0;
  }
}

template <typename VT, typename CT>
void MultiLevelTimeSeries<VT, CT>::clear() {
  for (auto& level : levels_) {
    level.clear();
  }

  cachedTime_ = TimePoint();
  cachedSum_ = 0;
  cachedCount_ = 0;
}

} // namespace folly
