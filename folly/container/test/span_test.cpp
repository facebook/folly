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

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#if __cpp_lib_span >= 202002L
#include <span>
#endif

#if __cpp_lib_span >= 202002L

static_assert(
    std::dynamic_extent == folly::detail::fallback_span::dynamic_extent);

#endif

template <typename Param>
struct SpanTest : public testing::TestWithParam<Param> {};
TYPED_TEST_SUITE_P(SpanTest);

template <template <typename T, size_t N> typename Span>
struct quote {
  template <typename T, size_t N>
  using apply = Span<T, N>;
};

TYPED_TEST_P(SpanTest, dyn_traits) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  EXPECT_TRUE(std::is_trivially_copy_constructible_v<span>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<span>);
  EXPECT_TRUE(std::is_trivially_destructible_v<span>);
}

TYPED_TEST_P(SpanTest, dyn_ctor_default) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  auto const obj = span{};
  EXPECT_TRUE(obj.empty());
  EXPECT_EQ(0, obj.size());
  EXPECT_EQ(nullptr, obj.data());
  EXPECT_THAT(obj, testing::ElementsAre());
}

TYPED_TEST_P(SpanTest, dyn_ctor_pointer_size) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data + 1, 1};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(1, obj.size());
  EXPECT_EQ(data + 1, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray({6}));
}

TYPED_TEST_P(SpanTest, dyn_ctor_pointer_pointer) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data + 1, data + 2};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(1, obj.size());
  EXPECT_EQ(data + 1, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray({6}));
}

TYPED_TEST_P(SpanTest, dyn_ctor_c_array) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, obj.size());
  EXPECT_EQ(data, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(data));
}

TYPED_TEST_P(SpanTest, dyn_ctor_std_array) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  std::array<int, 3> array{5, 6, 7};
  auto const obj = span{array};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, obj.size());
  EXPECT_EQ(array.data(), obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(array));
}

TYPED_TEST_P(SpanTest, dyn_ctor_std_vector) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  std::vector<int> vector{5, 6, 7};
  auto const obj = span{vector};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, vector.size());
  EXPECT_EQ(vector.data(), obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(vector));
}

TYPED_TEST_P(SpanTest, dyn_access) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data};
  EXPECT_EQ(data, &obj.front());
  EXPECT_EQ(data + 2, &obj.back());
  EXPECT_EQ(data + 1, &obj[1]);
  EXPECT_EQ(12, obj.size_bytes());
}

TYPED_TEST_P(SpanTest, dyn_static_subspan) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  EXPECT_THAT((obj.template subspan<0>()), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<1>()), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<5>()), testing::ElementsAre());
  EXPECT_THAT((obj.template subspan<0, 4>()), testing::ElementsAre(5, 6, 7, 8));
  EXPECT_THAT((obj.template subspan<1, 4>()), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<3, 0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template first<0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template first<2>()), testing::ElementsAre(5, 6));
  EXPECT_THAT((obj.template first<5>()), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT((obj.template last<0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template last<2>()), testing::ElementsAre(8, 9));
  EXPECT_THAT((obj.template last<5>()), testing::ElementsAre(5, 6, 7, 8, 9));
}

TYPED_TEST_P(SpanTest, dyn_dynamic_subspan) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  EXPECT_THAT(obj.subspan(0), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(1), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(5), testing::ElementsAre());
  EXPECT_THAT(obj.subspan(0, 4), testing::ElementsAre(5, 6, 7, 8));
  EXPECT_THAT(obj.subspan(1, 4), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(3, 0), testing::ElementsAre());
  EXPECT_THAT(obj.first(0), testing::ElementsAre());
  EXPECT_THAT(obj.first(2), testing::ElementsAre(5, 6));
  EXPECT_THAT(obj.first(5), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT(obj.last(0), testing::ElementsAre());
  EXPECT_THAT(obj.last(2), testing::ElementsAre(8, 9));
  EXPECT_THAT(obj.last(5), testing::ElementsAre(5, 6, 7, 8, 9));
}

TYPED_TEST_P(SpanTest, dyn_as_bytes) {
  using span = typename TypeParam::template apply<int, size_t(-1)>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  auto const bytes = as_bytes(obj);
  EXPECT_EQ(static_cast<void*>(data), bytes.data());
  EXPECT_EQ(20, bytes.size());
  auto const wbytes = as_bytes(obj);
  EXPECT_EQ(static_cast<void*>(data), wbytes.data());
  EXPECT_EQ(20, wbytes.size());
}

TYPED_TEST_P(SpanTest, nul_traits) {
  using span = typename TypeParam::template apply<int, 0>;

  EXPECT_TRUE(std::is_trivially_copy_constructible_v<span>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<span>);
  EXPECT_TRUE(std::is_trivially_destructible_v<span>);
}

TYPED_TEST_P(SpanTest, nul_ctor_default) {
  using span = typename TypeParam::template apply<int, 0>;

  auto const obj = span{};
  EXPECT_TRUE(obj.empty());
  EXPECT_EQ(0, obj.size());
  EXPECT_EQ(nullptr, obj.data());
  EXPECT_THAT(obj, testing::ElementsAre());
}

