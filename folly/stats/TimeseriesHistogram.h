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

#include <string>
#include <folly/stats/Histogram.h>
#include <folly/stats/MultiLevelTimeSeries.h>

namespace folly {

/*
 * TimeseriesHistogram tracks data distributions as they change over time.
 *
 * Specifically, it is a bucketed histogram with different value ranges assigned
 * to each bucket.  Within each bucket is a MultiLevelTimeSeries from
 * 'folly/stats/MultiLevelTimeSeries.h'. This means that each bucket contains a
 * different set of data for different historical time periods, and one can
 * query data distributions over different trailing time windows.
 *
 * For example, this can answer questions: "What is the data distribution over
 * the last minute? Over the last 10 minutes?  Since I last cleared this
 * histogram?"
 *
 * The class can also estimate percentiles and answer questions like: "What was
 * the 99th percentile data value over the last 10 minutes?"
 *
 * Note: that depending on the size of your buckets and the smoothness
 * of your data distribution, the estimate may be way off from the actual
 * value.  In particular, if the given percentile falls outside of the bucket
 * range (i.e. your buckets range in 0 - 100,000 but the 99th percentile is
 * around 115,000) this estimate may be very wrong.
 *
 * The memory usage for a typical histogram is roughly 3k * (# of buckets).  All
 * insertion operations are amortized O(1), and all queries are O(# of buckets).
 */
template <class T, class TT=std::chrono::seconds,
          class C=folly::MultiLevelTimeSeries<T, TT>>
class TimeseriesHistogram {
 private:
   // NOTE: T must be equivalent to _signed_ numeric type for our math.
   static_assert(std::numeric_limits<T>::is_signed, "");

 public:
  // values to be inserted into container
  typedef T ValueType;
  // the container type we use internally for each bucket
  typedef C ContainerType;
  // The time type.
  typedef TT TimeType;

  /*
   * Create a TimeSeries histogram and initialize the bucketing and levels.
   *
   * The buckets are created by chopping the range [min, max) into pieces
   * of size bucketSize, with the last bucket being potentially shorter.  Two
   * additional buckets are always created -- the "under" bucket for the range
   * (-inf, min) and the "over" bucket for the range [max, +inf).
   *
   * @param bucketSize the width of each bucket
   * @param min the smallest value for the bucket range.
   * @param max the largest value for the bucket range
   * @param defaultContainer a pre-initialized timeseries with the desired
   *                         number of levels and their durations.
   */
  TimeseriesHistogram(ValueType bucketSize, ValueType min, ValueType max,
                      const ContainerType& defaultContainer);

  /* Return the bucket size of each bucket in the histogram. */
  ValueType getBucketSize() const { return buckets_.getBucketSize(); }

  /* Return the min value at which bucketing begins. */
  ValueType getMin() const { return buckets_.getMin(); }

  /* Return the max value at which bucketing ends. */
  ValueType getMax() const { return buckets_.getMax(); }

  /* Return the number of levels of the Timeseries object in each bucket */
  int getNumLevels() const {
    return buckets_.getByIndex(0).numLevels();
  }

  /* Return the number of buckets */
  int getNumBuckets() const { return buckets_.getNumBuckets(); }

  /*
   * Return the threshold of the bucket for the given index in range
   * [0..numBuckets).  The bucket will have range [thresh, thresh + bucketSize)
   * or [thresh, max), whichever is shorter.
   */
  ValueType getBucketMin(int bucketIdx) const {
    return buckets_.getBucketMin(bucketIdx);
  }

  /* Return the actual timeseries in the given bucket (for reading only!) */
  const ContainerType& getBucket(int bucketIdx) const {
    return buckets_.getByIndex(bucketIdx);
  }

  /* Total count of values at the given timeseries level (all buckets). */
  int64_t count(int level) const {
    int64_t total = 0;
    for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
      total += buckets_.getByIndex(b).count(level);
    }
    return total;
  }

