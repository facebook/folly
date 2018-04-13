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

#include <emmintrin.h>

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
double q_to_k(double q, double d) {
  if (q >= 0.5) {
    return d - d * std::sqrt(0.5 - 0.5 * q);
  }
  return d * std::sqrt(0.5 * q);
}

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

  result.total_ = total_ + sortedValues.size();

  std::vector<Centroid> compressed;
  compressed.reserve(2 * maxSize_);

  double q_0_times_total = 0.0;
  double q_limit_times_total = detail::k_to_q(1, maxSize_) * result.total_;

  auto it_centroids = centroids_.begin();
  auto it_sortedValues = sortedValues.begin();

  Centroid cur;
  if (it_centroids != centroids_.end() &&
      it_centroids->mean() < *it_sortedValues) {
    cur = *it_centroids++;
  } else {
    cur = Centroid(*it_sortedValues++, 1.0);
  }

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

    double q_times_total = q_0_times_total + cur.weight() + next.weight();

    if (q_times_total <= q_limit_times_total) {
      cur.add(next);
    } else {
      compressed.push_back(cur);
      q_0_times_total += cur.weight();
      double q_to_k_res =
          detail::q_to_k(q_0_times_total / result.total_, maxSize_);
      q_limit_times_total =
          detail::k_to_q(q_to_k_res + 1, maxSize_) * result.total_;
      cur = next;
    }
  }
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

  double total = 0;

  for (auto it = digests.begin(); it != digests.end(); it++) {
    for (const auto& centroid : it->centroids_) {
      centroids.push_back(centroid);
      total += centroid.weight();
    }
  }
  std::sort(centroids.begin(), centroids.end());

  size_t maxSize = digests.begin()->maxSize_;
  std::vector<Centroid> compressed;
  compressed.reserve(2 * maxSize);

  double q_0_times_total = 0.0;

  double q_limit_times_total = detail::k_to_q(1, maxSize) * total;

  Centroid cur = centroids.front();
  for (auto it = centroids.begin() + 1; it != centroids.end(); ++it) {
    double q_times_total = q_0_times_total + cur.weight() + it->weight();

    if (q_times_total <= q_limit_times_total) {
      cur.add(*it);
    } else {
      compressed.push_back(cur);
      q_0_times_total += cur.weight();
      double q_to_k_res = detail::q_to_k(q_0_times_total / total, maxSize);
      q_limit_times_total = detail::k_to_q(q_to_k_res + 1, maxSize) * total;
      cur = *it;
    }
  }
  compressed.push_back(cur);

  TDigest result(maxSize);
  result.total_ = total;
  result.centroids_ = std::move(compressed);
  return result;
}

double TDigest::estimateQuantile(double q) const {
  if (centroids_.empty()) {
    return 0.0;
  }
  double rank = q * total_;

  size_t pos;
  double t;
  if (q > 0.5) {
    if (q >= 1.0) {
      return centroids_.back().mean();
    }
    pos = 0;
    t = total_;
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

void TDigest::Centroid::add(const TDigest::Centroid& other) {
  auto means = _mm_set_pd(mean(), other.mean());
  auto weights = _mm_set_pd(weight(), other.weight());
  auto sums128 = _mm_mul_pd(means, weights);

  double* sums = reinterpret_cast<double*>(&sums128);

  double sum = sums[0] + sums[1];
  weight_ += other.weight();
  mean_ = sum / weight_;
}

} // namespace folly
