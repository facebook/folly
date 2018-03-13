/*
 * Copyright 2017-present Facebook, Inc.
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

#include <cstddef>
#include <vector>

#include <folly/Demangle.h>
#include <folly/container/detail/F14Policy.h>
#include <folly/container/detail/F14Table.h>

namespace folly {
namespace f14 {

struct Histo {
  std::vector<std::size_t> const& data;
};

std::ostream& operator<<(std::ostream& xo, Histo const& histo) {
  xo << "[";
  size_t sum = 0;
  for (auto v : histo.data) {
    sum += v;
  }
  size_t partial = 0;
  for (size_t i = 0; i < histo.data.size(); ++i) {
    if (i > 0) {
      xo << ", ";
    }
    partial += histo.data[i];
    if (histo.data[i] > 0) {
      xo << i << ": " << histo.data[i] << " (" << (partial * 100.0 / sum)
         << "%)";
    }
  }
  xo << "]";
  return xo;
}

void accumulate(
    std::vector<std::size_t>& a,
    std::vector<std::size_t> const& d) {
  if (a.size() < d.size()) {
    a.resize(d.size());
  }
  for (std::size_t i = 0; i < d.size(); ++i) {
    a[i] += d[i];
  }
}

double expectedProbe(std::vector<std::size_t> const& probeLengths) {
  std::size_t sum = 0;
  std::size_t count = 0;
  for (std::size_t i = 1; i < probeLengths.size(); ++i) {
    sum += i * probeLengths[i];
    count += probeLengths[i];
  }
  return static_cast<double>(sum) / count;
}

// Returns i such that probeLengths elements 0 to i (inclusive) account
// for at least 99% of the samples.
std::size_t p99Probe(std::vector<std::size_t> const& probeLengths) {
  std::size_t count = 0;
  for (std::size_t i = 1; i < probeLengths.size(); ++i) {
    count += probeLengths[i];
  }
  std::size_t rv = probeLengths.size();
  std::size_t suffix = 0;
  while ((suffix + probeLengths[rv - 1]) * 100 <= count) {
    --rv;
  }
  return rv;
}

struct MoveOnlyTestInt {
  int x;

  MoveOnlyTestInt() noexcept : x(0) {}
  /* implicit */ MoveOnlyTestInt(int x0) : x(x0) {}
  MoveOnlyTestInt(MoveOnlyTestInt&& rhs) noexcept : x(rhs.x) {}
  MoveOnlyTestInt(MoveOnlyTestInt const&) = delete;
  MoveOnlyTestInt& operator=(MoveOnlyTestInt&& rhs) noexcept {
    x = rhs.x;
    return *this;
  }
  MoveOnlyTestInt& operator=(MoveOnlyTestInt const&) = delete;

  bool operator==(MoveOnlyTestInt const& rhs) const {
    return x == rhs.x;
  }
  bool operator!=(MoveOnlyTestInt const& rhs) const {
    return !(*this == rhs);
  }
};

} // namespace f14

std::ostream& operator<<(std::ostream& xo, F14TableStats const& stats) {
  using f14::Histo;

  xo << "{ " << std::endl;
  xo << "  policy: " << folly::demangle(stats.policy) << std::endl;
  xo << "  size: " << stats.size << std::endl;
  xo << "  valueSize: " << stats.valueSize << std::endl;
  xo << "  bucketCount: " << stats.bucketCount << std::endl;
  xo << "  chunkCount: " << stats.chunkCount << std::endl;
  xo << "  chunkOccupancyHisto" << Histo{stats.chunkOccupancyHisto}
     << std::endl;
  xo << "  chunkOutboundOverflowHisto"
     << Histo{stats.chunkOutboundOverflowHisto} << std::endl;
  xo << "  chunkHostedOverflowHisto" << Histo{stats.chunkHostedOverflowHisto}
     << std::endl;
  xo << "  keyProbeLengthHisto" << Histo{stats.keyProbeLengthHisto}
     << std::endl;
  xo << "  missProbeLengthHisto" << Histo{stats.missProbeLengthHisto}
     << std::endl;
  xo << "  totalBytes: " << stats.totalBytes << std::endl;
  xo << "  valueBytes: " << (stats.size * stats.valueSize) << std::endl;
  xo << "  overheadBytes: " << stats.overheadBytes << std::endl;
  if (stats.size > 0) {
    xo << "  overheadBytesPerKey: " << (stats.overheadBytes * 1.0 / stats.size)
       << std::endl;
  }
  xo << "}";
  return xo;
}

} // namespace folly

namespace std {
template <>
struct hash<folly::f14::MoveOnlyTestInt> {
  std::size_t operator()(folly::f14::MoveOnlyTestInt const& val) const {
    return val.x;
  }
};
} // namespace std
