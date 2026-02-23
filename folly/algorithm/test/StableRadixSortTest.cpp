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

#include <folly/algorithm/StableRadixSort.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include <folly/portability/GTest.h>

namespace folly {
namespace {

// Helper struct for testing key extractors and stability
struct Item {
  double score;
  int originalIndex;

  [[maybe_unused]] friend bool operator==(const Item&, const Item&) = default;
};

// Augmented comparator that distinguishes -0.0 from +0.0 via signbit,
// matching the radix sort's natural ordering.
template <typename T>
bool augmented_less(const T& a, const T& b) {
  if constexpr (std::is_floating_point_v<T>) {
    if (a < b) {
      return true;
    }
    if (b < a) {
      return false;
    }
    // a and b are equal (e.g. -0.0 and +0.0): negative sign sorts first
    return std::signbit(a) > std::signbit(b);
  } else {
    return a < b;
  }
}

template <typename T>
bool augmented_greater(const T& a, const T& b) {
  return augmented_less(b, a);
}

// Validate that stable_radix_sort produces the same result as std::stable_sort
template <typename T>
void validate(std::vector<T> v) {
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmented_less<T>);
  stable_radix_sort(v.begin(), v.end());
  EXPECT_EQ(v, expected);
}

template <typename T, typename Proj>
void validate(std::vector<T> v, Proj proj) {
  auto expected = v;
  std::stable_sort(
      expected.begin(), expected.end(), [&](const T& a, const T& b) {
        return augmented_less(proj(a), proj(b));
      });
  stable_radix_sort(v.begin(), v.end(), proj);
  EXPECT_EQ(v, expected);
}

template <typename T>
void validateDescending(std::vector<T> v) {
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmented_greater<T>);
  stable_radix_sort_descending(v.begin(), v.end());
  EXPECT_EQ(v, expected);
}

template <typename T, typename Proj>
void validateDescending(std::vector<T> v, Proj proj) {
  auto expected = v;
  std::stable_sort(
      expected.begin(), expected.end(), [&](const T& a, const T& b) {
        return augmented_greater(proj(a), proj(b));
      });
  stable_radix_sort_descending(v.begin(), v.end(), proj);
  EXPECT_EQ(v, expected);
}

// Test fixture
class StableRadixSortTest : public ::testing::Test {
 protected:
  std::mt19937 rng{42}; // Fixed seed for reproducibility
};

// ============================================================================
// Basic functionality tests
// ============================================================================

TEST_F(StableRadixSortTest, EmptyRange) {
  validate(std::vector<uint64_t>{});
}

TEST_F(StableRadixSortTest, SingleElement) {
  validate(std::vector<uint64_t>{42});
}

TEST_F(StableRadixSortTest, TwoElements) {
  validate(std::vector<uint64_t>{5, 3});
}

TEST_F(StableRadixSortTest, AlreadySorted) {
  validate(std::vector<uint64_t>{1, 2, 3, 4, 5});
}

TEST_F(StableRadixSortTest, ReverseSorted) {
  validate(std::vector<uint64_t>{5, 4, 3, 2, 1});
}

TEST_F(StableRadixSortTest, AllEqual) {
  validate(std::vector<uint64_t>(100, 42));
}

// ============================================================================
// Unsigned integer types
// ============================================================================

TEST_F(StableRadixSortTest, Uint32Random) {
  std::vector<uint32_t> v(1000);
  std::uniform_int_distribution<uint32_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, Uint64Random) {
  std::vector<uint64_t> v(1000);
  std::uniform_int_distribution<uint64_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, Uint16Random) {
  std::vector<uint16_t> v(1000);
  std::uniform_int_distribution<uint16_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, Uint8Random) {
  std::vector<uint8_t> v(1000);
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& x : v) {
    x = static_cast<uint8_t>(dist(rng));
  }
  validate(std::move(v));
}

// ============================================================================
// Signed integer types
// ============================================================================

TEST_F(StableRadixSortTest, Int32Random) {
  std::vector<int32_t> v(1000);
  std::uniform_int_distribution<int32_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, Int64Random) {
  std::vector<int64_t> v(1000);
  std::uniform_int_distribution<int64_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, Int32NegativeValues) {
  validate(std::vector<int32_t>{-5, 3, -1, 0, -10, 7, 2, -3});
}

