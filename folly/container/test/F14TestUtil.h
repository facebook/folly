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

// Tracked is implicitly constructible across tags
struct Counts {
  uint64_t copyConstruct{0};
  uint64_t moveConstruct{0};
  uint64_t copyConvert{0};
  uint64_t moveConvert{0};
  uint64_t copyAssign{0};
  uint64_t moveAssign{0};
  uint64_t defaultConstruct{0};

  explicit Counts(
      uint64_t copConstr = 0,
      uint64_t movConstr = 0,
      uint64_t copConv = 0,
      uint64_t movConv = 0,
      uint64_t copAssign = 0,
      uint64_t movAssign = 0,
      uint64_t def = 0)
      : copyConstruct{copConstr},
        moveConstruct{movConstr},
        copyConvert{copConv},
        moveConvert{movConv},
        copyAssign{copAssign},
        moveAssign{movAssign},
        defaultConstruct{def} {}

  uint64_t dist(Counts const& rhs) const {
    auto d = [](uint64_t x, uint64_t y) { return (x - y) * (x - y); };
    return d(copyConstruct, rhs.copyConstruct) +
        d(moveConstruct, rhs.moveConstruct) + d(copyConvert, rhs.copyConvert) +
        d(moveConvert, rhs.moveConvert) + d(copyAssign, rhs.copyAssign) +
        d(moveAssign, rhs.moveAssign) +
        d(defaultConstruct, rhs.defaultConstruct);
  }

  bool operator==(Counts const& rhs) const {
    return copyConstruct == rhs.copyConstruct &&
        moveConstruct == rhs.moveConstruct && copyConvert == rhs.copyConvert &&
        moveConvert == rhs.moveConvert && copyAssign == rhs.copyAssign &&
        moveAssign == rhs.moveAssign &&
        defaultConstruct == rhs.defaultConstruct;
  }
  bool operator!=(Counts const& rhs) const {
    return !(*this == rhs);
  }
};

std::ostream& operator<<(std::ostream& xo, Counts const& counts) {
  xo << "[";
  std::string glue = "";
  if (counts.copyConstruct > 0) {
    xo << glue << counts.copyConstruct << " copy";
    glue = ", ";
  }
  if (counts.moveConstruct > 0) {
    xo << glue << counts.moveConstruct << " move";
    glue = ", ";
  }
  if (counts.copyConvert > 0) {
    xo << glue << counts.copyConvert << " copy convert";
    glue = ", ";
  }
  if (counts.moveConvert > 0) {
    xo << glue << counts.moveConvert << " move convert";
    glue = ", ";
  }
  if (counts.copyAssign > 0) {
    xo << glue << counts.copyAssign << " copy assign";
    glue = ", ";
  }
  if (counts.moveAssign > 0) {
    xo << glue << counts.moveAssign << " move assign";
    glue = ", ";
  }
  if (counts.defaultConstruct > 0) {
    xo << glue << counts.defaultConstruct << " default construct";
    glue = ", ";
  }
  xo << "]";
  return xo;
}

thread_local Counts sumCounts{};

template <int Tag>
struct Tracked {
  static_assert(Tag <= 5, "Need to extend Tracked<Tag> in F14TestUtil.h");

  static thread_local Counts counts;

  uint64_t val_;

  Tracked() : val_{0} {
    sumCounts.defaultConstruct++;
    counts.defaultConstruct++;
  }
  /* implicit */ Tracked(uint64_t val) : val_{val} {
    sumCounts.copyConvert++;
    counts.copyConvert++;
  }
  Tracked(Tracked const& rhs) : val_{rhs.val_} {
    sumCounts.copyConstruct++;
    counts.copyConstruct++;
  }
  Tracked(Tracked&& rhs) noexcept : val_{rhs.val_} {
    sumCounts.moveConstruct++;
    counts.moveConstruct++;
  }
  Tracked& operator=(Tracked const& rhs) {
    val_ = rhs.val_;
    sumCounts.copyAssign++;
    counts.copyAssign++;
    return *this;
  }
  Tracked& operator=(Tracked&& rhs) noexcept {
    val_ = rhs.val_;
    sumCounts.moveAssign++;
    counts.moveAssign++;
    return *this;
  }

