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

namespace folly {

/*static*/
template <class Q>
QuantileHistogram<Q> QuantileHistogram<Q>::merge(
    Range<const ::folly::QuantileHistogram<Q>*> qhists) {
  if (qhists.empty()) {
    return QuantileHistogram<Q>{};
  }

  QuantileHistogram<Q> merged{qhists[0]};
  if (qhists.size() == 1) {
    return merged;
  }
  merged.count_ = 0;

  for (const auto& qhist : qhists) {
    merged.count_ += qhist.count();
    if (qhist.min() < merged.min()) {
      merged.locations_[0] = qhist.min();
    }
    if (qhist.max() > merged.max()) {
      merged.locations_[quantiles().size() - 1] = qhist.max();
    }
  }

  if (merged.count_ == 0) {
    return merged;
  }

  for (size_t i = 0; i < quantiles().size(); i++) {
    double weighted = 0.0;
    for (const auto& qhist : qhists) {
      weighted += qhist.locations_[i] * qhist.count();
    }
    merged.locations_[i] = weighted / merged.count();
  }

  return merged;
}

template <class Q>
QuantileHistogram<Q> QuantileHistogram<Q>::merge(
    Range<const double*> unsortedValues) const {
  QuantileHistogram<Q> merged{*this};
  for (const double val : unsortedValues) {
    merged.addValue(val);
  }
  return merged;
}

template <class Q>
void QuantileHistogram<Q>::addValue(double value) {
  if (count() == 0) {
    locations_.fill(value);
    count_ = 1;
    return;
  }

  const auto oldLocations = locations_;
  size_t bucketIndex = addValueImpl(value, oldLocations);

  // These two for loops correct for situations where one location shifts past
  // an adjacent location. This happens because of how the unitDistance is
  // calcuated.
  for (size_t i = 0; i < bucketIndex; i++) {
    if (locations_[i] > oldLocations[i + 1]) {
      locations_[i] = oldLocations[i + 1];
    }
  }

  for (size_t i = locations_.size() - 1; i > bucketIndex; i--) {
    if (locations_[i] < oldLocations[i - 1]) {
      locations_[i] = oldLocations[i - 1];
    }
  }

  dcheckSane();
}

/*
 * Estimates the value of the given quantile.
 */
template <class Q>
double QuantileHistogram<Q>::estimateQuantile(double q) const {
  if (q <= quantiles().front()) {
    return min();
  }
  if (q >= quantiles().back()) {
    return max();
  }

  size_t bucketIndex = quantiles().size() - 1;
  for (size_t i = 0; i < quantiles().size(); i++) {
    if (quantiles()[i] >= q) {
      bucketIndex = i;
      break;
    }
  }

  if (quantiles()[bucketIndex] == q) {
    return locations_[bucketIndex];
  }

  bucketIndex--;

  return locations_[bucketIndex] +
      (locations_[bucketIndex + 1] - locations_[bucketIndex]) *
      (q - quantiles()[bucketIndex]) /
      (quantiles()[bucketIndex + 1] - quantiles()[bucketIndex]);
}

template <class Q>
std::string QuantileHistogram<Q>::debugString() const {
  std::string ret = folly::to<std::string>(
      "num quantiles: ",
      quantiles().size(),
      ", count: ",
      count(),
      ", min: ",
      min(),
      ", max: ",
      max(),
      "\n");

  for (size_t i = 0; i < quantiles().size(); ++i) {
    folly::toAppend("  ", quantiles()[i], ": ", locations_[i], "\n", &ret);
  }

  return ret;
}

/*inline*/
template <class Q>
size_t QuantileHistogram<Q>::addValueImpl(
    double value, const decltype(Q::kQuantiles)& oldLocations) {
  size_t bucketIndex = 0;
  for (size_t i = 1; i < locations_.size() - 1; i++) {
    // We can approximate any quantile by starting with a quantile tracker
    // that is positioned at any arbitrary location. As we add streaming
    // data, we shift the tracker to the left if the new value is less or to
    // the right if the new value is greater.
    //
    // If the quantile tracker is tracking the `q` quantile, then if
    // we shift it to the left by C * (1 - q) or to the right by C * q, then
    // eventually we'll reach a steady state. At this point, the probability
    // that a new point will be greater is q and the probability that a new
    // point will be lesser is (1 - q), so the expected amount that we shift
    // the point to the left is C * (1 - q) * q, and the expected amount that
    // we shift the point to the right is C * q * (1 - q). In other words,
    // the expected movement is 0 once we reach that steady state.
    //
    // In order to make this estimate stabilize as more data has been
    // processed, we make the value C converge to 0. To do this, we
    // approximate the density of the histogram at the quantile tracker by
    // looking at the two adjacent quantile trackers. We use this to compute
    // the shift magnitude that would have an integral equal to 1. The value
    // `unitDistance` stands in for `C` in the above mathematical expressions.
    const double unitDistance = (oldLocations[i + 1] - oldLocations[i - 1]) /
        ((quantiles()[i + 1] - quantiles()[i - 1]) * count());
    if (oldLocations[i] < value) {
      // The std::min and std::max are used to address rounding issues.
      locations_[i] =
          std::min(value, oldLocations[i] + quantiles()[i] * unitDistance);
      if (value <= oldLocations[i + 1]) {
        bucketIndex = i;
      }
    } else {
      locations_[i] = std::max(
          value, oldLocations[i] - (1.0 - quantiles()[i]) * unitDistance);
    }
  }

  if (value < locations_.front()) {
    locations_.front() = value;
    bucketIndex = 0;
  } else if (value > locations_.back()) {
    locations_.back() = value;
    bucketIndex = locations_.size() - 1;
  }

  count_++;

  return bucketIndex;
}

template <class Q>
void QuantileHistogram<Q>::dcheckSane() const {
  DCHECK_LT(count(), 1ULL << 48) << debugString();

  for (size_t i = 0; i < quantiles().size() - 1; i++) {
    DCHECK_LT(quantiles()[i], quantiles()[i + 1]) << debugString();
  }

  for (size_t i = 0; i < locations_.size() - 1; i++) {
    DCHECK_LE(locations_[i], locations_[i + 1]) << debugString();
  }
}

template <class Q>
void CPUShardedQuantileHistogram<Q>::addValue(double value) {
  histBuilder_.append(value);
}

template <class Q>
double CPUShardedQuantileHistogram<Q>::estimateQuantile(double q) {
  std::unique_lock r{mtx_};
  flush();
  return mergedHist_.estimateQuantile(q);
}

template <class Q>
uint64_t CPUShardedQuantileHistogram<Q>::count() {
  std::unique_lock r{mtx_};
  flush();
  return mergedHist_.count();
}

template <class Q>
double CPUShardedQuantileHistogram<Q>::min() {
  std::unique_lock r{mtx_};
  flush();
  return mergedHist_.min();
}

template <class Q>
double CPUShardedQuantileHistogram<Q>::max() {
  std::unique_lock r{mtx_};
  flush();
  return mergedHist_.max();
}

template <class Q>
std::string CPUShardedQuantileHistogram<Q>::debugString() {
  std::unique_lock r{mtx_};
  flush();
  return mergedHist_.debugString();
}

template <class Q>
void CPUShardedQuantileHistogram<Q>::flush() {
  auto built = histBuilder_.build();
  mergedHist_ = mergedHist_.merge(
      std::array<QuantileHistogram<Q>, 2>{std::move(mergedHist_), built});
}

} // namespace folly