TEST_F(StableRadixSortTest, Int64Extremes) {
  validate(
      std::vector<int64_t>{
          std::numeric_limits<int64_t>::min(),
          std::numeric_limits<int64_t>::max(),
          0,
          -1,
          1,
          std::numeric_limits<int64_t>::min() + 1,
          std::numeric_limits<int64_t>::max() - 1,
      });
}

// ============================================================================
// Floating-point types
// ============================================================================

TEST_F(StableRadixSortTest, DoubleRandom) {
  std::vector<double> v(1000);
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, FloatRandom) {
  std::vector<float> v(1000);
  std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, DoubleSpecialValues) {
  validate(
      std::vector<double>{
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          0.0,
          -0.0,
          1.0,
          -1.0,
          std::numeric_limits<double>::max(),
          std::numeric_limits<double>::lowest(),
          std::numeric_limits<double>::denorm_min(), // smallest positive
                                                     // subnormal
      });
}

TEST_F(StableRadixSortTest, DoubleNegativeNumbers) {
  validate(std::vector<double>{-0.5, -0.1, -0.9, -0.3, -0.7});
}

TEST_F(StableRadixSortTest, NegativeZeroPositiveZeroEquality) {
  // Radix sort uses an augmented ordering where -0.0 sorts before +0.0
  // (distinguished by signbit). Stability is preserved within each group.
  std::vector<Item> items = {
      {-0.0, 0},
      {+0.0, 1},
      {-0.0, 2},
      {+0.0, 3},
  };

  stable_radix_sort(items.begin(), items.end(), [](const Item& item) {
    return item.score;
  });

  // -0.0 values come first (stable among themselves), then +0.0 values
  EXPECT_EQ(items[0].originalIndex, 0);
  EXPECT_EQ(items[1].originalIndex, 2);
  EXPECT_EQ(items[2].originalIndex, 1);
  EXPECT_EQ(items[3].originalIndex, 3);
}

TEST_F(StableRadixSortTest, NegativeZeroPositiveZeroEqualityFloat) {
  // Same test for float type
  struct FloatItem {
    float score;
    int originalIndex;
  };

  std::vector<FloatItem> items = {
      {-0.0f, 0},
      {+0.0f, 1},
      {-0.0f, 2},
      {+0.0f, 3},
  };

  stable_radix_sort(items.begin(), items.end(), [](const FloatItem& item) {
    return item.score;
  });

  // -0.0f values come first (stable among themselves), then +0.0f values
  EXPECT_EQ(items[0].originalIndex, 0);
  EXPECT_EQ(items[1].originalIndex, 2);
  EXPECT_EQ(items[2].originalIndex, 1);
  EXPECT_EQ(items[3].originalIndex, 3);
}

// ============================================================================
// Descending order tests
// ============================================================================

TEST_F(StableRadixSortTest, Uint64Descending) {
  std::vector<uint64_t> v(1000);
  std::uniform_int_distribution<uint64_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validateDescending(std::move(v));
}

TEST_F(StableRadixSortTest, Int64Descending) {
  std::vector<int64_t> v(1000);
  std::uniform_int_distribution<int64_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validateDescending(std::move(v));
}

TEST_F(StableRadixSortTest, DoubleDescending) {
  std::vector<double> v(1000);
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
  for (auto& x : v) {
    x = dist(rng);
  }
  validateDescending(std::move(v));
}

// ============================================================================
// Key extractor tests
// ============================================================================

TEST_F(StableRadixSortTest, KeyExtractorDouble) {
  std::vector<Item> items(100);
  std::uniform_real_distribution<double> dist(-100.0, 100.0);
  for (int i = 0; i < 100; ++i) {
    items[i].score = dist(rng);
    items[i].originalIndex = i;
  }

  validate(std::move(items), [](const Item& item) { return item.score; });
}

TEST_F(StableRadixSortTest, KeyExtractorDescending) {
  std::vector<Item> items(100);
  std::uniform_real_distribution<double> dist(-100.0, 100.0);
  for (int i = 0; i < 100; ++i) {
    items[i].score = dist(rng);
    items[i].originalIndex = i;
  }

  validateDescending(std::move(items), [](const Item& item) {
    return item.score;
  });
}

