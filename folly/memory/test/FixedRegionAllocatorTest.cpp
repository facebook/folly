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
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include <folly/memory/SanitizeAddress.h>
#include <folly/portability/GTest.h>

namespace {

struct TrackedAllocation {
  std::byte* ptr;
  size_t size;
  std::byte fill;
};

bool contentsMatch(const TrackedAllocation& allocation) {
  return std::all_of(
      allocation.ptr, allocation.ptr + allocation.size, [&](std::byte value) {
        return value == allocation.fill;
      });
}

void fill(TrackedAllocation& allocation, std::byte value) {
  allocation.fill = value;
  std::fill_n(allocation.ptr, allocation.size, value);
}

bool isAddressPoisoned(const void* address) {
  return folly::asan_address_is_poisoned(address) != 0;
}

class FixedRegionAllocatorTest : public testing::Test {
 protected:
  static constexpr size_t kRegionSize = 1024 * 1024;
  alignas(std::max_align_t) std::array<std::byte, kRegionSize> region_{};
  folly::FixedRegionAllocator allocator_{region_.data(), region_.size()};
};

TEST_F(FixedRegionAllocatorTest, AllocateAndDeallocate) {
  const auto initialFreeSpace = allocator_.freeSpace();
  void* ptr = allocator_.allocate(1024);

  ASSERT_NE(ptr, nullptr);
  EXPECT_TRUE(allocator_.contains(ptr));
  EXPECT_GE(allocator_.allocationSize(ptr), 1024);
  EXPECT_LT(allocator_.freeSpace(), initialFreeSpace);
  EXPECT_FALSE(allocator_.allMemoryDeallocated());

  allocator_.deallocate(ptr);
  EXPECT_EQ(allocator_.freeSpace(), initialFreeSpace);
  EXPECT_TRUE(allocator_.allMemoryDeallocated());
}

TEST_F(FixedRegionAllocatorTest, HonorsAlignment) {
  void* ptr = allocator_.allocate(1024, 256);

  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 256, 0);
  allocator_.deallocate(ptr);
}

TEST_F(FixedRegionAllocatorTest, ReturnsNullWhenExhausted) {
  void* wholeRegion = allocator_.allocate(region_.size());
  ASSERT_NE(wholeRegion, nullptr);
  EXPECT_EQ(allocator_.allocate(1), nullptr);
  allocator_.deallocate(wholeRegion);
}

TEST_F(FixedRegionAllocatorTest, CoalescesFreedRanges) {
  void* first = allocator_.allocate(1024);
  void* second = allocator_.allocate(1024);
  void* third = allocator_.allocate(1024);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(third, nullptr);

  allocator_.deallocate(second);
  allocator_.deallocate(first);
  allocator_.deallocate(third);

  EXPECT_EQ(allocator_.freeSpace(), region_.size());
  void* wholeRegion = allocator_.allocate(region_.size());
  EXPECT_EQ(wholeRegion, region_.data());
  allocator_.deallocate(wholeRegion);
}

TEST_F(FixedRegionAllocatorTest, ReallocatePreservesContents) {
  auto* ptr = static_cast<std::byte*>(allocator_.allocate(128));
  ASSERT_NE(ptr, nullptr);
  std::memset(ptr, 0x5a, 128);

  auto* grown = static_cast<std::byte*>(allocator_.reallocate(ptr, 4096));

  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(grown, ptr);
  for (size_t i = 0; i < 128; ++i) {
    EXPECT_EQ(grown[i], std::byte{0x5a});
  }
  allocator_.deallocate(grown);
}

