/*
 * Copyright 2012-present Facebook, Inc.
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

#include <folly/stats/TDigest.h>

#include <algorithm>
#include <cmath>

namespace folly {

namespace detail {
/*
 * A good biased scaling function has the following properties:
 *   - The value of the function k(0, delta) = 0, and k(1, delta) = delta.
 *     This is a requirement for any t-digest function.
 *   - The limit of the derivative of the function dk/dq at 0 is inf, and at
 *     1 is inf. This provides bias to improve accuracy at the tails.
 *   - For any q <= 0.5, dk/dq(q) = dk/dq(1-q). This ensures that the accuracy
 *     of upper and lower quantiles are equivalent.
 *
 * The scaling function used here is...
 *   k(q, d) = (IF q >= 0.5, d - d * sqrt(2 - 2q) / 2, d * sqrt(2q) / 2)
 *
 *   k(0, d) = 0
 *   k(1, d) = d
 *
 *   dk/dq = (IF q >= 0.5, d / sqrt(2-2q), d / sqrt(2q))
 *   limit q->1 dk/dq = inf
 *   limit q->0 dk/dq = inf
 *
 *   When plotted, the derivative function is symmetric, centered at q=0.5.
 *
 * Note that FMA has been tested here, but benchmarks have not shown it to be a
 * performance improvement.
 */

/*
 * q_to_k is unused but left here as a comment for completeness.
 * double q_to_k(double q, double d) {
 *   if (q >= 0.5) {
 *     return d - d * std::sqrt(0.5 - 0.5 * q);
 *   }
 *   return d * std::sqrt(0.5 * q);
 * }
 */

double k_to_q(double k, double d) {
  double k_div_d = k / d;
  if (k_div_d >= 0.5) {
    double base = 1 - k_div_d;
    return 1 - 2 * base * base;
  } else {
    return 2 * k_div_d * k_div_d;
  }
}

} // namespace detail

TDigest TDigest::merge(Range<const double*> sortedValues) const {
  if (sortedValues.empty()) {
    return *this;
  }

  TDigest result(maxSize_);

  result.count_ = count_ + sortedValues.size();

  std::vector<Centroid> compressed;
  compressed.reserve(maxSize_);

  double k_limit = 1;
  double q_limit_times_count =
      detail::k_to_q(k_limit++, maxSize_) * result.count_;

  auto it_centroids = centroids_.begin();
  auto it_sortedValues = sortedValues.begin();

  Centroid cur;
  if (it_centroids != centroids_.end() &&
      it_centroids->mean() < *it_sortedValues) {
    cur = *it_centroids++;
  } else {
    cur = Centroid(*it_sortedValues++, 1.0);
  }

  double weightSoFar = cur.weight();

  // Keep track of sums along the way to reduce expensive floating points
  double sumsToMerge = 0;
  double weightsToMerge = 0;

  while (it_centroids != centroids_.end() ||
         it_sortedValues != sortedValues.end()) {
    Centroid next;

    if (it_centroids != centroids_.end() &&
        (it_sortedValues == sortedValues.end() ||
         it_centroids->mean() < *it_sortedValues)) {
      next = *it_centroids++;
    } else {
      next = Centroid(*it_sortedValues++, 1.0);
    }

    double nextSum = next.mean() * next.weight();
    weightSoFar += next.weight();

    if (weightSoFar <= q_limit_times_count) {
      sumsToMerge += nextSum;
      weightsToMerge += next.weight();
    } else {
      result.sum_ += cur.add(sumsToMerge, weightsToMerge);
      sumsToMerge = 0;
      weightsToMerge = 0;
      compressed.push_back(cur);
      q_limit_times_count = detail::k_to_q(k_limit++, maxSize_) * result.count_;
      cur = next;
    }
  }
  result.sum_ += cur.add(sumsToMerge, weightsToMerge);
  compressed.push_back(cur);
  result.centroids_ = std::move(compressed);
  return result;
}

