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
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly;

namespace {

thread_local std::mt19937_64 rng{42};

// ---------------------------------------------------------------------------
// Augmented comparator that matches radix sort's IEEE 754 total ordering
// ---------------------------------------------------------------------------

template <typename T>
bool augmentedLess(const T& a, const T& b) {
  if constexpr (std::is_floating_point_v<T>) {
    if (a < b) {
      return true;
    }
    if (b < a) {
      return false;
    }
    return std::signbit(a) > std::signbit(b);
  } else {
    return a < b;
  }
}

template <typename T>
bool augmentedGreater(const T& a, const T& b) {
  return augmentedLess(b, a);
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

template <typename T>
void validate(std::vector<T> v) {
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmentedLess<T>);
  radixSort(v.begin(), v.end());
  ASSERT_EQ(v.size(), expected.size());
  for (size_t i = 0; i < v.size(); ++i) {
    SCOPED_TRACE(std::format("index={}", i));
    EXPECT_EQ(v[i], expected[i]);
  }
}

template <typename T, typename Proj>
void validate(std::vector<T> v, Proj proj) {
  auto expected = v;
  std::stable_sort(
      expected.begin(), expected.end(), [&](const T& a, const T& b) {
        return augmentedLess(proj(a), proj(b));
      });
  radixSort(v.begin(), v.end(), proj);
  ASSERT_EQ(v.size(), expected.size());
  for (size_t i = 0; i < v.size(); ++i) {
    SCOPED_TRACE(std::format("index={}", i));
    EXPECT_EQ(v[i], expected[i]);
  }
}

constexpr auto kDescOpts = [] {
  auto opts = RadixSortOptions{};
  opts.SortOrder = RadixSortOrder::Descending;
  return opts;
}();

template <typename T>
void validateDescending(std::vector<T> v) {
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), augmentedGreater<T>);
  radixSort<kDescOpts>(v.begin(), v.end());
  ASSERT_EQ(v.size(), expected.size());
  for (size_t i = 0; i < v.size(); ++i) {
    SCOPED_TRACE(std::format("index={}", i));
    EXPECT_EQ(v[i], expected[i]);
  }
}

// ---------------------------------------------------------------------------
// Random data generators
// ---------------------------------------------------------------------------

template <typename T>
std::vector<T> generateRandom(size_t n) {
  std::vector<T> v(n);
  if constexpr (std::is_floating_point_v<T>) {
    std::uniform_real_distribution<T> dist(-1000, 1000);
    for (auto& x : v) {
      x = dist(rng);
    }
  } else if constexpr (sizeof(T) == 1) {
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& x : v) {
      x = static_cast<T>(dist(rng));
    }
  } else if constexpr (std::is_signed_v<T>) {
    std::uniform_int_distribution<int64_t> dist(
        std::numeric_limits<T>::min() / 2, std::numeric_limits<T>::max() / 2);
    for (auto& x : v) {
      x = static_cast<T>(dist(rng));
    }
  } else {
    std::uniform_int_distribution<uint64_t> dist;
    for (auto& x : v) {
      x = static_cast<T>(dist(rng));
    }
  }
  return v;
}

// ---------------------------------------------------------------------------
// Item struct for stability and key-extractor tests
// ---------------------------------------------------------------------------

struct Item {
  double score;
  int originalIndex;

  bool operator==(const Item&) const = default;
  bool operator<(const Item& o) const { return augmentedLess(score, o.score); }
  bool operator>(const Item& o) const { return o < *this; }

  friend std::ostream& operator<<(std::ostream& os, const Item& item) {
    return os << std::format(
               "Item{{score={}, index={}}}", item.score, item.originalIndex);
  }
};

} // namespace

// ============================================================================
// Basic tests
// ============================================================================

TEST(RadixSortTest, Empty) {
  validate(std::vector<uint32_t>{});
}

TEST(RadixSortTest, SingleElement) {
  validate(std::vector<uint32_t>{42});
}

TEST(RadixSortTest, AlreadySorted) {
  validate(std::vector<uint32_t>{1, 2, 3, 4, 5});
}

TEST(RadixSortTest, ReverseSorted) {
  validate(std::vector<uint32_t>{5, 4, 3, 2, 1});
}

TEST(RadixSortTest, AllEqual) {
  validate(std::vector<uint32_t>(100, 42));
}

TEST(RadixSortTest, SmallArrayBelowThreshold) {
  std::vector<uint32_t> v(50);
  std::iota(v.begin(), v.end(), uint32_t(0));
  std::reverse(v.begin(), v.end());
  validate(v);
}

// ============================================================================
// Unsigned integer types
// ============================================================================

TEST(RadixSortTest, UnsignedIntegers) {
  // Note: uint8_t is excluded because kNumPasses==1 for 1-byte types
  // See SingleByte test for 8-bit coverage
  validate(generateRandom<uint16_t>(1000));
  validate(generateRandom<uint32_t>(1000));
  validate(generateRandom<uint64_t>(1000));
}

// ============================================================================
// Signed integer types
// ============================================================================

TEST(RadixSortTest, SignedIntegers) {
  // Note: int8_t is excluded because kNumPasses==1 for 1-byte types
  // See SingleByte test for 8-bit coverage
  validate(generateRandom<int16_t>(1000));
  validate(generateRandom<int32_t>(1000));
  validate(generateRandom<int64_t>(1000));
}

