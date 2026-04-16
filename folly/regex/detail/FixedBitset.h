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

#include <cstdint>

#include <folly/portability/Constexpr.h>

namespace folly {
namespace regex {
namespace detail {

// Constexpr-safe fixed-size bitset using uint64_t words.
// std::bitset is not constexpr until C++23; this provides the subset
// of operations needed by the regex engine for NFA state sets, thread
// dedup, and counter tracking.
template <int MaxBits>
struct FixedBitset {
  static constexpr int kWords = (MaxBits + 63) / 64;
  uint64_t words[kWords > 0 ? kWords : 1] = {};

  constexpr void set(int i) noexcept {
    words[i / 64] |= uint64_t{1} << (i % 64);
  }

  constexpr void clear(int i) noexcept {
    words[i / 64] &= ~(uint64_t{1} << (i % 64));
  }

  constexpr bool test(int i) const noexcept {
    return (words[i / 64] >> (i % 64)) & 1;
  }

  constexpr void clearAll() noexcept {
    for (int i = 0; i < kWords; ++i) {
      words[i] = 0;
    }
  }

  // Copy bits from another FixedBitset of potentially different size.
  // Copies min(kWords, other.kWords) words, zeroing any excess.
  template <int OtherBits>
  constexpr void copyFrom(const FixedBitset<OtherBits>& o) noexcept {
    constexpr int otherWords = FixedBitset<OtherBits>::kWords;
    constexpr int n = kWords < otherWords ? kWords : otherWords;
    folly::constexpr_memcpy(words, o.words, n);
    for (int i = n; i < kWords; ++i) {
      words[i] = 0;
    }
  }

  constexpr bool empty() const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if (words[i] != 0) {
        return false;
      }
    }
    return true;
  }

  constexpr bool operator==(const FixedBitset& o) const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if (words[i] != o.words[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr void orWith(const FixedBitset& o) noexcept {
    for (int i = 0; i < kWords; ++i) {
      words[i] |= o.words[i];
    }
  }

  constexpr void andWith(const FixedBitset& o) noexcept {
    for (int i = 0; i < kWords; ++i) {
      words[i] &= o.words[i];
    }
  }

  constexpr void andNotWith(const FixedBitset& o) noexcept {
    for (int i = 0; i < kWords; ++i) {
      words[i] &= ~o.words[i];
    }
  }

  constexpr bool anyIntersection(const FixedBitset& o) const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if ((words[i] & o.words[i]) != 0) {
        return true;
      }
    }
    return false;
  }

  // Returns true if every set bit in this is also set in o.
  constexpr bool isSubsetOf(const FixedBitset& o) const noexcept {
    for (int i = 0; i < kWords; ++i) {
      if ((words[i] & ~o.words[i]) != 0) {
        return false;
      }
    }
    return true;
  }

  template <typename Fn>
  constexpr void forEachSetBit(Fn fn) const noexcept {
    for (int i = 0; i < kWords; ++i) {
      uint64_t w = words[i];
      while (w) {
        int bit = __builtin_ctzll(w);
        fn(i * 64 + bit);
        w &= w - 1;
      }
    }
  }

  constexpr uint64_t hash() const noexcept {
    uint64_t h = 0;
    for (int i = 0; i < kWords; ++i) {
      h ^= words[i] * uint64_t{0x9e3779b97f4a7c15};
      h = (h << 13) | (h >> 51);
    }
    return h;
  }

  constexpr bool operator!=(const FixedBitset& o) const noexcept {
    return !(*this == o);
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
