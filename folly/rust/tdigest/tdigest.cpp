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

#include <folly/rust/tdigest/tdigest.h>

namespace facebook::folly_rust::tdigest {
using folly::TDigest;

std::unique_ptr<TDigest> new_tdigest(size_t max_size) noexcept {
  return std::make_unique<TDigest>(max_size);
}

std::unique_ptr<std::vector<TDigest>> new_tdigest_vec() noexcept {
  return std::make_unique<std::vector<TDigest>>();
}

bool add_tdigest(std::vector<TDigest>& vector, const TDigest& tdigest) {
  vector.push_back(tdigest);
  return true;
}

std::unique_ptr<TDigest> new_tdigest_with_centroids(
    const rust::Vec<double>& centroidMeans,
    const rust::Vec<double>& centroidWeights,
    double sum,
    double count,
    double maxVal,
    double minVal,
    size_t max_size) noexcept {
  assert(centroidMeans.size() == centroidWeights.size());

  std::vector<TDigest::Centroid> tdigestCentroids;
  tdigestCentroids.reserve(centroidMeans.size());
  for (size_t index = 0; index < centroidMeans.size(); index++) {
    tdigestCentroids.emplace_back(
        centroidMeans.at(index), centroidWeights.at(index));
  }
  return std::make_unique<TDigest>(
      std::move(tdigestCentroids), sum, count, maxVal, minVal, max_size);
}

std::unique_ptr<TDigest> new_tdigest_with_unsorted_values(
    size_t maxSize, const rust::Vec<double>& unsortedValues) noexcept {
  TDigest digest(maxSize);
  std::vector<double> stdUnsortedValues;
  std::copy(
      unsortedValues.begin(),
      unsortedValues.end(),
      std::back_inserter(stdUnsortedValues));
  return std::make_unique<TDigest>(digest.merge(stdUnsortedValues));
}

std::unique_ptr<TDigest> merge(const std::vector<TDigest>& tdigestVec) {
  return std::make_unique<TDigest>(TDigest::merge(tdigestVec));
}

rust::Vec<double> extract_centroid_means(const TDigest& tdigest) {
  auto& centroids = tdigest.getCentroids();
  rust::Vec<double> centroidMeans;
  centroidMeans.reserve(centroids.size());
  for (auto& centroid : centroids) {
    centroidMeans.push_back(centroid.mean());
  }

  return centroidMeans;
}

rust::Vec<double> extract_centroid_weights(const TDigest& tdigest) {
  auto& centroids = tdigest.getCentroids();
  rust::Vec<double> centroidWeights;
  centroidWeights.reserve(centroids.size());
  for (auto& centroid : centroids) {
    centroidWeights.push_back(centroid.weight());
  }

  return centroidWeights;
}

} // namespace facebook::folly_rust::tdigest
