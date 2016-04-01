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

#include <chrono>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <folly/stats/BucketedTimeSeries.h>

namespace folly {

/*
 * This class represents a timeseries which keeps several levels of data
 * granularity (similar in principle to the loads reported by the UNIX
 * 'uptime' command).  It uses several instances (one per level) of
 * BucketedTimeSeries as the underlying storage.
 *
 * This can easily be used to track sums (and thus rates or averages) over
 * several predetermined time periods, as well as all-time sums.  For example,
 * you would use to it to track query rate or response speed over the last
 * 5, 15, 30, and 60 minutes.
 *
 * The MultiLevelTimeSeries takes a list of level durations as an input; the
 * durations must be strictly increasing.  Furthermore a special level can be
 * provided with a duration of '0' -- this will be an "all-time" level.  If
 * an all-time level is provided, it MUST be the last level present.
 *
 * The class assumes that time advances forward --  you can't retroactively add
 * values for events in the past -- the 'now' argument is provided for better
 * efficiency and ease of unittesting.
 *
 * The class is not thread-safe -- use your own synchronization!
 */
template <typename VT, typename TT=std::chrono::seconds>
class MultiLevelTimeSeries {
 public:
  typedef VT ValueType;
  typedef TT TimeType;
  typedef folly::BucketedTimeSeries<ValueType, TimeType> Level;

  /*
   * Create a new MultiLevelTimeSeries.
   *
   * This creates a new MultiLevelTimeSeries that tracks time series data at the
   * specified time durations (level). The time series data tracked at each
   * level is then further divided by numBuckets for memory efficiency.
   *
   * The durations must be strictly increasing. Furthermore a special level can
   * be provided with a duration of '0' -- this will be an "all-time" level. If
   * an all-time level is provided, it MUST be the last level present.
   */
  MultiLevelTimeSeries(size_t numBuckets,
                       size_t numLevels,
                       const TimeType levelDurations[]);

  /*
   * Return the number of buckets used to track time series at each level.
   */
  size_t numBuckets() const {
    // The constructor ensures that levels_ has at least one item
    return levels_[0].numBuckets();
  }

  /*
   * Return the number of levels tracked by MultiLevelTimeSeries.
   */
  size_t numLevels() const { return levels_.size(); }

  /*
   * Get the BucketedTimeSeries backing the specified level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  const Level& getLevel(int level) const {
    CHECK(level >= 0);
    CHECK_LT(level, levels_.size());
    return levels_[level];
  }

  /*
   * Get the highest granularity level that is still large enough to contain
   * data going back to the specified start time.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  const Level& getLevel(TimeType start) const {
    for (const auto& level : levels_) {
      if (level.isAllTime()) {
        return level;
      }
      // Note that we use duration() here rather than elapsed().
      // If duration is large enough to contain the start time then this level
      // is good enough, even if elapsed() indicates that no data was recorded
      // before the specified start time.
      if (level.getLatestTime() - level.duration() <= start) {
        return level;
      }
    }
    // We should always have an all-time level, so this is never reached.
    LOG(FATAL) << "No level of timeseries covers internval"
               << " from " << start.count() << " to now";
    return levels_.back();
  }

  /*
   * Return the sum of all the data points currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  ValueType sum(int level) const {
    return getLevel(level).sum();
  }

  /*
   * Return the average (sum / count) of all the data points currently tracked
   * at this level.
   *
   * The return type may be specified to control whether floating-point or
   * integer division should be performed.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  template <typename ReturnType=double>
  ReturnType avg(int level) const {
    return getLevel(level).template avg<ReturnType>();
  }

  /*
   * Return the rate (sum divided by elaspsed time) of the all data points
   * currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  template <typename ReturnType=double, typename Interval=TimeType>
  ReturnType rate(int level) const {
    return getLevel(level).template rate<ReturnType, Interval>();
  }

  /*
   * Return the number of data points currently tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  int64_t count(int level) const {
    return getLevel(level).count();
  }

  /*
   * Return the count divided by the elapsed time tracked at this level.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  template <typename ReturnType=double, typename Interval=TimeType>
  ReturnType countRate(int level) const {
    return getLevel(level).template countRate<ReturnType, Interval>();
  }

  /*
   * Estimate the sum of the data points that occurred in the specified time
   * period at this level.
   *
   * The range queried is [start, end).
   * That is, start is inclusive, and end is exclusive.
   *
   * Note that data outside of the timeseries duration will no longer be
   * available for use in the estimation.  Specifying a start time earlier than
   * getEarliestTime() will not have much effect, since only data points after
   * that point in time will be counted.
   *
   * Note that the value returned is an estimate, and may not be precise.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  ValueType sum(TimeType start, TimeType end) const {
    return getLevel(start).sum(start, end);
  }

  /*
   * Estimate the average value during the specified time period.
   *
   * The same caveats documented in the sum(TimeType start, TimeType end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  template <typename ReturnType=double>
  ReturnType avg(TimeType start, TimeType end) const {
    return getLevel(start).template avg<ReturnType>(start, end);
  }

  /*
   * Estimate the rate during the specified time period.
   *
   * The same caveats documented in the sum(TimeType start, TimeType end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  template <typename ReturnType=double>
  ReturnType rate(TimeType start, TimeType end) const {
    return getLevel(start).template rate<ReturnType>(start, end);
  }

  /*
   * Estimate the count during the specified time period.
   *
   * The same caveats documented in the sum(TimeType start, TimeType end)
   * comments apply here as well.
   *
   * Note: you should generally call update() or flush() before accessing the
   * data. Otherwise you may be reading stale data if update() or flush() has
   * not been called recently.
   */
  int64_t count(TimeType start, TimeType end) const {
    return getLevel(start).count(start, end);
  }

  /*
   * Adds the value 'val' at time 'now' to all levels.
   *
   * Data points added at the same time point is cached internally here and not
   * propagated to the underlying levels until either flush() is called or when
   * update from a different time comes.
   *
   * This function expects time to always move forwards: it cannot be used to
   * add historical data points that have occurred in the past.  If now is
   * older than the another timestamp that has already been passed to
   * addValue() or update(), now will be ignored and the latest timestamp will
   * be used.
   */
  void addValue(TimeType now, const ValueType& val);

  /*
   * Adds the value 'val' at time 'now' to all levels.
   */
  void addValue(TimeType now, const ValueType& val, int64_t times);

  /*
   * Adds the value 'val' at time 'now' to all levels as the sum of 'nsamples'
   * samples.
   */
  void addValueAggregated(TimeType now, const ValueType& sum, int64_t nsamples);

  /*
   * Update all the levels to the specified time, doing all the necessary
   * work to rotate the buckets and remove any stale data points.
   *
   * When reading data from the timeseries, you should make sure to manually
   * call update() before accessing the data. Otherwise you may be reading
   * stale data if update() has not been called recently.
   */
  void update(TimeType now);

  /*
   * Reset all the timeseries to an empty state as if no data points have ever
   * been added to it.
   */
  void clear();

  /*
   * Flush all cached updates.
   */
  void flush();

 private:
  std::vector<Level> levels_;

  // Updates within the same time interval are cached
  // They are flushed out when updates from a different time comes,
  // or flush() is called.
  TimeType cachedTime_;
  ValueType cachedSum_;
  int cachedCount_;
};

} // folly
