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

#include <folly/container/span.h>

#include <type_traits>
#include <utility>

#include <folly/portability/GTest.h>

#if __cpp_lib_span

template <typename To, typename From, std::size_t Extend = std::dynamic_extent>
using reinterpret_span_cast_result_type =
    decltype(folly::reinterpret_span_cast<To>(
        std::declval<std::span<From, Extend>>()));

template <typename To, typename From, std::size_t Extend = std::dynamic_extent>
using static_span_cast_result_type = decltype(folly::const_span_cast<To>(
    std::declval<std::span<From, Extend>>()));

template <typename To, typename From, std::size_t Extend = std::dynamic_extent>
using const_span_cast_result_type = decltype(folly::const_span_cast<To>(
    std::declval<std::span<From, Extend>>()));

static_assert( //
    std::is_same_v<
        std::span<char>,
        reinterpret_span_cast_result_type<char, int>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        reinterpret_span_cast_result_type<const char, const int>>);
static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        reinterpret_span_cast_result_type<const char, const int, 3>>);

static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        const_span_cast_result_type<const char, char, 12>>);
static_assert( //
    std::is_same_v<
        std::span<char, 12>,
        const_span_cast_result_type<char, const char, 12>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        const_span_cast_result_type<const char, char>>);

static_assert( //
    std::is_same_v<
        std::span<char>,
        const_span_cast_result_type<char, const char>>);

static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        static_span_cast_result_type<const char, char, 12>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        static_span_cast_result_type<const char, char>>);

TEST(SpanCast, Examples) {
  auto tstSpanCast = [](auto to, auto from) {
    EXPECT_EQ(
        static_cast<const void*>(from.data()),
        static_cast<const void*>(to.data()));

    EXPECT_EQ(
        static_cast<const void*>(from.data() + from.size()),
        static_cast<const void*>(to.data() + to.size()));
  };

  {
    std::array<int, 4> a;
    tstSpanCast(
        folly::reinterpret_span_cast<const char>(std::span(a)), std::span(a));
    tstSpanCast(
        folly::reinterpret_span_cast<double>(std::span(a)), std::span(a));
  }

  {
    std::vector<int> a(4u, 1);
    tstSpanCast(
        folly::reinterpret_span_cast<const char>(std::span(a)), std::span(a));
    tstSpanCast(
        folly::reinterpret_span_cast<double>(std::span(a)), std::span(a));
  }

  {
    const std::vector<int> a(4u, 1);
    tstSpanCast(folly::const_span_cast<int>(std::span(a)), std::span(a));

    std::vector<int> b(4u, 1);
    tstSpanCast(folly::static_span_cast<const int>(std::span(b)), std::span(b));
    tstSpanCast(folly::const_span_cast<const int>(std::span(b)), std::span(b));
    tstSpanCast(
        folly::reinterpret_span_cast<const int>(std::span(b)), std::span(b));
  }

  // constexpr
  {
    constexpr bool validation = std::invoke([] {
      std::array<int, 4> a{0, 1, 2, 3};
      std::span<int, 4> mutableAFixed(a);
      std::span<int> mutableADynamic(a);
      auto resFixed = folly::static_span_cast<const int>(mutableAFixed);
      if (resFixed.data() != mutableAFixed.data() ||
          resFixed.size() != mutableAFixed.size()) {
        return false;
      }
      auto resDynamic = folly::static_span_cast<const int>(mutableADynamic);
      if (resDynamic.data() != mutableAFixed.data() ||
          resDynamic.size() != mutableAFixed.size()) {
        return false;
      }

      return true;
    });
    EXPECT_TRUE(validation);
  }
}

#endif
