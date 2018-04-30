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

#pragma once

#include <folly/stats/detail/BufferedStat.h>

#include <folly/stats/detail/DigestBuilder-defs.h>
#include <folly/stats/detail/SlidingWindow-defs.h>

namespace folly {
namespace detail {

template <typename DigestT, typename ClockT>
BufferedStat<DigestT, ClockT>::BufferedStat(
    typename ClockT::duration bufferDuration,
    size_t bufferSize,
    size_t digestSize)
    : bufferDuration_(bufferDuration), digestBuilder_(bufferSize, digestSize) {
  expiry_.store(
      TimePointHolder(roundUp(ClockT::now())), std::memory_order_relaxed);
}

template <typename DigestT, typename ClockT>
void BufferedStat<DigestT, ClockT>::append(double value) {
  auto now = ClockT::now();
  if (UNLIKELY(now > expiry_.load(std::memory_order_acquire).tp)) {
    std::unique_lock<SharedMutex> g(mutex_, std::try_to_lock_t());
    if (g.owns_lock()) {
      doUpdate(now, g);
    }
  }
  digestBuilder_.append(value);
}

template <typename DigestT, typename ClockT>
typename ClockT::time_point BufferedStat<DigestT, ClockT>::roundUp(
    typename ClockT::time_point t) {
  auto remainder = t.time_since_epoch() % bufferDuration_;
  if (remainder.count() != 0) {
    return t + bufferDuration_ - remainder;
  }
  return t;
}

template <typename DigestT, typename ClockT>
std::unique_lock<SharedMutex> BufferedStat<DigestT, ClockT>::updateIfExpired() {
  auto now = ClockT::now();
  std::unique_lock<SharedMutex> g(mutex_);
  doUpdate(now, g);
  return g;
}

template <typename DigestT, typename ClockT>
void BufferedStat<DigestT, ClockT>::doUpdate(
    typename ClockT::time_point now,
    const std::unique_lock<SharedMutex>& g) {
  DCHECK(g.owns_lock());
  // Check that no other thread has performed the slide after the check
  auto oldExpiry = expiry_.load(std::memory_order_acquire).tp;
  if (now > oldExpiry) {
    now = roundUp(now);
    expiry_.store(TimePointHolder(now), std::memory_order_release);
    onNewDigest(digestBuilder_.build(), now, oldExpiry, g);
  }
}

template <typename DigestT, typename ClockT>
BufferedDigest<DigestT, ClockT>::BufferedDigest(
    typename ClockT::duration bufferDuration,
    size_t bufferSize,
    size_t digestSize)
    : BufferedStat<DigestT, ClockT>(bufferDuration, bufferSize, digestSize),
      digest_(digestSize) {}

template <typename DigestT, typename ClockT>
DigestT BufferedDigest<DigestT, ClockT>::get() {
  auto g = this->updateIfExpired();
  return digest_;
}

template <typename DigestT, typename ClockT>
void BufferedDigest<DigestT, ClockT>::onNewDigest(
    DigestT digest,
    typename ClockT::time_point /*newExpiry*/,
    typename ClockT::time_point /*oldExpiry*/,
    const std::unique_lock<SharedMutex>& /*g*/) {
  std::array<DigestT, 2> a{{digest_, std::move(digest)}};
  digest_ = DigestT::merge(a);
}

template <typename DigestT, typename ClockT>
BufferedSlidingWindow<DigestT, ClockT>::BufferedSlidingWindow(
    size_t nBuckets,
    typename ClockT::duration bufferDuration,
    size_t bufferSize,
    size_t digestSize)
    : BufferedStat<DigestT, ClockT>(bufferDuration, bufferSize, digestSize),
      slidingWindow_([=]() { return DigestT(digestSize); }, nBuckets) {}

template <typename DigestT, typename ClockT>
std::vector<DigestT> BufferedSlidingWindow<DigestT, ClockT>::get() {
  std::vector<DigestT> digests;
  {
    auto g = this->updateIfExpired();
    digests = slidingWindow_.get();
  }
  digests.erase(
      std::remove_if(
          digests.begin(),
          digests.end(),
          [](const DigestT& digest) { return digest.empty(); }),
      digests.end());
  return digests;
}

template <typename DigestT, typename ClockT>
void BufferedSlidingWindow<DigestT, ClockT>::onNewDigest(
    DigestT digest,
    typename ClockT::time_point newExpiry,
    typename ClockT::time_point oldExpiry,
    const std::unique_lock<SharedMutex>& /*g*/) {
  auto diff = newExpiry - oldExpiry;
  slidingWindow_.slide(diff / this->bufferDuration_);

  diff -= this->bufferDuration_;
  slidingWindow_.set(diff / this->bufferDuration_, std::move(digest));
}

} // namespace detail
} // namespace folly
