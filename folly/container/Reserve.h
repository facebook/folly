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

#include <stdexcept>

#include <folly/Likely.h>
#include <folly/Traits.h>
#include <folly/Utility.h>

namespace folly {

namespace detail {

template <typename C>
using detect_capacity = decltype(FOLLY_DECLVAL(C).capacity());

template <typename C>
using detect_bucket_count = decltype(FOLLY_DECLVAL(C).bucket_count());

template <typename C>
using detect_max_load_factor = decltype(FOLLY_DECLVAL(C).max_load_factor());

} // namespace detail

/**
 * Avoids quadratic behavior that could arise from c.reserve(c.size() + N).
 *
 * May reserve more than N in order to effect geometric growth.  Useful when
 * c.size() is unknown, but more elements must be added by discrete operations.
 * For example: N emplace calls in a loop, or a series of range inserts, the sum
 * of their sizes being N.  Behaves like reserve() if the container is empty.
 */
struct grow_capacity_by_fn {
  template <typename C>
  void operator()(C& c, typename C::size_type const n) const {
    const size_t sz = c.size();

    if (FOLLY_UNLIKELY(c.max_size() - sz < n)) {
      throw std::length_error("max_size exceeded");
    }

    if constexpr (folly::is_detected_v<detail::detect_capacity, C&>) {
      if (sz + n <= c.capacity()) {
        return;
      }
    } else if constexpr (
        folly::is_detected_v<detail::detect_bucket_count, C&> &&
        folly::is_detected_v<detail::detect_max_load_factor, C&>) {
      if (sz + n <= c.bucket_count() * c.max_load_factor()) {
        return;
      }
    } else {
      static_assert(folly::always_false<C>, "unexpected container type");
    }

    static constexpr size_t kGrowthFactor = 2;
    auto const ra = sz * kGrowthFactor;
    auto const rb = sz + n;
    c.reserve(rb < ra ? ra : rb);
  }
};

FOLLY_INLINE_VARIABLE constexpr grow_capacity_by_fn grow_capacity_by{};

} // namespace folly
