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

#include <folly/stats/TDigest.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include <glog/logging.h>

#include <folly/Overload.h>
#include <folly/Utility.h>
#include <folly/algorithm/BinaryHeap.h>
#include <folly/memory/Malloc.h>
#include <folly/stats/detail/DoubleRadixSort.h>

namespace folly {

namespace {

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

template <class Container1, class Container2, class Compare, class Callback>
void merge2Containers(
    const Container1& c1, const Container2& c2, Compare&& cmp, Callback&& cb) {
  auto f1 = c1.begin();
  const auto l1 = c1.end();
  auto f2 = c2.begin();
  const auto l2 = c2.end();
  while (f1 != l1 || f2 != l2) {
    // In case of ties, c2 takes priority.
    if (f1 != l1 && (f2 == l2 || cmp(*f1, *f2))) {
      cb(*f1++);
    } else {
      cb(*f2++);
    }
  }
}

class Buffer : NonCopyableNonMovable {
 public:
  explicit Buffer(size_t size)
      : ptr_(reinterpret_cast<char*>(checkedMalloc(size))), size_(size) {}
  ~Buffer() { sizedFree(ptr_, size_); }

  char* get() { return ptr_; }

 private:
  char* ptr_;
  size_t size_;
};

double clamp(double v, double lo, double hi) {
  if (v > hi) {
    return hi;
  } else if (v < lo) {
    return lo;
  }
  return v;
}

} // namespace

class TDigest::CentroidMerger {
 public:
  CentroidMerger(
      std::vector<Centroid>&& workingBuffer, size_t maxSize, double count)
      : result_(std::move(workingBuffer)),
        maxSize_(maxSize),
        count_(count),
        k_limit_(1),
        q_limit_times_count_(k_to_q(k_limit_++, maxSize_) * count_) {
    result_.clear();
  }

  FOLLY_ALWAYS_INLINE void append(const Centroid& centroid) {
    if constexpr (kIsDebug) {
      CHECK(!(centroid < last_));
      last_ = centroid;
    }

    if (!cur_) { // First centroid.
      cur_ = centroid;
      weightSoFar_ = centroid.weight();
      return;
    }

    weightSoFar_ += centroid.weight();
    if (weightSoFar_ <= q_limit_times_count_ || k_limit_ > maxSize_) {
      sumsToMerge_ += centroid.mean() * centroid.weight();
      weightsToMerge_ += centroid.weight();
    } else {
      sum_ += cur_->add(sumsToMerge_, weightsToMerge_);
      sumsToMerge_ = 0;
      weightsToMerge_ = 0;
      commit(*cur_);
      q_limit_times_count_ = k_to_q(k_limit_++, maxSize_) * count_;
      cur_ = centroid;
    }
  }

  std::pair<std::vector<Centroid>, double> finalize() && {
    if (!cur_) {
      return {}; // No centroids, no sum.
    }

    sum_ += cur_->add(sumsToMerge_, weightsToMerge_);
    commit(*cur_);
    DCHECK_LE(result_.size(), maxSize_);
    DCHECK(std::is_sorted(result_.begin(), result_.end()));
    return {std::move(result_), sum_};
  }

 private:
  void commit(const Centroid& c) {
    // Since the input centroids are sorted, the resulting centroids should also
    // be sorted, but floating point error accumulation can cause perturbations
    // in the order. These should be small and extremely rare, so we can use
    // insertion sort to optimize for the case that there is nothing to do.
    auto it = result_.end();
    while (FOLLY_UNLIKELY(it != result_.begin() && c < *(it - 1))) {
      --it;
    }
    result_.insert(it, c);
  }

  std::vector<Centroid> result_;
  const size_t maxSize_;
  const double count_;
  double k_limit_;
  double q_limit_times_count_;
  double sum_ = 0.0;

  std::optional<Centroid> cur_;
  double weightSoFar_ = 0.0;
  // Keep track of sums along the way to reduce expensive floating point ops.
  double sumsToMerge_ = 0.0;
  double weightsToMerge_ = 0.0;

