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

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include <folly/Memory.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/detail/F14IntrinsicsAvailability.h>
#include <folly/lang/Align.h>
#include <folly/portability/GTest.h>

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE

namespace folly {

namespace {

// Distinct from std::allocator to exercise the custom-allocator path, which
// must retain its unpadded allocation size even under jemalloc.
template <typename T>
struct TestAllocator {
  using value_type = T;

  TestAllocator() = default;

  template <typename U>
  /* implicit */ TestAllocator(TestAllocator<U> const&) noexcept {}

  T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }

  void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }

  friend bool operator==(TestAllocator, TestAllocator) { return true; }
  friend bool operator!=(TestAllocator, TestAllocator) { return false; }
};

using TestValue = f14::detail::MapValueType<uint64_t, uint64_t>;
using VectorMap = F14VectorMap<uint64_t, uint64_t>;
using VectorByteAlloc =
    std::allocator_traits<VectorMap::allocator_type>::rebind_alloc<uint8_t>;
using CustomVectorMap = F14VectorMap<
    uint64_t,
    uint64_t,
    f14::DefaultHasher<uint64_t>,
    f14::DefaultKeyEqual<uint64_t>,
    TestAllocator<TestValue>>;

// Derive storage from the tag/index/value layout, not F14's sizing helpers.
constexpr std::size_t kTagAlignment =
    std::max(std::size_t{16}, alignof(folly::max_align_t));
template <typename Value>
constexpr std::size_t kAllocationAlignment =
    std::max(kTagAlignment, alignof(Value));
constexpr bool kCanPadVectorMap = sizeof(TestValue) >= 16 &&
    kAllocationAlignment<TestValue> <= alignof(folly::max_align_t) &&
    std::is_same_v<VectorByteAlloc, std::allocator<uint8_t>>;

// Account for allocator alignment overhead separately from F14's padding.
template <typename Value, typename ByteAlloc = std::allocator<uint8_t>>
std::size_t expectedAllocationBytes(std::size_t request) {
  return allocationBytesForOverAligned<ByteAlloc, kAllocationAlignment<Value>>(
      request);
}

struct alignas(2 * kTagAlignment) OverAlignedValue {
  uint64_t value;

  bool operator==(OverAlignedValue const& rhs) const {
    return value == rhs.value;
  }
};

struct OverAlignedHash {
  std::size_t operator()(OverAlignedValue const& value) const {
    return std::hash<uint64_t>{}(value.value);
  }
};

} // namespace

struct NormalHash {
  std::size_t operator()(int i) const { return std::hash<int>()(i); }
};

struct Force32BitHash {
  using folly_assume_32bit_hash = std::true_type;

  std::size_t operator()(int i) const { return std::hash<int>()(i); }
};

struct F14ValueSetTester {
  static void test() {
    static_assert(
        !F14FastSet<int, NormalHash>::Policy::shouldAssume32BitHash());
    static_assert(
        F14FastSet<int, Force32BitHash>::Policy::shouldAssume32BitHash());
  }
};

TEST(F14Policy, assume32BitTag) {
  F14ValueSetTester::test();
}

TEST(F14Policy, vectorAllocationSizeClassPadding) {
  constexpr std::size_t capacity = 6;
  constexpr auto prefixBytes = kTagAlignment + capacity * sizeof(uint32_t);
  const auto rawVectorBytes = align_ceil(prefixBytes, alignof(TestValue)) +
      capacity * sizeof(TestValue);
  const auto vectorRequest = kCanPadVectorMap && usingJEMalloc()
      ? align_ceil(rawVectorBytes, std::size_t{64})
      : rawVectorBytes;

  VectorMap vectorMap;
  vectorMap.reserve(capacity);
  EXPECT_EQ(
      expectedAllocationBytes<TestValue>(vectorRequest),
      vectorMap.getAllocatedMemorySize());

  CustomVectorMap customVectorMap;
  customVectorMap.reserve(capacity);
  EXPECT_EQ(
      (expectedAllocationBytes<TestValue, TestAllocator<uint8_t>>(
          rawVectorBytes)),
      customVectorMap.getAllocatedMemorySize());

  F14ValueMap<uint64_t, uint64_t> valueMap;
  valueMap.reserve(capacity);
  const auto valueRequest =
      kAllocationAlignment<TestValue> + capacity * sizeof(TestValue);
  EXPECT_EQ(
      expectedAllocationBytes<TestValue>(valueRequest),
      valueMap.getAllocatedMemorySize());

  F14VectorSet<uint64_t> compactVectorSet;
  compactVectorSet.reserve(capacity);
  const auto compactRequest =
      align_ceil(prefixBytes, alignof(uint64_t)) + capacity * sizeof(uint64_t);
  EXPECT_EQ(
      expectedAllocationBytes<uint64_t>(compactRequest),
      compactVectorSet.getAllocatedMemorySize());
}

