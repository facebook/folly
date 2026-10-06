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

#include <folly/memory/FixedRegionAllocator.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>

#include <folly/Synchronized.h>
#include <folly/lang/Align.h>
#include <folly/memory/SanitizeAddress.h>

namespace folly {

class FixedRegionAllocator::Impl {
 public:
  struct State {
    std::map<std::byte*, size_t, std::less<>> freeRanges;
    std::map<const std::byte*, size_t, std::less<>> allocations;
    size_t freeSpace{0};
  };

  Impl(void* data, size_t size) {
    auto state = state_.wlock();
    state->freeRanges.emplace(static_cast<std::byte*>(data), size);
    state->freeSpace = size;
  }

  void* allocate(State& state, size_t size, size_t alignment) {
    for (auto range = state.freeRanges.begin(); range != state.freeRanges.end();
         ++range) {
      auto* const begin = range->first;
      const auto rangeSize = range->second;
      auto* const address = folly::align_ceil(begin, alignment);
      const auto padding = static_cast<size_t>(address - begin);
      if (padding > rangeSize || size > rangeSize - padding) {
        continue;
      }

      const auto suffixSize = rangeSize - padding - size;
      state.allocations.emplace(address, size);
      if (padding == 0) {
        auto node = state.freeRanges.extract(range);
        if (suffixSize != 0) {
          node.key() = address + size;
          node.mapped() = suffixSize;
          state.freeRanges.insert(std::move(node));
        }
      } else {
        range->second = padding;
        if (suffixSize != 0) {
          state.freeRanges.emplace(address + size, suffixSize);
        }
      }
      state.freeSpace -= size;
      folly::asan_unpoison_memory_region(address, size);
      return address;
    }
    return nullptr;
  }

  void releaseRange(State& state, std::byte* address, size_t size) {
    auto next = state.freeRanges.lower_bound(address);
    const auto hasPrevious = next != state.freeRanges.begin() &&
        std::prev(next)->first + std::prev(next)->second == address;
    const auto hasNext =
        next != state.freeRanges.end() && address + size == next->first;

    if (hasPrevious) {
      auto previous = std::prev(next);
      previous->second += size;
      if (hasNext) {
        previous->second += next->second;
        state.freeRanges.erase(next);
      }
    } else if (hasNext) {
      auto node = state.freeRanges.extract(next);
      node.key() = address;
      node.mapped() += size;
      state.freeRanges.insert(std::move(node));
    } else {
      state.freeRanges.emplace(address, size);
    }
    state.freeSpace += size;
    folly::asan_poison_memory_region(address, size);
  }

  void deallocate(State& state, std::byte* address) {
    const auto allocation = state.allocations.find(address);
    if (allocation == state.allocations.end()) {
      throw std::invalid_argument(
          "address was not allocated by this allocator");
    }
    releaseRange(state, address, allocation->second);
    state.allocations.erase(allocation);
  }

  folly::Synchronized<State> state_;
};

size_t FixedRegionAllocator::minimumSize() noexcept {
  return 1;
}

FixedRegionAllocator::FixedRegionAllocator(void* data, size_t size)
    : data_(data), size_(size) {
  if (data == nullptr || size < minimumSize() ||
      reinterpret_cast<uintptr_t>(data) % alignof(std::max_align_t) != 0) {
    throw std::invalid_argument("invalid fixed allocator region");
  }
  impl_ = std::make_unique<Impl>(data, size);
  folly::asan_poison_memory_region(data_, size_);
}

FixedRegionAllocator::~FixedRegionAllocator() {
  folly::asan_unpoison_memory_region(data_, size_);
}

void* FixedRegionAllocator::allocate(size_t size, size_t alignment) {
  if (!std::has_single_bit(alignment)) {
    throw std::invalid_argument("allocation alignment is not a power of two");
  }
  size = std::max(size, size_t{1});
  alignment = std::max(alignment, alignof(std::max_align_t));
  auto state = impl_->state_.wlock();
  return impl_->allocate(*state, size, alignment);
}

void FixedRegionAllocator::deallocate(void* ptr) {
  if (ptr == nullptr) {
    return;
  }
  auto state = impl_->state_.wlock();
  impl_->deallocate(*state, static_cast<std::byte*>(ptr));
}

void* FixedRegionAllocator::reallocate(
    void* ptr, size_t size, size_t alignment) {
  if (ptr == nullptr) {
    return allocate(size, alignment);
  }
  if (size == 0) {
    deallocate(ptr);
    return nullptr;
  }
  if (!std::has_single_bit(alignment)) {
    throw std::invalid_argument("allocation alignment is not a power of two");
  }
  alignment = std::max(alignment, alignof(std::max_align_t));

  auto state = impl_->state_.wlock();
  const auto allocation = state->allocations.find(static_cast<std::byte*>(ptr));
  if (allocation == state->allocations.end()) {
    throw std::invalid_argument("address was not allocated by this allocator");
  }
  const auto oldSize = allocation->second;
  if (oldSize >= size && reinterpret_cast<uintptr_t>(ptr) % alignment == 0) {
    if (oldSize > size) {
      allocation->second = size;
      impl_->releaseRange(
          *state, static_cast<std::byte*>(ptr) + size, oldSize - size);
    }
    return ptr;
  }

  auto* const oldEnd = static_cast<std::byte*>(ptr) + oldSize;
  auto following = state->freeRanges.find(oldEnd);
  const auto extra = size > oldSize ? size - oldSize : 0;
  if (reinterpret_cast<uintptr_t>(ptr) % alignment == 0 && extra != 0 &&
      following != state->freeRanges.end() && following->second >= extra) {
    auto node = state->freeRanges.extract(following);
    if (node.mapped() > extra) {
      node.key() += extra;
      node.mapped() -= extra;
      state->freeRanges.insert(std::move(node));
    }
    allocation->second = size;
    state->freeSpace -= extra;
    folly::asan_unpoison_memory_region(oldEnd, extra);
    return ptr;
  }

  void* replacement = impl_->allocate(*state, size, alignment);
  if (replacement == nullptr) {
    return nullptr;
  }
  std::memcpy(replacement, ptr, std::min(oldSize, size));
  impl_->deallocate(*state, static_cast<std::byte*>(ptr));
  return replacement;
}

size_t FixedRegionAllocator::allocationSize(const void* ptr) const {
  auto state = impl_->state_.rlock();
  const auto allocation =
      state->allocations.find(static_cast<const std::byte*>(ptr));
  if (allocation == state->allocations.end()) {
    throw std::invalid_argument("address was not allocated by this allocator");
  }
  return allocation->second;
}

size_t FixedRegionAllocator::freeSpace() const {
  return impl_->state_.rlock()->freeSpace;
}

bool FixedRegionAllocator::allMemoryDeallocated() const {
  return impl_->state_.rlock()->allocations.empty();
}

bool FixedRegionAllocator::contains(const void* ptr) const noexcept {
  const auto address = reinterpret_cast<uintptr_t>(ptr);
  const auto begin = reinterpret_cast<uintptr_t>(data_);
  return address >= begin && address - begin < size_;
}

void* FixedRegionAllocator::data() const noexcept {
  return data_;
}

size_t FixedRegionAllocator::size() const noexcept {
  return size_;
}

} // namespace folly