TEST(RadixSortTest, SignedExtremes) {
  validate(
      std::vector<int32_t>{
          std::numeric_limits<int32_t>::min(),
          std::numeric_limits<int32_t>::max(),
          0,
          -1,
          1,
          std::numeric_limits<int32_t>::min() + 1,
          std::numeric_limits<int32_t>::max() - 1,
      });
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
  validate(std::vector<int32_t>{-5, 3, -1, 0, -10, 7, 2, -3});
}

// ============================================================================
// Floating point types
// ============================================================================

TEST(RadixSortTest, FloatRandom) {
  validate(generateRandom<float>(1000));
  validate(generateRandom<double>(1000));
}

TEST(RadixSortTest, FloatSpecialValues) {
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
          std::numeric_limits<double>::denorm_min(),
      });
  validate(std::vector<double>{-0.5, -0.1, -0.9, -0.3, -0.7});
}

// ============================================================================
// Descending order
// ============================================================================

TEST(RadixSortTest, Descending) {
  {
    auto v = generateRandom<uint64_t>(1000);
    validateDescending(v);
  }
  {
    auto v = generateRandom<int64_t>(1000);
    validateDescending(v);
  }
  {
    auto v = generateRandom<double>(1000);
    validateDescending(v);
  }
}

// ============================================================================
// Key extractor
// ============================================================================

TEST(RadixSortTest, KeyExtractor) {
  // Integer key
  struct Record {
    uint64_t id{};
    std::string name;
    bool operator==(const Record&) const = default;
  };
  {
    std::vector<Record> records = {
        {5, "five"},
        {2, "two"},
        {8, "eight"},
        {1, "one"},
        {3, "three"},
    };
    auto expected = records;
    std::stable_sort(
        expected.begin(), expected.end(), [](const Record& a, const Record& b) {
          return a.id < b.id;
        });
    radixSort(records.begin(), records.end(), [](const Record& r) {
      return r.id;
    });
    ASSERT_EQ(records.size(), expected.size());
    for (size_t i = 0; i < records.size(); ++i) {
      SCOPED_TRACE(std::format("index={}", i));
      EXPECT_EQ(records[i], expected[i]);
    }
  }

  // Floating-point key
  {
    std::vector<Item> items(100);
    std::uniform_real_distribution<double> dist(-100.0, 100.0);
    for (int i = 0; i < 100; ++i) {
      items[i].score = dist(rng);
      items[i].originalIndex = i;
    }
    validate(items, [](const Item& item) { return item.score; });
  }
}

// ============================================================================
// Stability
// ============================================================================

TEST(RadixSortTest, Stability) {
  {
    std::vector<Item> items;
    for (int i = 0; i < 100; ++i) {
      items.push_back({static_cast<double>(i / 10), i});
    }
    std::shuffle(items.begin(), items.end(), rng);
    validate(items, [](const Item& item) { return item.score; });
  }
  {
    std::vector<Item> items;
    for (int i = 0; i < 30; ++i) {
      items.push_back({static_cast<double>(i % 3), i});
    }
    validate(items, [](const Item& item) { return item.score; });
  }
}

// ============================================================================
// Large input
// ============================================================================

TEST(RadixSortTest, LargeInput) {
  {
    auto v = generateRandom<uint64_t>(100000);
    validate(v);
  }
  {
    auto v = generateRandom<double>(100000);
    validate(v);
  }
}

// ============================================================================
// Small input (insertion sort fallback)
// ============================================================================

TEST(RadixSortTest, SmallInput) {
  validate(std::vector<uint64_t>{9, 7, 5, 3, 1, 8, 6, 4, 2, 0});
  validate(std::vector<int64_t>{-5, -3, -1, 0, 2, 4, -4, -2, 1, 3});
  validate(std::vector<double>{-0.5, 0.3, -0.1, 0.9, -0.7});
}

// ============================================================================
// 16-bit chunks (RadixBitsPerPass::Bits16)
// ============================================================================

