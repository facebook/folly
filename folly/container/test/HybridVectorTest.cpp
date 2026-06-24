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

#include <folly/hybrid_vector.h>

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct Tracker {
  static inline int alive = 0;
  static inline int copies = 0;
  static inline int moves = 0;

  std::string value;

  Tracker() : value("default") { ++alive; }
  explicit Tracker(std::string v) : value(std::move(v)) { ++alive; }
  Tracker(const Tracker& other) : value(other.value) {
    ++alive;
    ++copies;
  }
  Tracker(Tracker&& other) noexcept : value(std::move(other.value)) {
    ++alive;
    ++moves;
  }

  Tracker& operator=(const Tracker& other) {
    value = other.value;
    ++copies;
    return *this;
  }

  Tracker& operator=(Tracker&& other) noexcept {
    value = std::move(other.value);
    ++moves;
    return *this;
  }

  ~Tracker() { --alive; }

  friend bool operator==(const Tracker& lhs, const Tracker& rhs) {
    return lhs.value == rhs.value;
  }
};

struct alignas(64) Wide {
  std::uint64_t value;
};

std::vector<int> valuesOf(const folly::hybrid_vector<int>& values) {
  return {values.begin(), values.end()};
}

} // namespace

TEST(HybridVector, StackBackedFixedCapacity) {
  FOLLY_HYBRID_VECTOR(int, values, 4);

  EXPECT_TRUE(values.empty());
  EXPECT_EQ(4, values.capacity());
  EXPECT_EQ(4, values.max_size());
  EXPECT_TRUE(values.is_on_stack());
  EXPECT_FALSE(values.is_on_heap());

  values.reserve(4);
  EXPECT_THROW(values.reserve(5), std::length_error);

  EXPECT_EQ(1, values.push_back(1));
  EXPECT_EQ(2, values.emplace_back(2));
  EXPECT_EQ(3, values.unchecked_push_back(3));

  EXPECT_EQ(3, values.size());
  EXPECT_EQ(1, values.front());
  EXPECT_EQ(3, values.back());
  EXPECT_EQ(2, values.at(1));
  EXPECT_THROW(values.at(3), std::out_of_range);
  EXPECT_EQ((std::array<int, 5>{{1, 2, 3, -1, -1}}), values.to_array<5>(-1));
  EXPECT_EQ((std::array<int, 3>{{1, 2, 3}}), values.to_array_exact<3>());
  EXPECT_THROW(values.to_array_exact<4>(), std::length_error);

  auto persistent = std::move(values).to_persistent();
  EXPECT_TRUE(persistent.is_on_heap());
  EXPECT_EQ((std::vector<int>{1, 2, 3}), valuesOf(persistent));
}

TEST(HybridVector, HeapBackedWhenThresholdIsExceeded) {
  FOLLY_HYBRID_VECTOR_THRESHOLD(int, values, 8, 1);

  EXPECT_TRUE(values.is_on_heap());
  EXPECT_FALSE(values.is_on_stack());
  values.assign(3, 7);

  auto moved = std::move(values);
  EXPECT_TRUE(moved.is_on_heap());
  EXPECT_EQ((std::array<int, 3>{{7, 7, 7}}), moved.to_array_exact<3>());
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(0, values.capacity());
}

TEST(HybridVector, PersistentFactoryCreatesMovableStorage) {
  auto values = folly::hybrid_vector<int>::persistent(3);
  ASSERT_TRUE(values.is_on_heap());

  values.append_range(std::array<int, 2>{{4, 5}});
  auto moved = std::move(values);

  EXPECT_TRUE(moved.is_on_heap());
  EXPECT_EQ((std::vector<int>{4, 5}), valuesOf(moved));
}