  Centroid last_{std::numeric_limits<double>::lowest()}; // Only for debug.
};

TDigest::TDigest(
    std::vector<Centroid> centroids,
    double sum,
    double count,
    double max_val,
    double min_val,
    size_t maxSize)
    : maxSize_(maxSize),
      sum_(sum),
      count_(count),
      max_(max_val),
      min_(min_val) {
  if (centroids.size() <= maxSize_) {
    centroids_ = std::move(centroids);
  } else {
    // Number of centroids is greater than maxSize, we need to compress them
    // When merging, resulting digest takes the maxSize of the first digest
    auto sz = centroids.size();
    std::array<TDigest, 2> digests{{
        TDigest(maxSize_),
        TDigest(std::move(centroids), sum_, count_, max_, min_, sz),
    }};
    *this = this->merge(digests);
  }
}

// Merge unsorted values by first sorting them.
TDigest TDigest::merge(Range<const double*> unsortedValues) const {
  constexpr size_t kRadixSortThreshold = 700;

  auto n = unsortedValues.size();

  if (n > kRadixSortThreshold) {
    // Use radix sort if the set is large enough.  This implementation puts all
    // additional memory in the heap, so that if
    // called from fiber context we do not smash the stack.  Otherwise it is
    // very similar to boost::spreadsort.

    // We require 256 buckets per byte level, plus one count array we can reuse.
    size_t kBucketsSize = 256 * 9;
    static_assert(alignof(double) == alignof(uint64_t));
    Buffer buffer(kBucketsSize * sizeof(uint64_t) + 2 * n * sizeof(double));
    auto* p = buffer.get();
    uint64_t* buckets = reinterpret_cast<uint64_t*>(p);
    p += kBucketsSize * sizeof(uint64_t);
    double* in = reinterpret_cast<double*>(p);
    p += n * sizeof(double);
    double* out = reinterpret_cast<double*>(p);

    std::copy(unsortedValues.begin(), unsortedValues.end(), in);
    detail::double_radix_sort(n, buckets, in, out);

    return merge(sorted_equivalent, Range<const double*>(in, in + n));
  } else {
    // Set is small, prefer avoiding allocations. Temporary buffer is small
    // enough that we can keep it on the stack.
    double in[kRadixSortThreshold];
    std::copy(unsortedValues.begin(), unsortedValues.end(), in);
    std::sort(in, in + n);
    return merge(sorted_equivalent, Range<const double*>(in, in + n));
  }
}

void TDigest::mergeValues(
    TDigest& dst,
    Range<const double*> sortedValues,
    std::vector<Centroid>& workingBuffer) const {
  if (sortedValues.empty()) {
    return;
  }

  const double newCount = count_ + sortedValues.size();
  double newMin = 0.0;
  double newMax = 0.0;

  double maybeMin = *sortedValues.begin();
  double maybeMax = *(sortedValues.end() - 1);

  if (count_ > 0) {
    // We know that min_ and max_ are numbers
    newMin = std::min(min_, maybeMin);
    newMax = std::max(max_, maybeMax);
  } else {
    // We know that min_ and max_ are NaN.
    newMin = maybeMin;
    newMax = maybeMax;
  }

  CentroidMerger merger(std::move(workingBuffer), maxSize_, newCount);

  merge2Containers(
      centroids_,
      sortedValues,
      [](const Centroid& c, double val) { return c.mean() < val; },
      overload(
          [&](const Centroid& c) { merger.append(c); },
          [&](double val) { merger.append(Centroid{val, 1.0}); }));

  workingBuffer = std::move(dst.centroids_);
  std::tie(dst.centroids_, dst.sum_) = std::move(merger).finalize();
  dst.count_ = newCount;
  dst.max_ = newMax;
  dst.min_ = newMin;
}

TDigest TDigest::merge(
    sorted_equivalent_t, Range<const double*> sortedValues) const {
  if (sortedValues.empty()) {
    return *this;
  }

  TDigest result(maxSize_);

  std::vector<Centroid> compressed;
  compressed.reserve(maxSize_);

  mergeValues(result, sortedValues, compressed);

  result.centroids_.shrink_to_fit();

  return result;
}

void TDigest::merge(
    sorted_equivalent_t,
    Range<const double*> sortedValues,
    MergeWorkingBuffer& workingBuffer) {
  if (sortedValues.empty()) {
    return;
  }

  workingBuffer.buf.reserve(maxSize_);
  mergeValues(*this, sortedValues, workingBuffer.buf);
}

namespace {

const TDigest* getPtr(const TDigest& d) {
  return &d;
}
const TDigest* getPtr(const TDigest* d) {
  return d;
}

} // namespace

template <class T>
/* static */ TDigest TDigest::mergeImpl(Range<T> digests) {
  if (digests.empty()) {
    return TDigest();
  } else if (digests.size() == 2) {
    return merge2Impl(*getPtr(digests[0]), *getPtr(digests[1]));
  }

  size_t maxSize = getPtr(digests.front())->maxSize_;

  size_t nCentroids = 0;
  const TDigest* lastNonEmpty = nullptr;
  for (const auto& digest : digests) {
    if (const auto* d = getPtr(digest); !d->empty()) {
      nCentroids += d->centroids_.size();
      lastNonEmpty = d;
    }
  }

  if (nCentroids == 0) {
    return TDigest(maxSize);
  } else if (
      nCentroids == lastNonEmpty->centroids_.size() &&
      lastNonEmpty->maxSize_ == maxSize) {
    // Only one non-empty digest and it already has the desidered maxSize, we
    // can skip merge.
    return *lastNonEmpty;
  }

  struct Cursor : folly::Range<const Centroid*> {
    using folly::Range<const Centroid*>::Range;

    bool operator<(const Cursor& rhs) const {
      // In a max-heap we want the top to be the minimum.
      return front().mean() > rhs.front().mean();
    }
  };

  std::vector<Cursor> cursors;
  cursors.reserve(digests.size());

  double count = 0;

  // We can safely use these limits to avoid isnan checks below because we know
  // nCentroids > 0, so at least one TDigest has a min and max.
  double min = std::numeric_limits<double>::infinity();
  double max = -std::numeric_limits<double>::infinity();

  for (const auto& d : digests) {
    const auto& digest = *getPtr(d);
    double curCount = digest.count();
    if (curCount > 0) {
      DCHECK(!std::isnan(digest.min_));
      DCHECK(!std::isnan(digest.max_));
      min = std::min(min, digest.min_);
      max = std::max(max, digest.max_);
      count += curCount;

      DCHECK(!digest.centroids_.empty());
      cursors.emplace_back(digest.centroids_);
    }
  }

  // Use a heap to iterate the union of the centroids to merge in sorted order.
  std::make_heap(cursors.begin(), cursors.end());

  std::vector<Centroid> workingBuffer;
  workingBuffer.reserve(maxSize);
  CentroidMerger merger(std::move(workingBuffer), maxSize, count);
  while (!cursors.empty()) {
    auto& top = cursors.front();
    merger.append(top.front());
    top.pop_front();
    if (top.empty()) {
      top = cursors.back();
      cursors.pop_back();
    }
    down_heap(cursors.begin(), cursors.end());
  }

  TDigest result(maxSize);
  std::tie(result.centroids_, result.sum_) = std::move(merger).finalize();
  result.count_ = count;
  result.min_ = min;
  result.max_ = max;

  result.centroids_.shrink_to_fit();
  return result;
}

/* static */ TDigest TDigest::merge2Impl(const TDigest& d1, const TDigest& d2) {
  size_t maxSize = d1.maxSize_;

  if (d2.empty()) {
    return d1;
  } else if (d1.empty() && maxSize == d2.maxSize_) {
    return d2;
  }

  double count = 0;
  double min = std::numeric_limits<double>::infinity();
  double max = -std::numeric_limits<double>::infinity();

  for (const auto* digest : {&d1, &d2}) {
    if (digest->count() > 0) {
      count += digest->count();
      DCHECK(!std::isnan(digest->min_));
      DCHECK(!std::isnan(digest->max_));
      min = std::min(min, digest->min_);
      max = std::max(max, digest->max_);
    }
  }

  std::vector<Centroid> workingBuffer;
  workingBuffer.reserve(maxSize);
  CentroidMerger merger(std::move(workingBuffer), maxSize, count);

  merge2Containers(
      d1.centroids_, d2.centroids_, std::less<>(), [&](const Centroid& c) {
        merger.append(c);
      });

  TDigest result(maxSize);
  std::tie(result.centroids_, result.sum_) = std::move(merger).finalize();
  result.count_ = count;
  result.min_ = min;
  result.max_ = max;

  result.centroids_.shrink_to_fit();
  return result;
}

/* static */ TDigest TDigest::merge(Range<const TDigest*> digests) {
  return mergeImpl(digests);
}

/* static */ TDigest TDigest::merge(Range<const TDigest**> digests) {
  return mergeImpl(digests);
}

/* static */ TDigest TDigest::merge(const TDigest& d1, const TDigest& d2) {
  return merge2Impl(d1, d2);
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
      return max_;
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
      return min_;
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
  double min = min_;
  double max = max_;
  if (centroids_.size() > 1) {
    if (pos == 0) {
      delta = centroids_[pos + 1].mean() - centroids_[pos].mean();
      // Numerical errors in accumulating the centroid can make it exceed the
      // min_/max_ bounds.
      max = std::min(centroids_[pos + 1].mean(), max_);
    } else if (pos == centroids_.size() - 1) {
      delta = centroids_[pos].mean() - centroids_[pos - 1].mean();
      min = std::max(centroids_[pos - 1].mean(), min_);
    } else {
      delta = (centroids_[pos + 1].mean() - centroids_[pos - 1].mean()) / 2;
      min = std::max(centroids_[pos - 1].mean(), min_);
      max = std::min(centroids_[pos + 1].mean(), max_);
    }
  }
  auto value = centroids_[pos].mean() +
      ((rank - t) / centroids_[pos].weight() - 0.5) * delta;
  return clamp(value, min, max);
}

double TDigest::Centroid::add(double sum, double weight) {
  sum += (mean_ * weight_);
  weight_ += weight;
  mean_ = sum / weight_;
  return sum;
}

} // namespace folly
