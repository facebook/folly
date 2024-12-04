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

#include <folly/concurrency/container/atomic_grow_array.h>

#include <atomic>
#include <numeric>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

extern "C" FOLLY_KEEP int check_folly_atomic_grow_array_index(
    folly::atomic_grow_array<int>& array, size_t const index) {
  return array[index];
}

static_assert( //
    std::is_nothrow_default_constructible_v< //
        folly::atomic_grow_array<int>::iterator>);
static_assert( //
    std::is_nothrow_default_constructible_v<
        folly::atomic_grow_array<int>::const_iterator>);
static_assert( //
    std::is_nothrow_copy_constructible_v<
        folly::atomic_grow_array<int>::iterator>);
static_assert( //
    std::is_nothrow_copy_constructible_v<
        folly::atomic_grow_array<int>::const_iterator>);
static_assert( //
    std::is_nothrow_constructible_v<
        folly::atomic_grow_array<int>::const_iterator,
        folly::atomic_grow_array<int>::iterator const&>);
static_assert( //
    std::is_nothrow_copy_assignable_v< //
        folly::atomic_grow_array<int>::iterator>);
static_assert( //
    std::is_nothrow_copy_assignable_v<
        folly::atomic_grow_array<int>::const_iterator>);
static_assert( //
    std::is_nothrow_assignable_v<
        folly::atomic_grow_array<int>::const_iterator,
        folly::atomic_grow_array<int>::iterator const&>);

static_assert( //
    std::is_nothrow_default_constructible_v<
        folly::atomic_grow_array<int>::view>);
static_assert( //
    std::is_nothrow_default_constructible_v<
        folly::atomic_grow_array<int>::const_view>);
static_assert( //
    std::is_nothrow_copy_constructible_v< //
        folly::atomic_grow_array<int>::view>);
static_assert( //
    std::is_nothrow_copy_constructible_v<
        folly::atomic_grow_array<int>::const_view>);
static_assert( //
    std::is_nothrow_constructible_v<
        folly::atomic_grow_array<int>::const_view,
        folly::atomic_grow_array<int>::view const&>);
static_assert( //
    std::is_nothrow_copy_assignable_v< //
        folly::atomic_grow_array<int>::view>);
static_assert( //
    std::is_nothrow_copy_assignable_v<
        folly::atomic_grow_array<int>::const_view>);
static_assert( //
    std::is_nothrow_assignable_v<
        folly::atomic_grow_array<int>::const_view,
        folly::atomic_grow_array<int>::view const&>);

struct AtomicGrowArrayTest : testing::Test {};

TEST_F(AtomicGrowArrayTest, example) {
  static constexpr size_t max_size = 36;
  struct policy_t {
    mutable size_t size_counter{0};
    mutable int item_counter{0};
    size_t grow(size_t /* curr */, size_t /* index */) const {
      if (size_counter < max_size) {
        ++size_counter;
      }
      return size_counter;
    }
    int make() const { return item_counter++; }
  };

  policy_t policy{0};
  folly::atomic_grow_array<int, policy_t> array{policy};
  EXPECT_EQ(0, array.size());

  for (size_t i = 0; i < max_size; ++i) {
    EXPECT_EQ(i, array[i]);
    EXPECT_LT(i, array.size());
  }

  EXPECT_EQ(max_size, array.size());
  EXPECT_EQ(max_size, array.as_view().size());
  EXPECT_EQ(max_size, std::as_const(array).as_view().size());
  EXPECT_FALSE(array.empty());
  EXPECT_FALSE(array.as_view().empty());
  EXPECT_FALSE(std::as_const(array).as_view().empty());
}

TEST_F(AtomicGrowArrayTest, empty) {
  folly::atomic_grow_array<std::atomic<int>> array;
  EXPECT_EQ(0, array.size());
  EXPECT_EQ(0, array.as_view().size());
  EXPECT_EQ(0, std::as_const(array).as_view().size());
  EXPECT_TRUE(array.empty());
  EXPECT_TRUE(array.as_view().empty());
  EXPECT_TRUE(std::as_const(array).as_view().empty());

  auto const expected = 0;
  auto const count = [](auto& v) {
    return std::accumulate(v.begin(), v.end(), 0);
  };
  auto const countp = [](auto& v) {
    auto const sum = [](auto a, auto* b) { return a + *b; };
    return std::accumulate(v.begin(), v.end(), 0, sum);
  };
  {
    auto view = array.as_view();
    EXPECT_EQ(expected, count(view));
    auto span = array.as_ptr_span();
    EXPECT_EQ(expected, countp(span));
  }
  {
    auto const view = array.as_view();
    EXPECT_EQ(expected, count(view));
    auto const span = array.as_ptr_span();
    EXPECT_EQ(expected, countp(span));
  }
  {
    auto view = std::as_const(array).as_view();
    EXPECT_EQ(expected, count(view));
    auto span = std::as_const(array).as_ptr_span();
    EXPECT_EQ(expected, countp(span));
  }
  {
    auto const view = std::as_const(array).as_view();
    EXPECT_EQ(expected, count(view));
    auto const span = std::as_const(array).as_ptr_span();
    EXPECT_EQ(expected, countp(span));
  }
}

TEST_F(AtomicGrowArrayTest, stress) {
  constexpr size_t num_iters = size_t(1) << 6;
  constexpr size_t num_threads = size_t(1) << 5;
  constexpr size_t max_size = size_t(1) << 10;

  for (size_t i = 0; i < num_iters; ++i) {
    folly::atomic_grow_array<std::atomic<int>> array;
    std::vector<std::thread> threads(num_threads);
    for (auto& th : threads) {
      th = std::thread([&] {
        for (size_t j = 0; j < max_size; ++j) {
          array[j]++;
        }
      });
    }
    for (auto& th : threads) {
      th.join();
    }
    auto const expected = max_size * num_threads;
    auto const count = [](auto& v) {
      return std::accumulate(v.begin(), v.end(), 0);
    };
    {
      auto view = array.as_view();
      EXPECT_EQ(expected, count(view));
    }
    {
      auto const view = array.as_view();
      EXPECT_EQ(expected, count(view));
    }
    {
      auto view = std::as_const(array).as_view();
      EXPECT_EQ(expected, count(view));
    }
    {
      auto const view = std::as_const(array).as_view();
      EXPECT_EQ(expected, count(view));
    }
  }
}