TEST(HybridVector, ModifiersAndPartialAppend) {
  FOLLY_HYBRID_VECTOR(int, values, 8);

  values.append_range(std::array<int, 3>{{1, 3, 4}});
  auto inserted = values.insert(values.begin() + 1, 2);
  EXPECT_EQ(values.begin() + 1, inserted);
  EXPECT_EQ((std::vector<int>{1, 2, 3, 4}), valuesOf(values));

  values.insert(values.begin() + 2, 2, 9);
  EXPECT_EQ((std::vector<int>{1, 2, 9, 9, 3, 4}), valuesOf(values));

  values.erase(values.begin() + 2, values.begin() + 4);
  EXPECT_EQ((std::vector<int>{1, 2, 3, 4}), valuesOf(values));

  const auto appended =
      values.try_append_range(std::array<int, 6>{{5, 6, 7, 8, 9, 10}});
  EXPECT_EQ(4, appended);
  EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}), valuesOf(values));
  EXPECT_EQ(nullptr, values.try_push_back(9));
  EXPECT_THROW(values.push_back(9), std::bad_alloc);

  EXPECT_EQ(4, folly::erase_if(values, [](int v) { return v % 2 == 0; }));
  EXPECT_EQ((std::vector<int>{1, 3, 5, 7}), valuesOf(values));
  EXPECT_EQ(1, folly::erase(values, 5));
  EXPECT_EQ((std::vector<int>{1, 3, 7}), valuesOf(values));
}

TEST(HybridVector, AssignRangeInsertRangeAndViews) {
  FOLLY_HYBRID_VECTOR(int, values, 8);
  std::vector<int> source{1, 2, 3, 4, 5};

  values.assign(source.begin() + 1, source.end() - 1);
  EXPECT_EQ((std::vector<int>{2, 3, 4}), valuesOf(values));

  values.assign_range(source | std::views::take(2));
  EXPECT_EQ((std::vector<int>{1, 2}), valuesOf(values));

  values.insert_range(values.end(), source | std::views::drop(2));
  EXPECT_EQ((std::vector<int>{1, 2, 3, 4, 5}), valuesOf(values));
}

TEST(HybridVector, NonTrivialElementsAreDestroyed) {
  Tracker::alive = 0;
  Tracker::copies = 0;
  Tracker::moves = 0;

  {
    FOLLY_HYBRID_VECTOR(Tracker, values, 3);
    values.emplace_back("one");
    values.emplace_back("two");
    values.resize(3);
    EXPECT_EQ(3, Tracker::alive);

    values.pop_back();
    EXPECT_EQ(2, Tracker::alive);

    auto persistent = std::move(values).to_persistent();
    EXPECT_TRUE(persistent.is_on_heap());
    EXPECT_EQ(2, persistent.size());
    EXPECT_EQ("one", persistent[0].value);
    EXPECT_EQ("two", persistent[1].value);
    EXPECT_EQ(2, Tracker::alive);

    auto exact = std::move(persistent).to_array_exact<2>();
    EXPECT_EQ("one", exact[0].value);
    EXPECT_EQ("two", exact[1].value);
    EXPECT_EQ(4, Tracker::alive);
  }

  EXPECT_EQ(0, Tracker::alive);
  EXPECT_GT(Tracker::moves, 0);
}

TEST(HybridVector, AlignmentAndZeroCapacity) {
  FOLLY_HYBRID_VECTOR_ALIGNED(Wide, wide, 2, 64);
  wide.emplace_back(Wide{42});
  EXPECT_TRUE(wide.is_on_stack());
  EXPECT_EQ(0, reinterpret_cast<std::uintptr_t>(wide.data()) % 64);
  EXPECT_EQ(42, wide[0].value);

  FOLLY_HYBRID_VECTOR(int, empty, 0);
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0, empty.capacity());
  EXPECT_EQ(nullptr, empty.data());
}

TEST(HybridVector, SwapComparisonAndHash) {
  FOLLY_HYBRID_VECTOR(int, lhs, 4);
  FOLLY_HYBRID_VECTOR(int, rhs, 4);
  lhs.assign({1, 2, 3});
  rhs.assign({9});

  lhs.swap(rhs);
  EXPECT_EQ((std::vector<int>{9}), valuesOf(lhs));
  EXPECT_EQ((std::vector<int>{1, 2, 3}), valuesOf(rhs));
  EXPECT_NE(lhs, rhs);
  EXPECT_EQ(std::strong_ordering::greater, lhs <=> rhs);

  FOLLY_HYBRID_VECTOR(int, copy, 4);
  copy.assign({9});
  EXPECT_EQ(lhs, copy);
  EXPECT_EQ(std::hash<folly::hybrid_vector<int>>{}(lhs),
            std::hash<folly::hybrid_vector<int>>{}(copy));
}
