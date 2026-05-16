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

#include <folly/portability/Constexpr.h>

namespace folly {
namespace regex {
namespace detail {

// Chain of fixed-size blocks for stable-pointer storage.
// Pointers into earlier blocks remain valid when new blocks are added.
// Non-copyable/non-movable to prevent pointer invalidation.
template <typename T, int BlockSize = 64>
struct ChunkedBuffer {
  struct Block {
    T data[BlockSize] = {};
    int count = 0;
    Block* next = nullptr;
  };

  Block first_;
  Block* tail_ = &first_;
  int total_count_ = 0;

  // Append n items contiguously. Returns pointer to first appended item.
  // All n items are placed in a single block for contiguity.
  constexpr T* append(T* items, int n) noexcept {
    if (n <= 0) {
      return nullptr;
    }
    if (tail_->count + n > BlockSize) {
      auto* newBlock = new Block{};
      tail_->next = newBlock;
      tail_ = newBlock;
    }
    T* result = &tail_->data[tail_->count];
    for (int i = 0; i < n; ++i) {
      tail_->data[tail_->count++] = static_cast<T&&>(items[i]);
    }
    total_count_ += n;
    return result;
  }

  // Append a single default-constructed item. Returns its index.
  constexpr int appendOne() noexcept {
    T item{};
    append(&item, 1);
    return total_count_ - 1;
  }

  // Indexed access — walks the block chain. O(total_count_ / BlockSize).
  constexpr T& operator[](int idx) noexcept {
    Block* b = &first_;
    int cum = 0;
    while (b) {
      if (idx < cum + b->count) {
        return b->data[idx - cum];
      }
      cum += b->count;
      b = b->next;
    }
    // Should not be reached for valid indices.
    return first_.data[0];
  }

  constexpr const T& operator[](int idx) const noexcept {
    const Block* b = &first_;
    int cum = 0;
    while (b) {
      if (idx < cum + b->count) {
        return b->data[idx - cum];
      }
      cum += b->count;
      b = b->next;
    }
    return first_.data[0];
  }

  // Copy all data into a contiguous output array.
  constexpr void linearize(T* out) const noexcept {
    int offset = 0;
    const Block* b = &first_;
    while (b) {
      folly::constexpr_memcpy(out + offset, b->data, b->count);
      offset += b->count;
      b = b->next;
    }
  }

  constexpr ~ChunkedBuffer() {
    Block* b = first_.next;
    while (b) {
      Block* next = b->next;
      delete b;
      b = next;
    }
  }

  ChunkedBuffer(const ChunkedBuffer&) = delete;
  ChunkedBuffer& operator=(const ChunkedBuffer&) = delete;
  ChunkedBuffer(ChunkedBuffer&&) = delete;
  ChunkedBuffer& operator=(ChunkedBuffer&&) = delete;
  constexpr ChunkedBuffer() noexcept = default;
};

} // namespace detail
} // namespace regex
} // namespace folly
