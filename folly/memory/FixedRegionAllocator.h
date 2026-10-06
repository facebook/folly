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

#include <cstddef>
#include <memory>

namespace folly {

/**
 * A thread-safe allocator over a fixed caller-owned memory region.
 *
 * The memory region must remain valid and must not be modified directly for
 * the lifetime of the allocator. Bookkeeping is stored separately, so all of
 * the supplied capacity is available for allocations, subject to alignment.
 */
class FixedRegionAllocator {
 public:
  static size_t minimumSize() noexcept;

  FixedRegionAllocator(void* data, size_t size);
  ~FixedRegionAllocator();

  FixedRegionAllocator(const FixedRegionAllocator&) = delete;
  FixedRegionAllocator& operator=(const FixedRegionAllocator&) = delete;
  FixedRegionAllocator(FixedRegionAllocator&&) = delete;
  FixedRegionAllocator& operator=(FixedRegionAllocator&&) = delete;

  void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));
  void deallocate(void* ptr);

  /**
   * Resize an allocation while preserving its contents.
   *
   * On failure, returns nullptr and leaves the original allocation intact.
   */
  void* reallocate(
      void* ptr, size_t size, size_t alignment = alignof(std::max_align_t));

  size_t allocationSize(const void* ptr) const;
  size_t freeSpace() const;
  bool allMemoryDeallocated() const;

  bool contains(const void* ptr) const noexcept;
  void* data() const noexcept;
  size_t size() const noexcept;

 private:
  class Impl;

  void* const data_;
  const size_t size_;
  std::unique_ptr<Impl> impl_;
};

} // namespace folly