  template <int T>
  /* implicit */ Tracked(Tracked<T> const& rhs) : val_{rhs.val_} {
    sumCounts.copyConvert++;
    counts.copyConvert++;
  }

  template <int T>
  /* implicit */ Tracked(Tracked<T>&& rhs) : val_{rhs.val_} {
    sumCounts.moveConvert++;
    counts.moveConvert++;
  }

  bool operator==(Tracked const& rhs) const {
    return val_ == rhs.val_;
  }
  bool operator!=(Tracked const& rhs) const {
    return !(*this == rhs);
  }
};

template <>
thread_local Counts Tracked<0>::counts{};
template <>
thread_local Counts Tracked<1>::counts{};
template <>
thread_local Counts Tracked<2>::counts{};
template <>
thread_local Counts Tracked<3>::counts{};
template <>
thread_local Counts Tracked<4>::counts{};
template <>
thread_local Counts Tracked<5>::counts{};

void resetTracking() {
  sumCounts = Counts{};
  Tracked<0>::counts = Counts{};
  Tracked<1>::counts = Counts{};
  Tracked<2>::counts = Counts{};
  Tracked<3>::counts = Counts{};
  Tracked<4>::counts = Counts{};
  Tracked<5>::counts = Counts{};
}

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

template <class T>
class SwapTrackingAlloc {
 public:
  using Alloc = std::allocator<T>;
  using value_type = typename Alloc::value_type;

  using pointer = typename Alloc::pointer;
  using const_pointer = typename Alloc::const_pointer;
  using reference = typename Alloc::reference;
  using const_reference = typename Alloc::const_reference;
  using size_type = typename Alloc::size_type;

  using propagate_on_container_swap = std::true_type;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;

  SwapTrackingAlloc() {}

  template <class U>
  SwapTrackingAlloc(SwapTrackingAlloc<U> const& other) noexcept
      : a_(other.a_), t_(other.t_) {}

  template <class U>
  SwapTrackingAlloc& operator=(SwapTrackingAlloc<U> const& other) noexcept {
    a_ = other.a_;
    t_ = other.t_;
    return *this;
  }

  template <class U>
  SwapTrackingAlloc(SwapTrackingAlloc<U>&& other) noexcept
      : a_(std::move(other.a_)), t_(std::move(other.t_)) {}

  template <class U>
  SwapTrackingAlloc& operator=(SwapTrackingAlloc<U>&& other) noexcept {
    a_ = std::move(other.a_);
    t_ = std::move(other.t_);
    return *this;
  }

  T* allocate(size_t n) {
    return a_.allocate(n);
  }
  void deallocate(T* p, size_t n) {
    a_.deallocate(p, n);
  }

 private:
  std::allocator<T> a_;
  folly::f14::Tracked<0> t_;

  template <class U>
  friend class SwapTrackingAlloc;
};

template <class T>
void swap(SwapTrackingAlloc<T>&, SwapTrackingAlloc<T>&) {
  // For argument dependent lookup:
  // This function will be called if the custom swap functions of F14 containers
  // are used. Otherwise, std::swap() will do 1 move construct and 2 move
  // assigns which will get tracked by t_.
}

template <class T1, class T2>
bool operator==(SwapTrackingAlloc<T1> const&, SwapTrackingAlloc<T2> const&) {
  return true;
}

template <class T1, class T2>
bool operator!=(SwapTrackingAlloc<T1> const&, SwapTrackingAlloc<T2> const&) {
  return false;
}

} // namespace f14
} // namespace folly

namespace std {
template <>
struct hash<folly::f14::MoveOnlyTestInt> {
  std::size_t operator()(folly::f14::MoveOnlyTestInt const& val) const {
    return val.x;
  }
};

template <int Tag>
struct hash<folly::f14::Tracked<Tag>> {
  size_t operator()(folly::f14::Tracked<Tag> const& tracked) const {
    return tracked.val_ ^ Tag;
  }
};

} // namespace std
