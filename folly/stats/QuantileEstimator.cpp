/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/stats/QuantileEstimator.h>

namespace folly {
namespace detail {

QuantileEstimates estimatesFromDigest(
    const TDigest& digest,
    Range<const double*> quantiles) {
  QuantileEstimates result;
  result.quantiles.reserve(quantiles.size());
  result.sum = digest.sum();
  result.count = digest.count();
  for (auto& quantile : quantiles) {
    result.quantiles.push_back(
        std::make_pair(quantile, digest.estimateQuantile(quantile)));
  }
  return result;
}

} // namespace detail

} // namespace folly