TEST_F(FixedRegionAllocatorTest, ShrinkingReallocateReleasesTail) {
  constexpr size_t kOriginalSize = 1024;
  constexpr size_t kShrunkSize = 128;
  auto* ptr = static_cast<std::byte*>(allocator_.allocate(kOriginalSize));
  ASSERT_NE(ptr, nullptr);

  void* shrunk = allocator_.reallocate(ptr, kShrunkSize);

  EXPECT_EQ(shrunk, ptr);
  EXPECT_EQ(allocator_.allocationSize(ptr), kShrunkSize);
  EXPECT_EQ(allocator_.freeSpace(), region_.size() - kShrunkSize);
  void* tail = allocator_.allocate(kOriginalSize - kShrunkSize);
  EXPECT_EQ(tail, ptr + kShrunkSize);

  allocator_.deallocate(tail);
  allocator_.deallocate(ptr);
  EXPECT_TRUE(allocator_.allMemoryDeallocated());
}

TEST_F(FixedRegionAllocatorTest, FailedReallocatePreservesAllocation) {
  auto* ptr = static_cast<std::byte*>(allocator_.allocate(128));
  ASSERT_NE(ptr, nullptr);
  std::memset(ptr, 0x5a, 128);

  EXPECT_EQ(allocator_.reallocate(ptr, region_.size() + 1), nullptr);
  EXPECT_EQ(allocator_.allocationSize(ptr), 128);
  for (size_t i = 0; i < 128; ++i) {
    EXPECT_EQ(ptr[i], std::byte{0x5a});
  }
  allocator_.deallocate(ptr);
}

TEST_F(FixedRegionAllocatorTest, ReallocateHandlesNullAndZeroSize) {
  auto* ptr = static_cast<std::byte*>(allocator_.reallocate(nullptr, 128));
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(allocator_.allocationSize(ptr), 128);

  EXPECT_EQ(allocator_.reallocate(ptr, 0), nullptr);
  EXPECT_TRUE(allocator_.allMemoryDeallocated());

  void* zeroSized = allocator_.allocate(0);
  ASSERT_NE(zeroSized, nullptr);
  EXPECT_EQ(allocator_.allocationSize(zeroSized), 1);
  allocator_.deallocate(zeroSized);
}

TEST(FixedRegionAllocatorSanitizerTest, TracksAllocationLifetime) {
  constexpr size_t kRegionSize = 1024;
  alignas(std::max_align_t) std::array<std::byte, kRegionSize> region{};
  EXPECT_FALSE(isAddressPoisoned(region.data()));

  {
    folly::FixedRegionAllocator allocator{region.data(), region.size()};
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(region.data()));

    auto* allocation = static_cast<std::byte*>(allocator.allocate(128));
    ASSERT_NE(nullptr, allocation);
    EXPECT_FALSE(isAddressPoisoned(allocation));
    EXPECT_FALSE(isAddressPoisoned(allocation + 127));
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(allocation + 128));

    allocation = static_cast<std::byte*>(allocator.reallocate(allocation, 256));
    ASSERT_EQ(region.data(), allocation);
    EXPECT_FALSE(isAddressPoisoned(allocation + 255));
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(allocation + 256));

    allocation = static_cast<std::byte*>(allocator.reallocate(allocation, 64));
    ASSERT_EQ(region.data(), allocation);
    EXPECT_FALSE(isAddressPoisoned(allocation + 63));
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(allocation + 64));

    auto* blocker = static_cast<std::byte*>(allocator.allocate(128));
    ASSERT_NE(nullptr, blocker);
    std::fill_n(allocation, 64, std::byte{0x5a});

    auto* replacement =
        static_cast<std::byte*>(allocator.reallocate(allocation, 256));
    ASSERT_NE(nullptr, replacement);
    EXPECT_NE(allocation, replacement);
    EXPECT_EQ(std::byte{0x5a}, replacement[0]);
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(allocation));
    EXPECT_FALSE(isAddressPoisoned(replacement + 255));

    allocator.deallocate(blocker);
    allocator.deallocate(replacement);
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(blocker));
    EXPECT_EQ(folly::kIsSanitizeAddress, isAddressPoisoned(replacement));
  }

  EXPECT_FALSE(isAddressPoisoned(region.data()));
}