TEST(RadixSortTest, ChunkBits16) {
  constexpr auto kChunk16Opts = [] {
    return RadixSortOptions{
        .SortStrategy = RadixSortStrategy::Lsd,
        .ExecutionPolicy = RadixExecutionPolicy::Seq,
        .SortOrder = RadixSortOrder::Ascending,
        .BitsPerPass = RadixBitsPerPass::Bits16};
  }();

  // uint32 with 16-bit chunks => 2 passes
  {
    auto v = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kChunk16Opts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // int32 with 16-bit chunks
  {
    auto v = generateRandom<int32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kChunk16Opts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// 64-bit bucket counters (BucketCounterType::UInt64)
// ============================================================================

TEST(RadixSortTest, CounterType64) {
  constexpr auto kCounter64Opts = [] {
    return RadixSortOptions{
        .SortStrategy = RadixSortStrategy::Lsd,
        .ExecutionPolicy = RadixExecutionPolicy::Seq,
        .SortOrder = RadixSortOrder::Ascending,
        .BitsPerPass = RadixBitsPerPass::Bits8,
        .HistogramStorageType = RadixHistogramStorageType::UInt64};
  }();

  auto v = generateRandom<uint64_t>(10000);
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end());
  radixSort<kCounter64Opts>(v.begin(), v.end());
  EXPECT_EQ(v, expected);
}

// ============================================================================
// Negative-zero / Positive-zero ordering
// ============================================================================

TEST(RadixSortTest, NegativeZero) {
  std::vector<Item> items = {
      {-0.0, 0},
      {+0.0, 1},
      {-0.0, 2},
      {+0.0, 3},
  };
  radixSort(items.begin(), items.end(), [](const Item& item) {
    return item.score;
  });
  EXPECT_EQ(items[0].originalIndex, 0);
  EXPECT_EQ(items[1].originalIndex, 2);
  EXPECT_EQ(items[2].originalIndex, 1);
  EXPECT_EQ(items[3].originalIndex, 3);
}

// ============================================================================
// Bool type
// ============================================================================

TEST(RadixSortTest, BoolType) {
  {
    std::vector<uint8_t> v = {1, 0, 1, 0, 1, 0, 0, 1};
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
  {
    std::vector<uint8_t> v(60);
    for (auto& x : v) {
      x = static_cast<uint8_t>(rng() % 2);
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// NaN handling
// ============================================================================

TEST(RadixSortTest, NaNsFirst) {
  constexpr auto kNaNsFirst = [] {
    auto opts = RadixSortOptions{};
    opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
    opts.NaNsAssumption = RadixNaNsAssumption::Existence;
    return opts;
  }();

  {
    std::vector<double> v = {
        NAN,
        3.0,
        NAN,
        1.0,
        NAN,
        2.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    radixSort<kNaNsFirst>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
    EXPECT_TRUE(std::isnan(v[2]));
    for (size_t i = 3; i < v.size(); ++i) {
      EXPECT_TRUE(!std::isnan(v[i]));
      if (i > 3) {
        EXPECT_LE(v[i - 1], v[i]);
      }
    }
  }
  // float with NaNsFirst
  {
    std::vector<float> v = {NAN, 1.0f, NAN, 2.0f, -1.0f};
    radixSort<kNaNsFirst>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
  }
}

TEST(RadixSortTest, NaNsLast) {
  constexpr auto kNaNsLast = [] {
    auto opts = RadixSortOptions{};
    opts.NaNsPosHandling = RadixNaNsPosHandling::AtLast;
    opts.NaNsAssumption = RadixNaNsAssumption::Existence;
    return opts;
  }();

  {
    std::vector<double> v = {
        NAN,
        3.0,
        NAN,
        1.0,
        2.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    radixSort<kNaNsLast>(v.begin(), v.end());
    size_t j = 0;
    for (; j < v.size(); ++j) {
      if (std::isnan(v[j])) {
        break;
      }
    }
    for (; j < v.size(); ++j) {
      EXPECT_TRUE(std::isnan(v[j]));
    }
  }
}

TEST(RadixSortTest, NaNsDescending) {
  constexpr auto kNaNsFirstDesc = [] {
    auto opts = RadixSortOptions{};
    opts.SortOrder = RadixSortOrder::Descending;
    opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
    opts.NaNsAssumption = RadixNaNsAssumption::Existence;
    return opts;
  }();
  constexpr auto kNaNsLastDesc = [] {
    auto opts = RadixSortOptions{};
    opts.SortOrder = RadixSortOrder::Descending;
    opts.NaNsPosHandling = RadixNaNsPosHandling::AtLast;
    opts.NaNsAssumption = RadixNaNsAssumption::Existence;
    return opts;
  }();

  // NaNsFirst descending: NaN mapped to key=0, descending ~0=max => NaN last
  {
    std::vector<double> v = {NAN, 1.0, NAN, 2.0, 3.0};
    radixSort<kNaNsFirstDesc>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[3]));
    EXPECT_TRUE(std::isnan(v[4]));
  }
  // NaNsLast descending: NaN mapped to key=max, descending ~max=0 => NaN first
  {
    std::vector<double> v = {1.0, NAN, 2.0, NAN, 3.0};
    radixSort<kNaNsLastDesc>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
  }
}

// ============================================================================
// Insertion threshold boundary (63, 64, 65)
// ============================================================================

TEST(RadixSortTest, InsertionThreshold) {
  for (auto n : {63, 64, 65}) {
    std::vector<uint32_t> v(n);
    std::iota(v.begin(), v.end(), uint32_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    SCOPED_TRACE(std::format("n={}", n));
    validate(v);
  }
  for (auto n : {63, 64, 65}) {
    std::vector<int64_t> v(n);
    std::iota(v.begin(), v.end(), int64_t(-32));
    std::shuffle(v.begin(), v.end(), rng);
    SCOPED_TRACE(std::format("n={}", n));
    validate(v);
  }
  for (auto n : {63, 64}) {
    std::vector<double> v(n);
    for (auto& x : v) {
      x = static_cast<double>(rng() % 1000) / 10.0;
    }
    SCOPED_TRACE(std::format("n={}", n));
    validate(v);
  }
}

// ============================================================================
// All elements map to the same bucket
// ============================================================================

TEST(RadixSortTest, AllSameBucket) {
  {
    std::vector<uint64_t> v(1000, 0);
    validate(v);
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(0));
    }
  }
  {
    std::vector<uint32_t> v(500, 42);
    validate(v);
    for (auto x : v) {
      EXPECT_EQ(x, uint32_t(42));
    }
  }
  {
    std::vector<int64_t> v(300, -1);
    validate(v);
    for (auto x : v) {
      EXPECT_EQ(x, int64_t(-1));
    }
  }
  {
    std::vector<double> v(200, 3.14);
    validate(v);
    for (auto x : v) {
      EXPECT_EQ(x, 3.14);
    }
  }
}

// ============================================================================
// Early exit when input is already sorted
// ============================================================================

TEST(RadixSortTest, EarlyExitSorted) {
  {
    std::vector<uint64_t> v(10000);
    std::iota(v.begin(), v.end(), uint64_t(0));
    validate(v);
  }
  {
    std::vector<int64_t> v(10000);
    std::iota(v.begin(), v.end(), int64_t(-5000));
    validate(v);
  }
  {
    std::vector<double> v(10000);
    for (size_t i = 0; i < 10000; ++i) {
      v[i] = static_cast<double>(i);
    }
    validate(v);
  }
  { // descending already sorted
    std::vector<uint64_t> v(10000);
    for (size_t i = 0; i < 10000; ++i) {
      v[i] = 9999 - i;
    }
    validateDescending(v);
  }
}

// ============================================================================
// Custom projection
// ============================================================================

TEST(RadixSortTest, CustomProjection) {
  // Sort by modulo
  {
    std::vector<int32_t> v(200);
    for (auto& x : v) {
      x = static_cast<int32_t>(rng() % 1000);
    }
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), [](int32_t a, int32_t b) {
          return (a % 10) < (b % 10);
        });
    radixSort(v.begin(), v.end(), [](int32_t x) { return x % 10; });
    EXPECT_EQ(v, expected);
  }
  // Sort by negation (signed int key)
  {
    std::vector<int32_t> v(200);
    for (auto& x : v) {
      x = static_cast<int32_t>(rng() % 1000 - 500);
    }
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), [](int32_t a, int32_t b) {
          return (-a) < (-b);
        });
    radixSort(v.begin(), v.end(), [](int32_t x) { return -x; });
    EXPECT_EQ(v, expected);
  }
  // Sort by fractional part (uint64_t key via bit_cast)
  {
    auto v = generateRandom<double>(200);
    radixSort(v.begin(), v.end(), [](double x) -> uint64_t {
      return std::bit_cast<uint64_t>(x - std::floor(x));
    });
    EXPECT_TRUE(std::is_sorted(v.begin(), v.end(), [](double a, double b) {
      return std::bit_cast<uint64_t>(a - std::floor(a)) <
          std::bit_cast<uint64_t>(b - std::floor(b));
    }));
  }
  // Struct with int projection
  {
    struct Person {
      int age;
      int id;
      bool operator==(const Person&) const = default;
    };
    std::vector<Person> people(100);
    for (int i = 0; i < 100; ++i) {
      people[i] = Person{static_cast<int>(rng() % 80), i};
    }
    auto expected = people;
    std::stable_sort(
        expected.begin(), expected.end(), [](const Person& a, const Person& b) {
          return a.age < b.age;
        });
    radixSort(people.begin(), people.end(), [](const Person& p) {
      return p.age;
    });
    EXPECT_EQ(people, expected);
  }
  // Struct with double projection
  {
    struct Employee {
      double salary;
      int id;
      bool operator==(const Employee&) const = default;
    };
    std::vector<Employee> emps(100);
    for (int i = 0; i < 100; ++i) {
      emps[i] = Employee{static_cast<double>(rng() % 100000) / 100.0, i};
    }
    radixSort(emps.begin(), emps.end(), [](const Employee& e) {
      return e.salary;
    });
    EXPECT_TRUE(
        std::is_sorted(
            emps.begin(), emps.end(), [](const Employee& a, const Employee& b) {
              return a.salary < b.salary;
            }));
  }
}

// ============================================================================
// Reverse iterator
// ============================================================================

TEST(RadixSortTest, ReverseIterator) {
  // reverse + ascending = forward descending
  {
    auto v = generateRandom<uint64_t>(200);
    radixSort(v.rbegin(), v.rend());
    for (size_t i = 1; i < v.size(); ++i) {
      EXPECT_GE(v[i - 1], v[i]) << std::format("index={}", i);
    }
  }
  // reverse + descending = forward ascending
  {
    auto v = generateRandom<uint64_t>(200);
    radixSort<kDescOpts>(v.rbegin(), v.rend());
    for (size_t i = 1; i < v.size(); ++i) {
      EXPECT_LE(v[i - 1], v[i]) << std::format("index={}", i);
    }
  }
  // double + reverse iterator
  {
    auto v = generateRandom<double>(200);
    auto expected = v;
    std::stable_sort(expected.rbegin(), expected.rend(), augmentedLess<double>);
    radixSort(v.rbegin(), v.rend());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// Descending + UInt64 counter
// ============================================================================

TEST(RadixSortTest, DescendingCounter64) {
  constexpr auto kDescC64 = [] {
    auto opts = RadixSortOptions{};
    opts.SortOrder = RadixSortOrder::Descending;
    opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
    return opts;
  }();

  auto v = generateRandom<uint64_t>(10000);
  auto expected = v;
  std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t>{});
  radixSort<kDescC64>(v.begin(), v.end());
  EXPECT_EQ(v, expected);
}

// ============================================================================
// Descending + 16-bit chunks
// ============================================================================

TEST(RadixSortTest, DescendingChunk16) {
  constexpr auto kDescC16 = [] {
    auto opts = RadixSortOptions{};
    opts.SortOrder = RadixSortOrder::Descending;
    opts.BitsPerPass = RadixBitsPerPass::Bits16;
    return opts;
  }();

  {
    auto v = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint32_t>{});
    radixSort<kDescC16>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
  {
    auto v = generateRandom<double>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<double>{});
    radixSort<kDescC16>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// Single-byte types (counting sort path)
// ============================================================================

TEST(RadixSortTest, SingleByte) {
  // uint8_t: all 256 values shuffled
  {
    std::vector<uint8_t> v(256);
    std::iota(v.begin(), v.end(), uint8_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // int8_t: all 256 values shuffled
  {
    std::vector<int8_t> v(256);
    std::iota(v.begin(), v.end(), int8_t(-128));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), augmentedLess<int8_t>);
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // uint8_t: descending by augmented ordering
  {
    std::vector<uint8_t> v(50);
    std::iota(v.begin(), v.end(), uint8_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), augmentedGreater<uint8_t>);
    radixSort<kDescOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // uint8_t key extractor
  {
    std::vector<uint8_t> keys(256);
    std::iota(keys.begin(), keys.end(), uint8_t(0));
    std::shuffle(keys.begin(), keys.end(), rng);
    struct Tagged {
      uint8_t key;
      int idx;
      bool operator==(const Tagged&) const = default;
    };
    std::vector<Tagged> items;
    for (int i = 0; i < 256; ++i) {
      items.push_back({keys[i], i});
    }
    auto expected = items;
    std::stable_sort(
        expected.begin(), expected.end(), [](const Tagged& a, const Tagged& b) {
          return a.key < b.key;
        });
    radixSort(items.begin(), items.end(), [](const Tagged& x) {
      return x.key;
    });
    ASSERT_EQ(items.size(), expected.size());
    for (size_t i = 0; i < items.size(); ++i) {
      SCOPED_TRACE(std::format("index={}", i));
      EXPECT_EQ(items[i], expected[i]);
    }
  }

  // int8_t with mixed positive and negative clusters
  {
    std::vector<int8_t> v = {-128, -64, -1, 0, 1, 63, 127, -127, -2, 2};
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), augmentedLess<int8_t>);
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// char / signed char / unsigned char (standalone 1-byte types)
// ============================================================================

TEST(RadixSortTest, CharTypes) {
  // unsigned char full range 0..255
  {
    std::vector<unsigned char> v(256);
    std::iota(v.begin(), v.end(), static_cast<unsigned char>(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // signed char full range -128..127
  {
    std::vector<signed char> v(256);
    std::iota(v.begin(), v.end(), static_cast<signed char>(-128));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char all equal
  {
    std::vector<char> v(100, static_cast<char>(42));
    auto expected = v;
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char threshold boundary: 63, 64, 65
  for (auto n : {63, 64, 65}) {
    std::vector<char> v(n);
    std::iota(v.begin(), v.end(), static_cast<char>(0));
    std::shuffle(v.begin(), v.end(), rng);
    SCOPED_TRACE(std::format("char threshold n={}", n));
    radixSort(v.begin(), v.end());
    EXPECT_TRUE(std::is_sorted(v.begin(), v.end()));
  }

  // char already sorted
  {
    std::vector<char> v(100);
    std::iota(v.begin(), v.end(), static_cast<char>(0));
    auto expected = v;
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char descending
  {
    std::vector<char> v(100);
    std::iota(v.begin(), v.end(), static_cast<char>(0));
    std::reverse(v.begin(), v.end());
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<char>{});
    radixSort<kDescOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // unsigned char with only high values (128..255)
  {
    std::vector<unsigned char> v(128);
    std::iota(v.begin(), v.end(), static_cast<unsigned char>(128));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // signed char all negative (-128..-1)
  {
    std::vector<signed char> v(128);
    std::iota(v.begin(), v.end(), static_cast<signed char>(-128));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char custom projection via unsigned return type
  {
    std::vector<char> v(200);
    std::iota(v.begin(), v.end(), static_cast<char>(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto keyed = v;
    std::stable_sort(keyed.begin(), keyed.end(), [](char a, char b) {
      return static_cast<unsigned char>(a % 50) <
          static_cast<unsigned char>(b % 50);
    });
    radixSort(v.begin(), v.end(), [](char x) {
      return static_cast<unsigned char>(x % 50);
    });
    EXPECT_EQ(v, keyed);
  }
}

// ============================================================================
// wchar_t / char16_t / char32_t
// ============================================================================

TEST(RadixSortTest, WideCharTypes) {
  // char16_t random
  {
    std::vector<char16_t> v(1000);
    std::uniform_int_distribution<int> dist(0, 65535);
    for (auto& x : v) {
      x = static_cast<char16_t>(dist(rng));
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char32_t random
  {
    std::vector<char32_t> v(1000);
    std::uniform_int_distribution<uint32_t> dist;
    for (auto& x : v) {
      x = static_cast<char32_t>(dist(rng));
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char32_t large values concentrated in high byte
  {
    std::vector<char32_t> v(500);
    std::uniform_int_distribution<uint32_t> dist(0xFF000000, 0xFFFFFFFF);
    for (auto& x : v) {
      x = static_cast<char32_t>(dist(rng));
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // char32_t small values (only in low 2 bytes)
  {
    std::vector<char32_t> v(500);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);
    for (auto& x : v) {
      x = static_cast<char32_t>(dist(rng));
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // wchar_t (equivalent to char16_t on MSVC) all equal
  {
    std::vector<wchar_t> v(200, static_cast<wchar_t>(0x42));
    radixSort(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, static_cast<wchar_t>(0x42));
    }
  }

  // char16_t threshold boundary 63/64/65
  for (auto n : {63, 64, 65}) {
    std::vector<char16_t> v(n);
    std::iota(v.begin(), v.end(), static_cast<char16_t>(0));
    std::shuffle(v.begin(), v.end(), rng);
    SCOPED_TRACE(std::format("char16_t threshold n={}", n));
    radixSort(v.begin(), v.end());
    EXPECT_TRUE(std::is_sorted(v.begin(), v.end()));
  }

  // char32_t descending
  {
    auto v = generateRandom<char32_t>(500);
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), std::greater<char32_t>{});
    constexpr auto kDescChar32 = [] {
      auto opts = RadixSortOptions{};
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    radixSort<kDescChar32>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// long double
// ============================================================================

TEST(RadixSortTest, LongDouble) {
  // long double random (on MSVC, long double == double)
  {
    std::vector<long double> v(500);
    std::uniform_real_distribution<long double> dist(-1000.0, 1000.0);
    for (auto& x : v) {
      x = dist(rng);
    }
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), augmentedLess<long double>);
    radixSort(v.begin(), v.end());
    ASSERT_EQ(v.size(), expected.size());
    for (size_t i = 0; i < v.size(); ++i) {
      SCOPED_TRACE(std::format("index={}", i));
      EXPECT_EQ(v[i], expected[i]);
    }
  }

  // long double special values
  {
    std::vector<long double> v = {
        std::numeric_limits<long double>::infinity(),
        -std::numeric_limits<long double>::infinity(),
        0.0L,
        -0.0L,
        1.0L,
        -1.0L,
        std::numeric_limits<long double>::max(),
        std::numeric_limits<long double>::lowest(),
        std::numeric_limits<long double>::denorm_min(),
    };
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), augmentedLess<long double>);
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // long double NaNsFirst
  {
    constexpr auto kNaNsFirstLD = [] {
      auto opts = RadixSortOptions{};
      opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
      opts.NaNsAssumption = RadixNaNsAssumption::Existence;
      return opts;
    }();
    std::vector<long double> v = {
        static_cast<long double>(NAN),
        3.0L,
        static_cast<long double>(NAN),
        1.0L,
        static_cast<long double>(NAN),
        2.0L,
        std::numeric_limits<long double>::infinity(),
        -std::numeric_limits<long double>::infinity(),
    };
    radixSort<kNaNsFirstLD>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(static_cast<double>(v[0])));
    EXPECT_TRUE(std::isnan(static_cast<double>(v[1])));
    EXPECT_TRUE(std::isnan(static_cast<double>(v[2])));
  }

  // long double descending
  {
    std::vector<long double> v(200);
    std::uniform_real_distribution<long double> dist(-500.0, 500.0);
    for (auto& x : v) {
      x = dist(rng);
    }
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), augmentedGreater<long double>);
    constexpr auto kDescLD = [] {
      auto opts = RadixSortOptions{};
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    radixSort<kDescLD>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // long double tiny input
  {
    std::vector<long double> v = {3.14L, -2.71L, 1.41L, -1.73L};
    auto expected = v;
    std::stable_sort(
        expected.begin(), expected.end(), augmentedLess<long double>);
    radixSort(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// bool (native type)
// ============================================================================

TEST(RadixSortTest, BoolNative) {
  // bool array shuffled
  {
    bool arr[100];
    for (int i = 0; i < 100; ++i) {
      arr[i] = static_cast<bool>(rng() % 2);
    }
    bool expected[100];
    std::copy(std::begin(arr), std::end(arr), std::begin(expected));
    std::stable_sort(std::begin(expected), std::end(expected));
    radixSort(std::begin(arr), std::end(arr));
    for (int i = 0; i < 100; ++i) {
      SCOPED_TRACE(std::format("index={}", i));
      EXPECT_EQ(arr[i], expected[i]);
    }
  }

  // bool all false
  {
    bool arr[50]{};
    bool expected[50]{};
    radixSort(std::begin(arr), std::end(arr));
    for (int i = 0; i < 50; ++i) {
      EXPECT_FALSE(arr[i]);
    }
  }

  // bool all true
  {
    bool arr[50];
    std::fill(std::begin(arr), std::end(arr), true);
    radixSort(std::begin(arr), std::end(arr));
    for (int i = 0; i < 50; ++i) {
      EXPECT_TRUE(arr[i]);
    }
  }

  // bool single element
  {
    bool arr1[] = {true};
    radixSort(std::begin(arr1), std::end(arr1));
    EXPECT_TRUE(arr1[0]);
    bool arr2[] = {false};
    radixSort(std::begin(arr2), std::end(arr2));
    EXPECT_FALSE(arr2[0]);
  }

  // bool descending
  {
    bool arr[100];
    for (int i = 0; i < 100; ++i) {
      arr[i] = static_cast<bool>(rng() % 2);
    }
    bool expected[100];
    std::copy(std::begin(arr), std::end(arr), std::begin(expected));
    std::stable_sort(
        std::begin(expected), std::end(expected), std::greater<bool>{});
    constexpr auto kDescBool = [] {
      auto opts = RadixSortOptions{};
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    radixSort<kDescBool>(std::begin(arr), std::end(arr));
    for (int i = 0; i < 100; ++i) {
      SCOPED_TRACE(std::format("index={}", i));
      EXPECT_EQ(arr[i], expected[i]);
    }
  }

  // bool already sorted
  {
    bool arr[] = {false, false, false, true, true, true};
    bool expected[]{false, false, false, true, true, true};
    radixSort(std::begin(arr), std::end(arr));
    for (int i = 0; i < 6; ++i) {
      EXPECT_EQ(arr[i], expected[i]);
    }
  }

  // bool alternating (pattern stability)
  {
    bool arr[60];
    for (int i = 0; i < 60; ++i) {
      arr[i] = (i % 2 == 0);
    }
    std::shuffle(std::begin(arr), std::end(arr), rng);
    bool expected[60];
    std::copy(std::begin(arr), std::end(arr), std::begin(expected));
    std::stable_sort(std::begin(expected), std::end(expected));
    radixSort(std::begin(arr), std::end(arr));
    for (int i = 0; i < 60; ++i) {
      EXPECT_EQ(arr[i], expected[i]);
    }
  }
}

// ============================================================================
// RadixExecutionPolicy::Par (OpenMP parallel LSD)
// ============================================================================

#ifdef _OPENMP
TEST(RadixSortTest, ParLsd) {
  constexpr auto kParOpts = [] {
    auto opts = RadixSortOptions{};
    opts.ExecutionPolicy = RadixExecutionPolicy::Par;
    return opts;
  }();

  // uint64_t random parallel
  {
    auto v = generateRandom<uint64_t>(10000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // int64 random parallel (with negatives)
  {
    auto v = generateRandom<int64_t>(10000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // double random parallel
  {
    auto v = generateRandom<double>(10000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // all equal (nonEmptyCount <= 1 for all passes)
  {
    auto v = std::vector<uint64_t>(10000, 42);
    radixSort<kParOpts>(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(42));
    }
  }

  // already sorted (early exit in parallel)
  {
    auto v = std::vector<uint64_t>(10000);
    std::iota(v.begin(), v.end(), uint64_t(0));
    auto expected = v;
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // non-power-of-2 size (isChunksSorted default path)
  {
    auto v = generateRandom<uint64_t>(10001);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // small input (below insertion threshold)
  {
    auto v = generateRandom<uint64_t>(50);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Par descending
  {
    constexpr auto kParDescOpts = [] {
      auto opts = RadixSortOptions{};
      opts.ExecutionPolicy = RadixExecutionPolicy::Par;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(5000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t>{});
    radixSort<kParDescOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Par with float + NaN
  {
    constexpr auto kParNaNsFirst = [] {
      auto opts = RadixSortOptions{};
      opts.ExecutionPolicy = RadixExecutionPolicy::Par;
      opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
      opts.NaNsAssumption = RadixNaNsAssumption::Existence;
      return opts;
    }();
    std::vector<double> v = {
        NAN,
        3.0,
        NAN,
        1.0,
        NAN,
        2.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    radixSort<kParNaNsFirst>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
    EXPECT_TRUE(std::isnan(v[2]));
  }

  // Par stability (duplicate keys)
  {
    struct ItemParallel {
      int key;
      int idx;
      bool operator==(const ItemParallel&) const = default;
    };
    std::vector<ItemParallel> items(200);
    for (int i = 0; i < 200; ++i) {
      items[i] = {i % 10, i};
    }
    std::shuffle(items.begin(), items.end(), rng);
    auto expected = items;
    std::stable_sort(
        expected.begin(),
        expected.end(),
        [](const ItemParallel& a, const ItemParallel& b) {
          return a.key < b.key;
        });
    radixSort<kParOpts>(
        items.begin(),
        items.end(),
        std::allocator<ItemParallel>{},
        [](const ItemParallel& x) { return x.key; });
    EXPECT_EQ(items, expected);
  }
}
#endif // _OPENMP

// ============================================================================
// RadixSortStrategy::Msd (Most Significant Digit first)
// ============================================================================

TEST(RadixSortTest, MsdSeq) {
  constexpr auto kMsdOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
    return opts;
  }();

  // uint64 random
  {
    auto v = generateRandom<uint64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // all zero (maxVal=0 → passes=1)
  {
    auto v = std::vector<uint64_t>(500, 0);
    radixSort<kMsdOpts>(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(0));
    }
  }

  // small values 0..255 (only lowest byte varies)
  {
    std::vector<uint64_t> v(500);
    std::iota(v.begin(), v.end(), uint64_t(0));
    std::shuffle(v.begin(), v.end(), rng);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // uint64 large values with scattered MSB
  {
    std::vector<uint64_t> v(500);
    std::uniform_int_distribution<uint64_t> dist(
        uint64_t(1) << 63, std::numeric_limits<uint64_t>::max());
    for (auto& x : v) {
      x = dist(rng);
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // int64 with negatives
  {
    auto v = generateRandom<int64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // double random
  {
    auto v = generateRandom<double>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // already sorted (early exit)
  {
    auto v = std::vector<uint64_t>(1000);
    std::iota(v.begin(), v.end(), uint64_t(0));
    auto expected = v;
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // all equal (nonEmpty=1 at each pass, recursion without partitioning)
  {
    auto v = std::vector<uint64_t>(500, 42);
    radixSort<kMsdOpts>(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(42));
    }
  }

  // MSD fallback to LSD threshold boundary
  {
    constexpr auto kMsdLowThreshold = [] {
      auto opts = RadixSortOptions{};
      opts.SortStrategy = RadixSortStrategy::Msd;
      opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
      opts.MsdFallbackToLsdThreshold = 10;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(100);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdLowThreshold>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Msd descending
  {
    constexpr auto kMsdDesc = [] {
      auto opts = RadixSortOptions{};
      opts.SortStrategy = RadixSortStrategy::Msd;
      opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(500);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t>{});
    radixSort<kMsdDesc>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Msd with Bits16
  {
    constexpr auto kMsdBits16 = [] {
      auto opts = RadixSortOptions{};
      opts.SortStrategy = RadixSortStrategy::Msd;
      opts.ExecutionPolicy = RadixExecutionPolicy::Seq;
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(500);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdBits16>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Msd with uint32 (fewer passes)
  {
    auto v = generateRandom<uint32_t>(500);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Msd with small data (< 64 → insertion sort)
  {
    auto v = generateRandom<uint64_t>(50);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Msd with only one distinct bucket
  {
    std::vector<uint64_t> v(100);
    for (auto& x : v) {
      x = uint64_t(0xFF) << 56;
    }
    v[0] = uint64_t(0xFE) << 56;
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================
// RadixSortStrategy::Msd + RadixExecutionPolicy::Par
// ============================================================================

#ifdef _OPENMP
TEST(RadixSortTest, MsdPar) {
  constexpr auto kMsdParOpts = [] {
    auto opts = RadixSortOptions{};
    opts.SortStrategy = RadixSortStrategy::Msd;
    opts.ExecutionPolicy = RadixExecutionPolicy::Par;
    return opts;
  }();

  // uint64 random
  {
    auto v = generateRandom<uint64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // all zero (maxVal=0 → passes=1)
  {
    auto v = std::vector<uint64_t>(500, 0);
    radixSort<kMsdParOpts>(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(0));
    }
  }

  // int64 with negatives
  {
    auto v = generateRandom<int64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // double random
  {
    auto v = generateRandom<double>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // already sorted (early exit)
  {
    auto v = std::vector<uint64_t>(1000);
    std::iota(v.begin(), v.end(), uint64_t(0));
    auto expected = v;
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // all equal (nonEmpty=1 at each pass)
  {
    auto v = std::vector<uint64_t>(500, 42);
    radixSort<kMsdParOpts>(v.begin(), v.end());
    for (auto x : v) {
      EXPECT_EQ(x, uint64_t(42));
    }
  }

  // MsdPar descending
  {
    constexpr auto kMsdParDesc = [] {
      auto opts = RadixSortOptions{};
      opts.SortStrategy = RadixSortStrategy::Msd;
      opts.ExecutionPolicy = RadixExecutionPolicy::Par;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(500);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t>{});
    radixSort<kMsdParDesc>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // non-power-of-2 size (isChunksSorted default path)
  {
    auto v = generateRandom<uint64_t>(1001);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // small data (< 64 → insertion sort)
  {
    auto v = generateRandom<uint64_t>(50);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // MsdPar with one distinct bucket
  {
    std::vector<uint64_t> v(100);
    for (auto& x : v) {
      x = uint64_t(0xFF) << 56;
    }
    v[0] = uint64_t(0xFE) << 56;
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // MsdPar fallback to LSD threshold
  {
    constexpr auto kMsdParLowThreshold = [] {
      auto opts = RadixSortOptions{};
      opts.SortStrategy = RadixSortStrategy::Msd;
      opts.ExecutionPolicy = RadixExecutionPolicy::Par;
      opts.MsdFallbackToLsdThreshold = 10;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(100);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParLowThreshold>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // MsdPar stability (duplicate keys)
  {
    auto v = generateRandom<uint64_t>(500);
    for (auto& x : v) {
      x &= 0x1F;
    }
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kMsdParOpts>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}
#endif // _OPENMP

// ============================================================================
// RadixSortOptions combination matrix
// ============================================================================

TEST(RadixSortTest, OptionsMatrix) {
  // Bits16 + Ascending
  {
    constexpr auto kM1 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.SortOrder = RadixSortOrder::Ascending;
      return opts;
    }();
    auto v = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kM1>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Bits16 + Descending
  {
    constexpr auto kM2 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<uint32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint32_t>{});
    radixSort<kM2>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Bits16 + Counter64 + Asc
  {
    constexpr auto kM3 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Ascending;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end());
    radixSort<kM3>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Bits8 + Counter64 + Desc
  {
    constexpr auto kM4 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits8;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<uint64_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<uint64_t>{});
    radixSort<kM4>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Bits16 + Counter64 + Desc + double
  {
    constexpr auto kM5 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<double>(500);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<double>{});
    radixSort<kM5>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }

  // Bits16 + AtFirst + double
  {
    constexpr auto kM6 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
      opts.NaNsAssumption = RadixNaNsAssumption::Existence;
      return opts;
    }();
    std::vector<double> v = {
        NAN,
        1.0,
        NAN,
        2.0,
        3.0,
        -1.0,
        -2.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    radixSort<kM6>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
  }

  // Bits8 + Counter64 + AtFirst + Desc + float
  {
    constexpr auto kM7 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits8;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Descending;
      opts.NaNsPosHandling = RadixNaNsPosHandling::AtFirst;
      opts.NaNsAssumption = RadixNaNsAssumption::Existence;
      return opts;
    }();
    std::vector<float> v = {
        NAN,
        1.0f,
        NAN,
        2.0f,
        3.0f,
        -1.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    radixSort<kM7>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[6]));
    EXPECT_TRUE(std::isnan(v[7]));
  }

  // Bits16 + Counter64 + AtLast + Desc + double
  {
    constexpr auto kM8 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Descending;
      opts.NaNsPosHandling = RadixNaNsPosHandling::AtLast;
      opts.NaNsAssumption = RadixNaNsAssumption::Existence;
      return opts;
    }();
    std::vector<double> v = {
        1.0,
        NAN,
        2.0,
        NAN,
        3.0,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    radixSort<kM8>(v.begin(), v.end());
    EXPECT_TRUE(std::isnan(v[0]));
    EXPECT_TRUE(std::isnan(v[1]));
  }

  // int32 + Bits16 + Counter64 + Desc
  {
    constexpr auto kM9 = [] {
      auto opts = RadixSortOptions{};
      opts.BitsPerPass = RadixBitsPerPass::Bits16;
      opts.HistogramStorageType = RadixHistogramStorageType::UInt64;
      opts.SortOrder = RadixSortOrder::Descending;
      return opts;
    }();
    auto v = generateRandom<int32_t>(1000);
    auto expected = v;
    std::stable_sort(expected.begin(), expected.end(), std::greater<int32_t>{});
    radixSort<kM9>(v.begin(), v.end());
    EXPECT_EQ(v, expected);
  }
}

// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
