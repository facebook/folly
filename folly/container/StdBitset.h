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

#include <folly/CPortability.h>
#include <folly/Portability.h>

#include <bitset>
#include <cassert>

namespace folly {

struct std_bitset_find_first_fn {
  /// Return the index of the first set bit in a bitset, or bitset.size() if
  /// none.
  template <size_t N>
  FOLLY_ALWAYS_INLINE size_t
  operator()(const std::bitset<N>& bitset) const noexcept {
    // equivalent to #if defined(__GLIBCXX__)
    if constexpr (kIsGlibcxx) {
      // GNU provides non-standard (its a hold over from the original SGI
      // implementation) _Find_first(), which efficiently returns the index of
      // the first set bit.
      return bitset._Find_first();
    }

    for (size_t i = 0; i < bitset.size(); ++i) {
      if (bitset[i]) {
        return i;
      }
    }

    return bitset.size();
  }
};

inline constexpr std_bitset_find_first_fn std_bitset_find_first{};

struct std_bitset_find_next_fn {
  /// Return the index of the first set bit in a bitset after the given index,
  /// or bitset.size() if none.
  template <size_t N>
  FOLLY_ALWAYS_INLINE size_t
  operator()(const std::bitset<N>& bitset, size_t prev) const noexcept {
    assert(prev < bitset.size());

    // equivalent to #if defined(__GLIBCXX__)
    if constexpr (kIsGlibcxx) {
      // GNU provides non-standard (its a hold over from the original SGI
      // implementation) _Find_next(), which given an index, efficiently returns
      // the index of the first set bit after the index.
      return bitset._Find_next(prev);
    }

    for (size_t i = prev + 1; i < bitset.size(); ++i) {
      if (bitset[i]) {
        return i;
      }
    }

    return bitset.size();
  }
};

inline constexpr std_bitset_find_next_fn std_bitset_find_next{};

} // namespace folly
