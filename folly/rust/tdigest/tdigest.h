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
#include "rust/cxx.h"

namespace facebook::folly_rust::tdigest {
std::unique_ptr<folly::TDigest> new_tdigest(size_t max_size) noexcept;
std::unique_ptr<std::vector<folly::TDigest>> new_tdigest_vec() noexcept;
bool add_tdigest(
    std::vector<folly::TDigest>& vector, const folly::TDigest& tdigest);
std::unique_ptr<folly::TDigest> new_tdigest_with_centroids(
    const rust::Vec<double>& centroidMeans,
    const rust::Vec<double>& centroidWeights,
    double sum,
    double count,
    double maxVal,
    double minVal,
    size_t maxSize) noexcept;
std::unique_ptr<folly::TDigest> new_tdigest_with_unsorted_values(
    size_t maxSize, const rust::Vec<double>& unsortedValues) noexcept;

std::unique_ptr<folly::TDigest> merge(
    const std::vector<folly::TDigest>& tdigestVec);
rust::Vec<double> extract_centroid_means(const folly::TDigest& tdigest);
rust::Vec<double> extract_centroid_weights(const folly::TDigest& tdigest);
} // namespace facebook::folly_rust::tdigest