TEST(F14Policy, vectorChunkCacheLinePlacement) {
  struct Case {
    std::size_t capacity;
    std::size_t chunkCount;
  };
  constexpr std::size_t cacheLineSize = 64;
  constexpr auto chunkStride =
      align_ceil(kTagAlignment + 12 * sizeof(uint32_t), kTagAlignment);
  const bool padded = kCanPadVectorMap && usingJEMalloc();

  for (auto const& [capacity, chunkCount] :
       {Case{3, 1}, Case{6, 1}, Case{12, 1}, Case{20, 2}}) {
    SCOPED_TRACE(capacity);
    const auto chunkBytes = chunkCount == 1
        ? kTagAlignment + capacity * sizeof(uint32_t)
        : chunkCount * chunkStride;
    const auto valuesOffset = align_ceil(chunkBytes, alignof(TestValue));
    const auto rawBytes = valuesOffset + capacity * sizeof(TestValue);
    const auto chunkAlignment = chunkBytes <= 32 ? 32 : cacheLineSize;
    const auto request =
        padded ? align_ceil(rawBytes, chunkAlignment) : rawBytes;

    // Keep allocations live together so tcache reuse cannot mask placement.
    std::array<VectorMap, 32> maps;
    for (auto& map : maps) {
      map.reserve(capacity);
      ASSERT_TRUE(map.emplace(1, 2).second);
    }
    for (auto const& map : maps) {
      ASSERT_EQ(capacity, map.bucket_count());
      ASSERT_EQ(1, map.size());
      EXPECT_EQ(
          expectedAllocationBytes<TestValue>(request),
          map.getAllocatedMemorySize());
      auto const& value = *map.begin();
      EXPECT_EQ((TestValue{1, 2}), value);
      const auto address = reinterpret_cast<uintptr_t>(std::addressof(value));
      ASSERT_GE(address, valuesOffset);
      const auto allocation = address - valuesOffset;
      EXPECT_EQ(0, allocation % kAllocationAlignment<TestValue>);
      if (padded) {
        EXPECT_EQ(0, allocation % chunkAlignment);
        for (std::size_t i = 0; i < chunkCount; ++i) {
          const auto start = allocation + i * chunkStride;
          const auto bytes = chunkCount == 1 ? chunkBytes : chunkStride;
          const auto lines =
              (start + bytes - 1) / cacheLineSize - start / cacheLineSize + 1;
          EXPECT_EQ((bytes + cacheLineSize - 1) / cacheLineSize, lines);
        }
      }
    }
  }
}

TEST(F14Policy, overAlignedVectorAllocationUnchanged) {
  static_assert(alignof(OverAlignedValue) > kTagAlignment);
  constexpr std::size_t capacity = 5;
  constexpr auto prefixBytes = kTagAlignment + capacity * sizeof(uint32_t);
  const auto valuesOffset = align_ceil(prefixBytes, alignof(OverAlignedValue));
  const auto request = valuesOffset + capacity * sizeof(OverAlignedValue);

  F14VectorSet<OverAlignedValue, OverAlignedHash> set;
  set.reserve(capacity);
  EXPECT_EQ(
      expectedAllocationBytes<OverAlignedValue>(request),
      set.getAllocatedMemorySize());
  ASSERT_TRUE(set.emplace(OverAlignedValue{1}).second);
  ASSERT_EQ(capacity, set.bucket_count());
  ASSERT_EQ(1, set.size());
  EXPECT_EQ(1, set.begin()->value);
  EXPECT_EQ(
      0,
      reinterpret_cast<uintptr_t>(std::addressof(*set.begin())) %
          alignof(OverAlignedValue));
  EXPECT_EQ(
      expectedAllocationBytes<OverAlignedValue>(request),
      set.getAllocatedMemorySize());
}

} // namespace folly

#endif // FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
