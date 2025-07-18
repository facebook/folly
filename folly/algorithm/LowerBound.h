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

#include <functional>
#include <iterator>

#include <folly/lang/Builtin.h>

namespace folly {

namespace detail {
template <typename Iter>
constexpr bool isRandomAccessIter() {
  // TODO (ilvokhin): write a test?
  using Tag = typename std::iterator_traits<Iter>::iterator_category;
  using RandomAccessTag = typename std::random_access_iterator_tag;
  return std::is_base_of_v<RandomAccessTag, Tag>;
}

enum class Prefetch { DISABLED = 0, ENABLED };

struct Options {
  Prefetch prefetch{Prefetch::ENABLED};
};

template <typename ForwardIter, typename T, typename Compare>
ForwardIter lowerBound(
    ForwardIter beg,
    ForwardIter end,
    const T& value,
    Compare comp,
    Options opts = {}) {
  auto len = std::distance(beg, end);
  while (len > 0) {
    auto half = len / 2;
    if constexpr (isRandomAccessIter<ForwardIter>()) {
      if (opts.prefetch == Prefetch::ENABLED) {
        auto quarter = half / 2;
        auto threeQuarters = len - half + quarter;
        // TODO (ilvokhin): use wrapper for prefetch?
        __builtin_prefetch(&*(beg + quarter));
        __builtin_prefetch(&*(beg + threeQuarters));
      }
    }
    ForwardIter mid = beg;
    std::advance(mid, half);
    if (FOLLY_BUILTIN_UNPREDICTABLE(comp(*mid, value))) {
      // This is a pessimization compared to std::lower_bound for forward
      // iterators, since this effectively means: beg = mid + len % 2 and we
      // are already advanced iterator to mid, but it slightly improves codegen
      // for random access iterators, which we care the most in practice.
      std::advance(beg, len - half);
    }
    len = half;
  }
  return beg;
}

} // namespace detail

template <typename ForwardIter, typename T, typename Compare>
ForwardIter lower_bound(
    ForwardIter beg, ForwardIter end, const T& value, Compare comp) {
  return detail::lowerBound(beg, end, value, comp);
}

template <typename ForwardIter, typename T>
ForwardIter lower_bound(ForwardIter beg, ForwardIter end, const T& value) {
  return detail::lowerBound(beg, end, value, std::less<>{});
}

} // namespace folly
