# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

folly_add_library(
  NAME bucketed_time_series
  HEADERS
    BucketedTimeSeries-inl.h
    BucketedTimeSeries.h
  EXPORTED_DEPS
    folly_likely
    folly_stats_detail_bucket
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME histogram
  HEADERS
    Histogram-inl.h
    Histogram.h
  EXPORTED_DEPS
    folly_c_portability
    folly_conv
    folly_lang_exception
    folly_stats_detail_bucket
    folly_traits
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME streaming_stats
  HEADERS
    StreamingStats.h
  EXPORTED_DEPS
    folly_lang_exception
)

folly_add_library(
  NAME multi_level_time_series
  HEADERS
    MultiLevelTimeSeries-inl.h
    MultiLevelTimeSeries.h
  EXPORTED_DEPS
    folly_constexpr_math
    folly_stats_bucketed_time_series
    folly_string
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME quantile_estimator
  SRCS
    QuantileEstimator.cpp
  HEADERS
    QuantileEstimator-inl.h
    QuantileEstimator.h
  EXPORTED_DEPS
    folly_stats_detail_buffered_stat
    folly_stats_tdigest
)

folly_add_library(
  NAME quantile_histogram
  HEADERS
    QuantileHistogram-inl.h
    QuantileHistogram.h
  EXPORTED_DEPS
    folly_conv
    folly_glog
    folly_range
)

folly_add_library(
  NAME tdigest
  SRCS
    TDigest.cpp
  HEADERS
    TDigest.h
  DEPS
    folly_algorithm_binary_heap
    folly_lang_exception
    folly_memory_malloc
    folly_overload
    folly_stats_detail_double_radix_sort
  EXPORTED_DEPS
    folly_range
    folly_utility
  EXTERNAL_DEPS
    ${GLOG_LIBRARIES}
)

folly_add_library(
  NAME digest_builder
  HEADERS
    DigestBuilder-inl.h
    DigestBuilder.h
  EXPORTED_DEPS
    folly_concurrency_cache_locality
    folly_lang_bits
    folly_memory
    folly_spin_lock
)

folly_add_library(
  NAME timeseries_histogram
  HEADERS
    TimeseriesHistogram-inl.h
    TimeseriesHistogram.h
  EXPORTED_DEPS
    folly_stats_histogram
    folly_stats_multi_level_time_series
)

add_subdirectory(detail)
