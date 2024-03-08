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

#include <folly/container/Reserve.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include <folly/container/F14Map.h>
#include <folly/portability/GTest.h>

namespace {

template <class T>
class CountingAlloc : private std::allocator<T> {
 public:
  using value_type = T;

  CountingAlloc() = default;

  template <class U>
  explicit CountingAlloc(const CountingAlloc<U>& /*unused*/) noexcept
      : CountingAlloc() {} // count each type separately

  int getCount() const { return *counter_; }

  T* allocate(size_t size) {
    (*counter_)++;
    return std::allocator<T>::allocate(size);
  }

  void deallocate(T* p, size_t size) { std::allocator<T>::deallocate(p, size); }

 private:
  std::shared_ptr<int> counter_ = std::make_shared<int>(0);
};

} // namespace

constexpr int kIters = 1000;

TEST(ReserveUtil, VectorGrowBy) {
  using Alloc = CountingAlloc<int>;
  for (const int initSize : {0, 2, 3, 5, 7, 11, 13, 17}) {
    for (const int numToAdd : {2, 3, 5, 7, 11, 13, 17}) {
      std::vector<int, Alloc> v1(initSize);
      std::vector<int, Alloc> v2(initSize);
      for (int i = 0; i < kIters; ++i) {
        v1.insert(v1.end(), numToAdd, 0); // range insert at end
        folly::grow_capacity_by(v2, numToAdd);
        for (int j = 0; j < numToAdd; ++j) {
          v2.emplace_back();
        }
      }
      const auto rangeInsertAllocs = v1.get_allocator().getCount();
      const auto growByAllocs = v2.get_allocator().getCount();
      EXPECT_EQ(rangeInsertAllocs, growByAllocs);
    }
  }
}

namespace {

auto getUniqueId(int i, int j) {
  return (i * kIters) + j;
}

template <class MapT>
void testMapGrowBy() {
  for (const int initSize : {0, 2, 3, 5, 7, 11, 13, 17}) {
    for (const int numToAdd : {2, 3, 5, 7, 11, 13, 17}) {
      MapT m1(initSize);
      MapT m2(initSize);
      MapT m3(initSize);
      for (int i = 0; i < kIters; ++i) {
        for (int j = 0; j < numToAdd; ++j) {
          const auto& [_, inserted] = m1.emplace(getUniqueId(i, j), 0);
          EXPECT_TRUE(inserted);
        }
        m2.reserve(m2.size() + numToAdd);
        for (int j = 0; j < numToAdd; ++j) {
          const auto& [_, inserted] = m2.emplace(getUniqueId(i, j), 0);
          EXPECT_TRUE(inserted);
        }
        folly::grow_capacity_by(m3, numToAdd);
        for (int j = 0; j < numToAdd; ++j) {
          const auto& [_, inserted] = m3.emplace(getUniqueId(i, j), 0);
          EXPECT_TRUE(inserted);
        }
      }
      const auto reserveAllocs = m2.get_allocator().getCount();
      const auto growByAllocs = m3.get_allocator().getCount();
      EXPECT_GE(reserveAllocs, growByAllocs);
      const auto noReserveCapacity = m1.bucket_count() * m1.max_load_factor();
      const auto growByCapacity = m3.bucket_count() * m3.max_load_factor();
      EXPECT_GE(2 * noReserveCapacity, growByCapacity);
    }
  }
}

using F14VectorMap = folly::F14VectorMap<
    int,
    int,
    std::hash<int>,
    std::equal_to<>,
    CountingAlloc<std::pair<const int, int>>>;

using F14ValueMap = folly::F14ValueMap<
    int,
    int,
    std::hash<int>,
    std::equal_to<>,
    CountingAlloc<std::pair<const int, int>>>;

using F14NodeMap = folly::F14NodeMap<
    int,
    int,
    std::hash<int>,
    std::equal_to<>,
    CountingAlloc<std::pair<const int, int>>>;

using unordered_map = std::unordered_map<
    int,
    int,
    std::hash<int>,
    std::equal_to<>,
    CountingAlloc<std::pair<const int, int>>>;

} // namespace

TEST(ReserveUtil, F14VectorMapGrowBy) {
  testMapGrowBy<F14VectorMap>();
}

TEST(ReserveUtil, F14ValueMapGrowBy) {
  testMapGrowBy<F14ValueMap>();
}

TEST(ReserveUtil, F14NodeMapGrowBy) {
  testMapGrowBy<F14NodeMap>();
}

TEST(ReserveUtil, UnorderedMapGrowBy) {
  testMapGrowBy<unordered_map>();
}