  /* Total count of values added during the given interval (all buckets). */
  int64_t count(TimeType start, TimeType end) const {
    int64_t total = 0;
    for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
      total += buckets_.getByIndex(b).count(start, end);
    }
    return total;
  }

  /* Total sum of values at the given timeseries level (all buckets). */
  ValueType sum(int level) const {
    ValueType total = ValueType();
    for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
      total += buckets_.getByIndex(b).sum(level);
    }
    return total;
  }

  /* Total sum of values added during the given interval (all buckets). */
  ValueType sum(TimeType start, TimeType end) const {
    ValueType total = ValueType();
    for (unsigned int b = 0; b < buckets_.getNumBuckets(); ++b) {
      total += buckets_.getByIndex(b).sum(start, end);
    }
    return total;
  }

  /* Average of values at the given timeseries level (all buckets). */
  template <typename ReturnType=double>
  ReturnType avg(int level) const;

  /* Average of values added during the given interval (all buckets). */
  template <typename ReturnType=double>
  ReturnType avg(TimeType start, TimeType end) const;

  /*
   * Rate at the given timeseries level (all buckets).
   * This is the sum of all values divided by the time interval (in seconds).
   */
  ValueType rate(int level) const;

  /*
   * Rate for the given interval (all buckets).
   * This is the sum of all values divided by the time interval (in seconds).
   */
  template <typename ReturnType=double>
  ReturnType rate(TimeType start, TimeType end) const;

  /*
   * Update every underlying timeseries object with the given timestamp. You
   * must call this directly before querying to ensure that the data in all
   * buckets is decayed properly.
   */
  void update(TimeType now);

  /* clear all the data from the histogram. */
  void clear();

  /* Add a value into the histogram with timestamp 'now' */
  void addValue(TimeType now, const ValueType& value);
  /* Add a value the given number of times with timestamp 'now' */
  void addValue(TimeType now, const ValueType& value, int64_t times);

  /*
   * Add all of the values from the specified histogram.
   *
   * All of the values will be added to the current time-slot.
   *
   * One use of this is for thread-local caching of frequently updated
   * histogram data.  For example, each thread can store a thread-local
   * Histogram that is updated frequently, and only add it to the global
   * TimeseriesHistogram once a second.
   */
  void addValues(TimeType now, const folly::Histogram<ValueType>& values);

  /*
   * Return an estimate of the value at the given percentile in the histogram
   * in the given timeseries level.  The percentile is estimated as follows:
   *
   * - We retrieve a count of the values in each bucket (at the given level)
   * - We determine via the counts which bucket the given percentile falls in.
   * - We assume the average value in the bucket is also its median
   * - We then linearly interpolate within the bucket, by assuming that the
   *   distribution is uniform in the two value ranges [left, median) and
   *   [median, right) where [left, right) is the bucket value range.
   *
   * Caveats:
   * - If the histogram is empty, this always returns ValueType(), usually 0.
   * - For the 'under' and 'over' special buckets, their range is unbounded
   *   on one side.  In order for the interpolation to work, we assume that
   *   the average value in the bucket is equidistant from the two edges of
   *   the bucket.  In other words, we assume that the distance between the
   *   average and the known bound is equal to the distance between the average
   *   and the unknown bound.
   */
  ValueType getPercentileEstimate(double pct, int level) const;
  /*
   * Return an estimate of the value at the given percentile in the histogram
   * in the given historical interval.  Please see the documentation for
   * getPercentileEstimate(int pct, int level) for the explanation of the
   * estimation algorithm.
   */
  ValueType getPercentileEstimate(double pct, TimeType start, TimeType end)
    const;

  /*
   * Return the bucket index that the given percentile falls into (in the
   * given timeseries level).  This index can then be used to retrieve either
   * the bucket threshold, or other data from inside the bucket.
   */
  int getPercentileBucketIdx(double pct, int level) const;
  /*
   * Return the bucket index that the given percentile falls into (in the
   * given historical interval).  This index can then be used to retrieve either
   * the bucket threshold, or other data from inside the bucket.
   */
  int getPercentileBucketIdx(double pct, TimeType start, TimeType end) const;

  /* Get the bucket threshold for the bucket containing the given pct. */
  int getPercentileBucketMin(double pct, int level) const {
    return getBucketMin(getPercentileBucketIdx(pct, level));
  }
  /* Get the bucket threshold for the bucket containing the given pct. */
  int getPercentileBucketMin(double pct, TimeType start, TimeType end) const {
    return getBucketMin(getPercentileBucketIdx(pct, start, end));
  }

  /*
   * Print out serialized data from all buckets at the given level.
   * Format is: BUCKET [',' BUCKET ...]
   * Where: BUCKET == bucketMin ':' count ':' avg
   */
  std::string getString(int level) const;

  /*
   * Print out serialized data for all buckets in the historical interval.
   * For format, please see getString(int level).
   */
  std::string getString(TimeType start, TimeType end) const;

 private:
  typedef ContainerType Bucket;
  struct CountFromLevel {
    explicit CountFromLevel(int level) : level_(level) {}

    uint64_t operator()(const ContainerType& bucket) const {
      return bucket.count(level_);
    }

   private:
    int level_;
  };
  struct CountFromInterval {
    explicit CountFromInterval(TimeType start, TimeType end)
      : start_(start),
        end_(end) {}

    uint64_t operator()(const ContainerType& bucket) const {
      return bucket.count(start_, end_);
    }

   private:
    TimeType start_;
    TimeType end_;
  };

  struct AvgFromLevel {
    explicit AvgFromLevel(int level) : level_(level) {}

    ValueType operator()(const ContainerType& bucket) const {
      return bucket.template avg<ValueType>(level_);
    }

   private:
    int level_;
  };

  template <typename ReturnType>
  struct AvgFromInterval {
    explicit AvgFromInterval(TimeType start, TimeType end)
      : start_(start),
        end_(end) {}

    ReturnType operator()(const ContainerType& bucket) const {
      return bucket.template avg<ReturnType>(start_, end_);
    }

   private:
    TimeType start_;
    TimeType end_;
  };

  /*
   * Special logic for the case of only one unique value registered
   * (this can happen when clients don't pick good bucket ranges or have
   * other bugs).  It's a lot easier for clients to track down these issues
   * if they are getting the correct value.
   */
  void maybeHandleSingleUniqueValue(const ValueType& value);

  folly::detail::HistogramBuckets<ValueType, ContainerType> buckets_;
  bool haveNotSeenValue_;
  bool singleUniqueValue_;
  ValueType firstValue_;
};

}  // folly