TEST_F(FixedRegionAllocatorTest, RejectsInvalidOperations) {
  EXPECT_THROW(allocator_.allocate(1, 0), std::invalid_argument);
  EXPECT_THROW(allocator_.allocate(1, 3), std::invalid_argument);

  auto* ptr = static_cast<std::byte*>(allocator_.allocate(128));
  ASSERT_NE(ptr, nullptr);
  EXPECT_THROW(allocator_.deallocate(ptr + 1), std::invalid_argument);
  EXPECT_THROW(allocator_.reallocate(ptr + 1, 256), std::invalid_argument);
  EXPECT_THROW(allocator_.reallocate(ptr, 256, 3), std::invalid_argument);
  EXPECT_EQ(allocator_.allocationSize(ptr), 128);
  allocator_.deallocate(ptr);
}

TEST_F(FixedRegionAllocatorTest, RandomizedOperationsPreserveInvariants) {
  constexpr size_t kIterations = 20000;
  std::mt19937 random{0x5eed};
  std::vector<TrackedAllocation> allocations;
  size_t allocatedSpace = 0;

  for (size_t iteration = 0; iteration < kIterations; ++iteration) {
    const auto action = random() % 100;
    if (allocations.empty() || action < 40) {
      const auto size = size_t{1} + random() % 8192;
      const auto alignment = size_t{1} << (random() % 13);
      auto* ptr = static_cast<std::byte*>(allocator_.allocate(size, alignment));
      if (ptr != nullptr) {
        EXPECT_EQ(
            reinterpret_cast<uintptr_t>(ptr) %
                std::max(alignment, alignof(std::max_align_t)),
            0);
        for (const auto& allocation : allocations) {
          EXPECT_TRUE(
              ptr + size <= allocation.ptr ||
              allocation.ptr + allocation.size <= ptr);
        }
        allocations.push_back({ptr, size, {}});
        fill(
            allocations.back(),
            std::byte{static_cast<unsigned char>(iteration % 251 + 1)});
        allocatedSpace += size;
      }
    } else if (action < 75) {
      const auto index = random() % allocations.size();
      auto& allocation = allocations[index];
      ASSERT_TRUE(contentsMatch(allocation));
      const auto oldAllocation = allocation;
      const auto oldFreeSpace = allocator_.freeSpace();
      const auto newSize = size_t{1} + random() % 8192;
      const auto alignment = size_t{1} << (random() % 13);
      auto* ptr = static_cast<std::byte*>(
          allocator_.reallocate(allocation.ptr, newSize, alignment));
      if (ptr == nullptr) {
        EXPECT_EQ(
            allocator_.allocationSize(oldAllocation.ptr), oldAllocation.size);
        EXPECT_EQ(allocator_.freeSpace(), oldFreeSpace);
        EXPECT_TRUE(contentsMatch(oldAllocation));
      } else {
        EXPECT_EQ(
            reinterpret_cast<uintptr_t>(ptr) %
                std::max(alignment, alignof(std::max_align_t)),
            0);
        EXPECT_TRUE(
            std::all_of(
                ptr,
                ptr + std::min(oldAllocation.size, newSize),
                [&](std::byte value) { return value == oldAllocation.fill; }));
        EXPECT_EQ(allocator_.allocationSize(ptr), newSize);
        for (size_t other = 0; other < allocations.size(); ++other) {
          if (other == index) {
            continue;
          }
          EXPECT_TRUE(
              ptr + newSize <= allocations[other].ptr ||
              allocations[other].ptr + allocations[other].size <= ptr);
        }
        allocation = {ptr, newSize, {}};
        fill(
            allocation,
            std::byte{static_cast<unsigned char>(iteration % 251 + 1)});
        allocatedSpace = allocatedSpace - oldAllocation.size + newSize;
      }
    } else {
      const auto index = random() % allocations.size();
      ASSERT_TRUE(contentsMatch(allocations[index]));
      allocatedSpace -= allocations[index].size;
      allocator_.deallocate(allocations[index].ptr);
      allocations[index] = allocations.back();
      allocations.pop_back();
    }

    if (iteration % 257 == 0) {
      for (const auto& allocation : allocations) {
        EXPECT_TRUE(allocator_.contains(allocation.ptr));
        EXPECT_EQ(allocator_.allocationSize(allocation.ptr), allocation.size);
        EXPECT_TRUE(contentsMatch(allocation));
      }
    }
    EXPECT_EQ(allocator_.freeSpace(), region_.size() - allocatedSpace);
  }

  for (const auto& allocation : allocations) {
    ASSERT_TRUE(contentsMatch(allocation));
    allocator_.deallocate(allocation.ptr);
  }
  EXPECT_TRUE(allocator_.allMemoryDeallocated());
  EXPECT_EQ(allocator_.freeSpace(), region_.size());
  void* wholeRegion = allocator_.allocate(region_.size());
  EXPECT_EQ(wholeRegion, region_.data());
  allocator_.deallocate(wholeRegion);
}