TYPED_TEST_P(SpanTest, fix_traits) {
  using span = typename TypeParam::template apply<int, 3>;

  EXPECT_TRUE(std::is_trivially_copy_constructible_v<span>);
  EXPECT_TRUE(std::is_trivially_copy_assignable_v<span>);
  EXPECT_TRUE(std::is_trivially_destructible_v<span>);
}

TYPED_TEST_P(SpanTest, fix_ctor_pointer_size) {
  using span = typename TypeParam::template apply<int, 1>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data + 1, 1};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(1, obj.size());
  EXPECT_EQ(data + 1, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray({6}));
}

TYPED_TEST_P(SpanTest, fix_ctor_pointer_pointer) {
  using span = typename TypeParam::template apply<int, 1>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data + 1, data + 2};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(1, obj.size());
  EXPECT_EQ(data + 1, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray({6}));
}

TYPED_TEST_P(SpanTest, fix_ctor_c_array) {
  using span = typename TypeParam::template apply<int, 3>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, obj.size());
  EXPECT_EQ(data, obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(data));
}

TYPED_TEST_P(SpanTest, fix_ctor_std_array) {
  using span = typename TypeParam::template apply<int, 3>;

  std::array<int, 3> array{5, 6, 7};
  auto const obj = span{array};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, obj.size());
  EXPECT_EQ(array.data(), obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(array));
}

TYPED_TEST_P(SpanTest, fix_ctor_std_vector) {
  using span = typename TypeParam::template apply<int, 3>;

  std::vector<int> vector{5, 6, 7};
  auto const obj = span{vector};
  EXPECT_FALSE(obj.empty());
  EXPECT_EQ(3, vector.size());
  EXPECT_EQ(vector.data(), obj.data());
  EXPECT_THAT(obj, testing::ElementsAreArray(vector));
}

TYPED_TEST_P(SpanTest, fix_access) {
  using span = typename TypeParam::template apply<int, 3>;

  int data[3] = {5, 6, 7};
  auto const obj = span{data};
  EXPECT_EQ(data, &obj.front());
  EXPECT_EQ(data + 2, &obj.back());
  EXPECT_EQ(data + 1, &obj[1]);
  EXPECT_EQ(12, obj.size_bytes());
}

