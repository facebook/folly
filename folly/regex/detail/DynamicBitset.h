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

#include <folly/regex/detail/ChunkedBuffer.h>
#include <folly/regex/detail/FixedBitset.h>

namespace folly {
namespace regex {
namespace detail {

// Dynamically growable bitset backed by ChunkedBuffer<uint64_t>.
// Supports appending individual bit values and efficiently copying
// the result into a precisely-sized FixedBitset via linearize.
// Arbitrary indexing walks the chunk list without a second-level
// index, which is acceptable for use as a temporary builder.
// Non-copyable/non-movable (inherited from ChunkedBuffer).
struct DynamicBitset {
  ChunkedBuffer<uint64_t> storage_;
  uint64_t partial_ = 0;
  int totalBits_ = 0;

  constexpr void append(bool value) noexcept {
    if (value) {
      partial_ |= uint64_t{1} << (totalBits_ % 64);
    }
    ++totalBits_;
    if (totalBits_ % 64 == 0) {
      storage_.append(&partial_, 1);
      partial_ = 0;
    }
  }

  constexpr int size() const noexcept { return totalBits_; }

  // Ensure the bitset can hold at least `n` bits, all initialized to zero.
  // Appends full zero words in bulk, avoiding per-bit constexpr overhead.
  constexpr void reserve(int n) noexcept {
    if (n <= totalBits_) {
      return;
    }
    // Flush any partial word first.
    if (totalBits_ % 64 != 0) {
      int remaining = 64 - (totalBits_ % 64);
      int toAdd = (n - totalBits_) < remaining ? (n - totalBits_) : remaining;
      totalBits_ += toAdd;
      if (totalBits_ % 64 == 0) {
        storage_.append(&partial_, 1);
        partial_ = 0;
      }
    }
    // Append full zero words.
    while (n - totalBits_ >= 64) {
      uint64_t zero = 0;
      storage_.append(&zero, 1);
      totalBits_ += 64;
    }
    // Handle any remaining bits in a new partial word.
    if (n > totalBits_) {
      totalBits_ = n;
      // partial_ is already 0
    }
  }

  constexpr void set(int i) noexcept {
    *wordAt(i / 64) |= uint64_t{1} << (i % 64);
  }

  constexpr void clear(int i) noexcept {
    *wordAt(i / 64) &= ~(uint64_t{1} << (i % 64));
  }

  constexpr bool test(int i) const noexcept {
    return (*wordAt(i / 64) >> (i % 64)) & 1;
  }

  // Copy all bits into a FixedBitset. The caller must ensure
  // MaxBits >= size(). Excess bits in the target are zeroed.
  template <int MaxBits>
  constexpr void copyTo(FixedBitset<MaxBits>& out) const noexcept {
    out.clearAll();
    storage_.linearize(out.words);
    if (totalBits_ % 64 != 0) {
      out.words[storage_.total_count_] = partial_;
    }
  }

 private:
  constexpr uint64_t* wordAt(int wordIdx) noexcept {
    if (wordIdx < storage_.total_count_) {
      auto* block = &storage_.first_;
      int cum = 0;
      while (block) {
        if (wordIdx < cum + block->count) {
          return &block->data[wordIdx - cum];
        }
        cum += block->count;
        block = block->next;
      }
    }
    return &partial_;
  }

  constexpr const uint64_t* wordAt(int wordIdx) const noexcept {
    if (wordIdx < storage_.total_count_) {
      const auto* block = &storage_.first_;
      int cum = 0;
      while (block) {
        if (wordIdx < cum + block->count) {
          return &block->data[wordIdx - cum];
        }
        cum += block->count;
        block = block->next;
      }
    }
    return &partial_;
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
