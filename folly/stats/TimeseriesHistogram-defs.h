/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/Conv.h>
#include <folly/stats/Histogram-defs.h>
#include <folly/stats/MultiLevelTimeSeries-defs.h>
#include <folly/stats/BucketedTimeSeries-defs.h>

namespace folly {

template <class T, class TT, class C>
template <typename ReturnType>
ReturnType TimeseriesHistogram<T, TT, C>::avg(int level) const {
  ValueType total = ValueType();
  int64_t nsamples = 0;
  for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
    const auto& levelObj = buckets_.getByIndex(b).getLevel(level);
    total += levelObj.sum();
    nsamples += levelObj.count();
  }
  return folly::detail::avgHelper<ReturnType>(total, nsamples);
}

template <class T, class TT, class C>
template <typename ReturnType>
ReturnType TimeseriesHistogram<T, TT, C>::avg(TimeType start,
                                              TimeType end) const {
  ValueType total = ValueType();
  int64_t nsamples = 0;
  for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
    const auto& levelObj = buckets_.getByIndex(b).getLevel(start, end);
    total += levelObj.sum(start, end);
    nsamples += levelObj.count(start, end);
  }
  return folly::detail::avgHelper<ReturnType>(total, nsamples);
}

template <class T, class TT, class C>
template <typename ReturnType>
ReturnType TimeseriesHistogram<T, TT, C>::rate(TimeType start,
                                               TimeType end) const {
  ValueType total = ValueType();
  TimeType elapsed(0);
  for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
    const auto& level = buckets_.getByIndex(b).getLevel(start);
    total += level.sum(start, end);
    elapsed = std::max(elapsed, level.elapsed(start, end));
  }
  return folly::detail::rateHelper<ReturnType, TimeType, TimeType>(
      total, elapsed);
}

template <typename T, typename TT, typename C>
TimeseriesHistogram<T, TT, C>::TimeseriesHistogram(ValueType bucketSize,
                                            ValueType min,
                                            ValueType max,
                                            const ContainerType& copyMe)
  : buckets_(bucketSize, min, max, copyMe),
    haveNotSeenValue_(true),
    singleUniqueValue_(false) {
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::addValue(TimeType now,
                                             const ValueType& value) {
  buckets_.getByValue(value).addValue(now, value);
  maybeHandleSingleUniqueValue(value);
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::addValue(TimeType now,
                                      const ValueType& value,
                                      int64_t times) {
  buckets_.getByValue(value).addValue(now, value, times);
  maybeHandleSingleUniqueValue(value);
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::addValues(
    TimeType now, const folly::Histogram<ValueType>& hist) {
  CHECK_EQ(hist.getMin(), getMin());
  CHECK_EQ(hist.getMax(), getMax());
  CHECK_EQ(hist.getBucketSize(), getBucketSize());
  CHECK_EQ(hist.getNumBuckets(), getNumBuckets());

  for (unsigned int n = 0; n < hist.getNumBuckets(); ++n) {
    const typename folly::Histogram<ValueType>::Bucket& histBucket =
      hist.getBucketByIndex(n);
    Bucket& myBucket = buckets_.getByIndex(n);
    myBucket.addValueAggregated(now, histBucket.sum, histBucket.count);
  }

  // We don't bother with the singleUniqueValue_ tracking.
  haveNotSeenValue_ = false;
  singleUniqueValue_ = false;
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::maybeHandleSingleUniqueValue(
  const ValueType& value) {
  if (haveNotSeenValue_) {
    firstValue_ = value;
    singleUniqueValue_ = true;
    haveNotSeenValue_ = false;
  } else if (singleUniqueValue_) {
    if (value != firstValue_) {
      singleUniqueValue_ = false;
    }
  }
}

template <typename T, typename TT, typename C>
T TimeseriesHistogram<T, TT, C>::getPercentileEstimate(double pct,
                                                       int level) const {
  if (singleUniqueValue_) {
    return firstValue_;
  }

  return buckets_.getPercentileEstimate(pct / 100.0, CountFromLevel(level),
                                        AvgFromLevel(level));
}

template <typename T, typename TT, typename C>
T TimeseriesHistogram<T, TT, C>::getPercentileEstimate(double pct,
                                                TimeType start,
                                                TimeType end) const {
  if (singleUniqueValue_) {
    return firstValue_;
  }

  return buckets_.getPercentileEstimate(pct / 100.0,
                                        CountFromInterval(start, end),
                                        AvgFromInterval<T>(start, end));
}

template <typename T, typename TT, typename C>
int TimeseriesHistogram<T, TT, C>::getPercentileBucketIdx(
  double pct,
  int level
) const {
  return buckets_.getPercentileBucketIdx(pct / 100.0, CountFromLevel(level));
}

template <typename T, typename TT, typename C>
int TimeseriesHistogram<T, TT, C>::getPercentileBucketIdx(double pct,
                                                   TimeType start,
                                                   TimeType end) const {
  return buckets_.getPercentileBucketIdx(pct / 100.0,
                                         CountFromInterval(start, end));
}

template <typename T, typename TT, typename C>
T TimeseriesHistogram<T, TT, C>::rate(int level) const {
  ValueType total = ValueType();
  TimeType elapsed(0);
  for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
    const auto& levelObj = buckets_.getByIndex(b).getLevel(level);
    total += levelObj.sum();
    elapsed = std::max(elapsed, levelObj.elapsed());
  }
  return elapsed == TimeType(0) ? 0 : (total / elapsed.count());
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::clear() {
  for (size_t i = 0; i < buckets_.getNumBuckets(); i++) {
    buckets_.getByIndex(i).clear();
  }
}

template <typename T, typename TT, typename C>
void TimeseriesHistogram<T, TT, C>::update(TimeType now) {
  for (size_t i = 0; i < buckets_.getNumBuckets(); i++) {
    buckets_.getByIndex(i).update(now);
  }
}

template <typename T, typename TT, typename C>
std::string TimeseriesHistogram<T, TT, C>::getString(int level) const {
  std::string result;

  for (size_t i = 0; i < buckets_.getNumBuckets(); i++) {
    if (i > 0) {
      toAppend(",", &result);
    }
    const ContainerType& cont = buckets_.getByIndex(i);
    toAppend(buckets_.getBucketMin(i),
             ":", cont.count(level),
             ":", cont.template avg<ValueType>(level), &result);
  }

  return result;
}

template <typename T, typename TT, typename C>
std::string TimeseriesHistogram<T, TT, C>::getString(TimeType start,
                                                     TimeType end) const {
  std::string result;

  for (size_t i = 0; i < buckets_.getNumBuckets(); i++) {
    if (i > 0) {
      toAppend(",", &result);
    }
    const ContainerType& cont = buckets_.getByIndex(i);
    toAppend(buckets_.getBucketMin(i),
             ":", cont.count(start, end),
             ":", cont.avg(start, end), &result);
  }

  return result;
}

}  // namespace folly