TYPED_TEST_P(SpanTest, fix_static_subspan) {
  using span = typename TypeParam::template apply<int, 5>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  EXPECT_THAT((obj.template subspan<0>()), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<1>()), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<5>()), testing::ElementsAre());
  EXPECT_THAT((obj.template subspan<0, 4>()), testing::ElementsAre(5, 6, 7, 8));
  EXPECT_THAT((obj.template subspan<1, 4>()), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT((obj.template subspan<3, 0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template first<0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template first<2>()), testing::ElementsAre(5, 6));
  EXPECT_THAT((obj.template first<5>()), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT((obj.template last<0>()), testing::ElementsAre());
  EXPECT_THAT((obj.template last<2>()), testing::ElementsAre(8, 9));
  EXPECT_THAT((obj.template last<5>()), testing::ElementsAre(5, 6, 7, 8, 9));
}

TYPED_TEST_P(SpanTest, fix_dynamic_subspan) {
  using span = typename TypeParam::template apply<int, 5>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  EXPECT_THAT(obj.subspan(0), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(1), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(5), testing::ElementsAre());
  EXPECT_THAT(obj.subspan(0, 4), testing::ElementsAre(5, 6, 7, 8));
  EXPECT_THAT(obj.subspan(1, 4), testing::ElementsAre(6, 7, 8, 9));
  EXPECT_THAT(obj.subspan(3, 0), testing::ElementsAre());
  EXPECT_THAT(obj.first(0), testing::ElementsAre());
  EXPECT_THAT(obj.first(2), testing::ElementsAre(5, 6));
  EXPECT_THAT(obj.first(5), testing::ElementsAre(5, 6, 7, 8, 9));
  EXPECT_THAT(obj.last(0), testing::ElementsAre());
  EXPECT_THAT(obj.last(2), testing::ElementsAre(8, 9));
  EXPECT_THAT(obj.last(5), testing::ElementsAre(5, 6, 7, 8, 9));
}

TYPED_TEST_P(SpanTest, fix_as_bytes) {
  using span = typename TypeParam::template apply<int, 5>;

  int data[5] = {5, 6, 7, 8, 9};
  auto const obj = span{data};
  auto const bytes = as_bytes(obj);
  EXPECT_EQ(static_cast<void*>(data), bytes.data());
  EXPECT_EQ(20, bytes.size());
  auto const wbytes = as_bytes(obj);
  EXPECT_EQ(static_cast<void*>(data), wbytes.data());
  EXPECT_EQ(20, wbytes.size());
}

namespace fallback_span_ctad {

namespace fallback = folly::detail::fallback_span;

template <typename... Ts>
using deduced_for = decltype(fallback::span(std::declval<Ts>()...));

static_assert( //
    std::is_same_v<
        fallback::span<const int>,
        deduced_for<const int*, std::size_t>>);

static_assert( //
    std::is_same_v<
        fallback::span<const int>,
        deduced_for<const std::vector<int>&>>);

static_assert( //
    std::is_same_v<fallback::span<int>, deduced_for<std::vector<int>&>>);

static_assert(
    std::is_same_v<fallback::span<int, 3>, deduced_for<std::array<int, 3>&>>);

static_assert( //
    std::is_same_v<
        fallback::span<const int, 3>,
        deduced_for<const std::array<int, 3>&>>);

int arr1[3];
static_assert( //
    std::is_same_v<fallback::span<int, 3>, decltype(fallback::span(arr1))>);

constexpr int arr2[3]{0, 1, 2};
static_assert( //
    std::is_same_v<
        fallback::span<const int, 3>,
        decltype(fallback::span(arr2))>);

} // namespace fallback_span_ctad

// clang-format off
REGISTER_TYPED_TEST_SUITE_P(
    SpanTest
    , dyn_traits
    , dyn_ctor_default
    , dyn_ctor_pointer_size
    , dyn_ctor_pointer_pointer
    , dyn_ctor_c_array
    , dyn_ctor_std_array
    , dyn_ctor_std_vector
    , dyn_access
    , dyn_static_subspan
    , dyn_dynamic_subspan
    , dyn_as_bytes
    , nul_traits
    , nul_ctor_default
    , fix_traits
    , fix_ctor_pointer_size
    , fix_ctor_pointer_pointer
    , fix_ctor_c_array
    , fix_ctor_std_array
    , fix_ctor_std_vector
    , fix_access
    , fix_static_subspan
    , fix_dynamic_subspan
    , fix_as_bytes
    );
// clang-format on

INSTANTIATE_TYPED_TEST_SUITE_P(
    folly_fallback_span, SpanTest, quote<folly::detail::fallback_span::span>);
INSTANTIATE_TYPED_TEST_SUITE_P(folly_span, SpanTest, quote<folly::span>);
#if __cpp_lib_span >= 202002L
INSTANTIATE_TYPED_TEST_SUITE_P(std_span, SpanTest, quote<std::span>);
#endif

#if __cpp_lib_span

template <typename To, typename From>
using reinterpret_span_cast_result_type =
    decltype(folly::reinterpret_span_cast<To>(std::declval<From>()));

template <typename To, typename From>
using static_span_cast_result_type =
    decltype(folly::const_span_cast<To>(std::declval<From>()));

template <typename To, typename From>
using const_span_cast_result_type =
    decltype(folly::const_span_cast<To>(std::declval<From>()));

static_assert( //
    std::is_same_v<
        std::span<char>,
        reinterpret_span_cast_result_type<char, std::span<int>>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        reinterpret_span_cast_result_type<const char, std::span<const int>>>);
static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        reinterpret_span_cast_result_type<
            const char,
            std::span<const int, 3>>>);

static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        const_span_cast_result_type<const char, std::span<char, 12>>>);
static_assert( //
    std::is_same_v<
        std::span<char, 12>,
        const_span_cast_result_type<char, std::span<const char, 12>>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        const_span_cast_result_type<const char, std::span<char>>>);

static_assert( //
    std::is_same_v<
        std::span<char>,
        const_span_cast_result_type<char, std::span<const char>>>);

static_assert( //
    std::is_same_v<
        std::span<const char, 12>,
        static_span_cast_result_type<const char, std::span<char, 12>>>);

static_assert( //
    std::is_same_v<
        std::span<const char>,
        static_span_cast_result_type<const char, std::span<char>>>);

struct SpanCastTest : testing::Test {
  template <typename To, typename From>
  static auto test(To to, From from) {
    EXPECT_EQ(
        static_cast<const void*>(from.data()),
        static_cast<const void*>(to.data()));

    EXPECT_EQ(
        static_cast<const void*>(from.data() + from.size()),
        static_cast<const void*>(to.data() + to.size()));
  }
};

TEST_F(SpanCastTest, array) {
  std::array<int, 4> a;
  test(folly::reinterpret_span_cast<const char>(std::span(a)), std::span(a));
  test(folly::reinterpret_span_cast<double>(std::span(a)), std::span(a));
}

TEST_F(SpanCastTest, vector) {
  std::vector<int> a(4u, 1);
  test(folly::reinterpret_span_cast<const char>(std::span(a)), std::span(a));
  test(folly::reinterpret_span_cast<double>(std::span(a)), std::span(a));
}

TEST_F(SpanCastTest, const_cast) {
  const std::vector<int> a(4u, 1);
  test(folly::const_span_cast<int>(std::span(a)), std::span(a));
}

TEST_F(SpanCastTest, all_casts) {
  std::vector<int> b(4u, 1);
  test(folly::static_span_cast<const int>(std::span(b)), std::span(b));
  test(folly::const_span_cast<const int>(std::span(b)), std::span(b));
  test(folly::reinterpret_span_cast<const int>(std::span(b)), std::span(b));
}

TEST_F(SpanCastTest, static_cast_constexpr) {
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

#endif