TEST_F(FixedRegionAllocatorTest, SupportsConcurrentRandomizedReuse) {
  constexpr size_t kThreads = 8;
  constexpr size_t kIterations = 2500;
  constexpr size_t kMaxLiveAllocations = 32;
  std::atomic<bool> contentsValid{true};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (size_t thread = 0; thread < kThreads; ++thread) {
    threads.emplace_back([&, thread] {
      std::mt19937 random{static_cast<uint32_t>(0xc0ffee + thread)};
      std::vector<TrackedAllocation> allocations;
      for (size_t i = 0; i < kIterations; ++i) {
        const auto action = random() % 100;
        if (allocations.empty() ||
            (action < 45 && allocations.size() < kMaxLiveAllocations)) {
          const auto size = size_t{1} + random() % 2048;
          const auto alignment = size_t{1} << (random() % 9);
          auto* ptr =
              static_cast<std::byte*>(allocator_.allocate(size, alignment));
          if (ptr != nullptr) {
            allocations.push_back({ptr, size, {}});
            fill(
                allocations.back(),
                std::byte{static_cast<unsigned char>(
                    (thread * kIterations + i) % 251 + 1)});
          }
        } else {
          const auto index = random() % allocations.size();
          auto& allocation = allocations[index];
          if (!contentsMatch(allocation)) {
            contentsValid = false;
          }
          if (action < 75) {
            const auto newSize = size_t{1} + random() % 4096;
            const auto alignment = size_t{1} << (random() % 9);
            const auto oldAllocation = allocation;
            auto* ptr = static_cast<std::byte*>(
                allocator_.reallocate(allocation.ptr, newSize, alignment));
            if (ptr != nullptr) {
              if (!std::all_of(
                      ptr,
                      ptr + std::min(oldAllocation.size, newSize),
                      [&](std::byte value) {
                        return value == oldAllocation.fill;
                      })) {
                contentsValid = false;
              }
              allocation = {ptr, newSize, {}};
              fill(
                  allocation,
                  std::byte{static_cast<unsigned char>(
                      (thread * kIterations + i) % 251 + 1)});
            }
          } else {
            allocator_.deallocate(allocation.ptr);
            allocation = allocations.back();
            allocations.pop_back();
          }
        }
      }
      for (const auto& allocation : allocations) {
        if (!contentsMatch(allocation)) {
          contentsValid = false;
        }
        allocator_.deallocate(allocation.ptr);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_TRUE(contentsValid);
  EXPECT_TRUE(allocator_.allMemoryDeallocated());
  EXPECT_EQ(allocator_.freeSpace(), region_.size());
}

TEST(FixedRegionAllocatorConstructionTest, RejectsInvalidRegions) {
  alignas(std::max_align_t) std::array<std::byte, 1024> region{};
  EXPECT_THROW(
      folly::FixedRegionAllocator(nullptr, region.size()),
      std::invalid_argument);
  EXPECT_THROW(
      folly::FixedRegionAllocator(
          region.data(), folly::FixedRegionAllocator::minimumSize() - 1),
      std::invalid_argument);
}

} // namespace
