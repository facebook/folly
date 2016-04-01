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

#include <glog/logging.h>

namespace folly {

template <typename VT, typename TT>
MultiLevelTimeSeries<VT, TT>::MultiLevelTimeSeries(
  size_t nBuckets,
  size_t nLevels,
  const TimeType levelDurations[])
    : cachedTime_(0),
      cachedSum_(0),
      cachedCount_(0) {
    CHECK_GT(nLevels, 0);
    CHECK(levelDurations);

    levels_.reserve(nLevels);
    for (size_t i = 0; i < nLevels; ++i) {
      if (levelDurations[i] == TT(0)) {
        CHECK_EQ(i, nLevels - 1);
      } else if (i > 0) {
        CHECK(levelDurations[i-1] < levelDurations[i]);
      }
      levels_.emplace_back(nBuckets, levelDurations[i]);
    }
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::addValue(TimeType now,
                                            const ValueType& val) {
  addValueAggregated(now, val, 1);
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::addValue(TimeType now,
                                            const ValueType& val,
                                            int64_t times) {
  addValueAggregated(now, val * times, times);
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::addValueAggregated(TimeType now,
                                                      const ValueType& total,
                                                      int64_t nsamples) {
  if (cachedTime_ != now) {
    flush();
    cachedTime_ = now;
  }
  cachedSum_ += total;
  cachedCount_ += nsamples;
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::update(TimeType now) {
  flush();
  for (size_t i = 0; i < levels_.size(); ++i) {
    levels_[i].update(now);
  }
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::flush() {
  // update all the underlying levels
  if (cachedCount_ > 0) {
    for (size_t i = 0; i < levels_.size(); ++i) {
      levels_[i].addValueAggregated(cachedTime_, cachedSum_, cachedCount_);
    }
    cachedCount_ = 0;
    cachedSum_ = 0;
  }
}

template <typename VT, typename TT>
void MultiLevelTimeSeries<VT, TT>::clear() {
  for (auto & level : levels_) {
    level.clear();
  }

  cachedTime_ = TimeType(0);
  cachedSum_ = 0;
  cachedCount_ = 0;
}

}  // folly