TEST_F(StableRadixSortTest, KeyExtractorInteger) {
  struct Record {
    uint64_t id{};
    std::string name;

    bool operator==(const Record&) const = default;
  };

  std::vector<Record> records = {
      {5, "five"},
      {2, "two"},
      {8, "eight"},
      {1, "one"},
      {3, "three"},
  };

  validate(std::move(records), [](const Record& r) { return r.id; });
}

// ============================================================================
// Stability tests
// ============================================================================

TEST_F(StableRadixSortTest, StabilityPreserved) {
  // Create items with duplicate scores
  std::vector<Item> items;
  items.reserve(100);
  for (int i = 0; i < 100; ++i) {
    items.push_back({static_cast<double>(i / 10), i}); // 10 items per score
  }

  // Shuffle to randomize order
  std::shuffle(items.begin(), items.end(), rng);

  validate(std::move(items), [](const Item& item) { return item.score; });
}

TEST_F(StableRadixSortTest, StabilityWithMultipleGroups) {
  // Create items with 3 distinct score values
  std::vector<Item> items;
  for (int i = 0; i < 30; ++i) {
    double score = static_cast<double>(i % 3); // 0.0, 1.0, or 2.0
    items.push_back({score, i});
  }

  validate(std::move(items), [](const Item& item) { return item.score; });
}

// ============================================================================
// Comparison with std::stable_sort
// ============================================================================

TEST_F(StableRadixSortTest, MatchesStableSort) {
  std::vector<double> v(500);
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, MatchesStableSortDescending) {
  std::vector<double> v(500);
  std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
  for (auto& x : v) {
    x = dist(rng);
  }
  validateDescending(std::move(v));
}

// ============================================================================
// Large input tests
// ============================================================================

TEST_F(StableRadixSortTest, LargeInput) {
  constexpr size_t kSize = 100000;
  std::vector<uint64_t> v(kSize);
  std::uniform_int_distribution<uint64_t> dist;
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

TEST_F(StableRadixSortTest, LargeInputDouble) {
  constexpr size_t kSize = 100000;
  std::vector<double> v(kSize);
  std::uniform_real_distribution<double> dist(-1e9, 1e9);
  for (auto& x : v) {
    x = dist(rng);
  }
  validate(std::move(v));
}

// ============================================================================
// Small input (fallback to std::stable_sort)
// ============================================================================

TEST_F(StableRadixSortTest, SmallInputFallback) {
  validate(std::vector<uint64_t>{9, 7, 5, 3, 1, 8, 6, 4, 2, 0});
}

// ============================================================================
// Key transformation helpers
// ============================================================================

TEST_F(StableRadixSortTest, FloatKeyCorrectness) {
  using namespace stable_radix_sort_keys;

  FloatKey<double, false> asc;
  FloatKey<double, true> desc;

  // Verify ordering is preserved
  EXPECT_LT(asc(-1.0), asc(0.0));
  EXPECT_LT(asc(0.0), asc(1.0));
  EXPECT_LT(asc(-std::numeric_limits<double>::infinity()), asc(-1e308));
  EXPECT_LT(asc(1e308), asc(std::numeric_limits<double>::infinity()));

  // Descending should invert
  EXPECT_GT(desc(-1.0), desc(0.0));
  EXPECT_GT(desc(0.0), desc(1.0));
}

TEST_F(StableRadixSortTest, IntegralKeyCorrectness) {
  using namespace stable_radix_sort_keys;

  IntegralKey<int64_t, false> asc;
  IntegralKey<int64_t, true> desc;

  // Verify ordering is preserved
  EXPECT_LT(asc(std::numeric_limits<int64_t>::min()), asc(-1));
  EXPECT_LT(asc(-1), asc(0));
  EXPECT_LT(asc(0), asc(1));
  EXPECT_LT(asc(1), asc(std::numeric_limits<int64_t>::max()));

  // Descending should invert
  EXPECT_GT(desc(-1), desc(0));
  EXPECT_GT(desc(0), desc(1));
}

} // namespace
} // namespace folly