TDigest TDigest::merge(Range<const TDigest*> digests) {
  size_t nCentroids = 0;
  for (auto it = digests.begin(); it != digests.end(); it++) {
    nCentroids += it->centroids_.size();
  }

  if (nCentroids == 0) {
    return TDigest();
  }

  std::vector<Centroid> centroids;
  centroids.reserve(nCentroids);

  std::vector<std::vector<Centroid>::iterator> starts;
  starts.reserve(digests.size());

  double count = 0;

  for (auto it = digests.begin(); it != digests.end(); it++) {
    starts.push_back(centroids.end());
    count += it->count();
    for (const auto& centroid : it->centroids_) {
      centroids.push_back(centroid);
    }
  }

  for (size_t digestsPerBlock = 1; digestsPerBlock < starts.size();
       digestsPerBlock *= 2) {
    // Each sorted block is digestPerBlock digests big. For each step, try to
    // merge two blocks together.
    for (size_t i = 0; i < starts.size(); i += (digestsPerBlock * 2)) {
      // It is possible that this block is incomplete (less than digestsPerBlock
      // big). In that case, the rest of the block is sorted and leave it alone
      if (i + digestsPerBlock < starts.size()) {
        auto first = starts[i];
        auto middle = starts[i + digestsPerBlock];

        // It is possible that the next block is incomplete (less than
        // digestsPerBlock big). In that case, merge to end. Otherwise, merge to
        // the end of that block.
        std::vector<Centroid>::iterator last =
            (i + (digestsPerBlock * 2) < starts.size())
            ? *(starts.begin() + i + 2 * digestsPerBlock)
            : centroids.end();
        std::inplace_merge(first, middle, last);
      }
    }
  }

  DCHECK(std::is_sorted(centroids.begin(), centroids.end()));

  size_t maxSize = digests.begin()->maxSize_;
  TDigest result(maxSize);

  std::vector<Centroid> compressed;
  compressed.reserve(maxSize);

  double k_limit = 1;
  double q_limit_times_count = detail::k_to_q(k_limit, maxSize) * count;

  Centroid cur = centroids.front();
  double weightSoFar = cur.weight();
  double sumsToMerge = 0;
  double weightsToMerge = 0;
  for (auto it = centroids.begin() + 1; it != centroids.end(); ++it) {
    weightSoFar += it->weight();
    if (weightSoFar <= q_limit_times_count) {
      sumsToMerge += it->mean() * it->weight();
      weightsToMerge += it->weight();
    } else {
      result.sum_ += cur.add(sumsToMerge, weightsToMerge);
      sumsToMerge = 0;
      weightsToMerge = 0;
      compressed.push_back(cur);
      q_limit_times_count = detail::k_to_q(k_limit++, maxSize) * count;
      cur = *it;
    }
  }
  result.sum_ += cur.add(sumsToMerge, weightsToMerge);
  compressed.push_back(cur);

  result.count_ = count;
  result.centroids_ = std::move(compressed);
  return result;
}

double TDigest::estimateQuantile(double q) const {
  if (centroids_.empty()) {
    return 0.0;
  }
  double rank = q * count_;

  size_t pos;
  double t;
  if (q > 0.5) {
    if (q >= 1.0) {
      return centroids_.back().mean();
    }
    pos = 0;
    t = count_;
    for (auto rit = centroids_.rbegin(); rit != centroids_.rend(); ++rit) {
      t -= rit->weight();
      if (rank >= t) {
        pos = std::distance(rit, centroids_.rend()) - 1;
        break;
      }
    }
  } else {
    if (q <= 0.0) {
      return centroids_.front().mean();
    }
    pos = centroids_.size() - 1;
    t = 0;
    for (auto it = centroids_.begin(); it != centroids_.end(); ++it) {
      if (rank < t + it->weight()) {
        pos = std::distance(centroids_.begin(), it);
        break;
      }
      t += it->weight();
    }
  }

  double delta = 0;
  if (centroids_.size() > 1) {
    if (pos == 0) {
      delta = centroids_[pos + 1].mean() - centroids_[pos].mean();
    } else if (pos == centroids_.size() - 1) {
      delta = centroids_[pos].mean() - centroids_[pos - 1].mean();
    } else {
      delta = (centroids_[pos + 1].mean() - centroids_[pos - 1].mean()) / 2;
    }
  }
  return centroids_[pos].mean() +
      ((rank - t) / centroids_[pos].weight() - 0.5) * delta;
}

double TDigest::Centroid::add(double sum, double weight) {
  sum += (mean_ * weight_);
  weight_ += weight;
  mean_ = sum / weight_;
  return sum;
}

} // namespace folly
