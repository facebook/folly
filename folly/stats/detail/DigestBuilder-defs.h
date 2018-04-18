/*
 * Copyright 2018-present Facebook, Inc.
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

#include <folly/stats/detail/DigestBuilder.h>

#include <algorithm>

#include <folly/concurrency/CacheLocality.h>

namespace folly {
namespace detail {

template <typename DigestT>
DigestBuilder<DigestT>::DigestBuilder(size_t bufferSize, size_t digestSize)
    : nextPos_(0),
      digestSize_(digestSize),
      cpuLocalBuffersReady_(false),
      buffer_(bufferSize) {}

template <typename DigestT>
DigestT DigestBuilder<DigestT>::buildSyncFree() const {
  std::vector<double> values;
  std::vector<DigestT> digests;
  auto numElems =
      std::min(nextPos_.load(std::memory_order_relaxed), buffer_.size());
  values.insert(values.end(), buffer_.begin(), buffer_.begin() + numElems);

  if (cpuLocalBuffersReady_.load(std::memory_order_relaxed)) {
    for (const auto& cpuLocalBuffer : cpuLocalBuffers_) {
      if (cpuLocalBuffer.digest) {
        digests.push_back(*cpuLocalBuffer.digest);
      }
      values.insert(
          values.end(),
          cpuLocalBuffer.buffer.begin(),
          cpuLocalBuffer.buffer.end());
    }
  }
  std::sort(values.begin(), values.end());
  DigestT digest(digestSize_);
  digests.push_back(digest.merge(values));
  return DigestT::merge(digests);
}

template <typename DigestT>
void DigestBuilder<DigestT>::append(double value) {
  auto pos = nextPos_.load(std::memory_order_relaxed);
  if (pos < buffer_.size()) {
    pos = nextPos_.fetch_add(1, std::memory_order_relaxed);
    if (pos < buffer_.size()) {
      buffer_[pos] = value;
      if (pos == buffer_.size() - 1) {
        // The shared buffer is full. From here on out, appends will go to a
        // cpu local.
        auto& cl = CacheLocality::system();
        cpuLocalBuffers_.resize(cl.numCachesByLevel[0]);
        cpuLocalBuffersReady_.store(true, std::memory_order_release);
      }
      return;
    }
  }
  while (!cpuLocalBuffersReady_.load(std::memory_order_acquire)) {
  }
  auto which = AccessSpreader<>::current(cpuLocalBuffers_.size());
  auto& cpuLocalBuf = cpuLocalBuffers_[which];
  SpinLockGuard g(cpuLocalBuf.mutex);
  cpuLocalBuf.buffer.push_back(value);
  if (cpuLocalBuf.buffer.size() > buffer_.size()) {
    std::sort(cpuLocalBuf.buffer.begin(), cpuLocalBuf.buffer.end());
    if (!cpuLocalBuf.digest) {
      cpuLocalBuf.digest = std::make_unique<DigestT>(digestSize_);
    }
    *cpuLocalBuf.digest = cpuLocalBuf.digest->merge(cpuLocalBuf.buffer);
    cpuLocalBuf.buffer.clear();
  }
}

} // namespace detail
} // namespace folly
