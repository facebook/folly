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

#include <folly/algorithm/simd/detail/Traits.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <set>
#include <vector>

namespace folly::simd::detail {

struct SimdTraitsTest : testing::Test {};

namespace simd_friendly_equivalent_scalar_test {

// ints
static_assert( //
    std::is_same_v< //
        std::int8_t,
        simd_friendly_equivalent_scalar_t<signed char>>);
static_assert( //
    std::is_same_v<
        std::uint8_t,
        simd_friendly_equivalent_scalar_t<unsigned char>>);

static_assert( //
    std::is_same_v< //
        std::int16_t,
        simd_friendly_equivalent_scalar_t<short>>);
static_assert( //
    std::is_same_v<
        std::uint16_t,
        simd_friendly_equivalent_scalar_t<unsigned short>>);

static_assert( //
    std::is_same_v< //
        std::int32_t,
        simd_friendly_equivalent_scalar_t<int>>);
static_assert( //
    std::is_same_v<
        std::uint32_t,
        simd_friendly_equivalent_scalar_t<unsigned int>>);

static_assert( //
    std::is_same_v<
        std::int64_t,
        simd_friendly_equivalent_scalar_t<std::int64_t>>);
static_assert( //
    std::is_same_v<
        std::uint64_t,
        simd_friendly_equivalent_scalar_t<std::uint64_t>>);

// floats
static_assert( //
    std::is_same_v<float, simd_friendly_equivalent_scalar_t<float>>);
static_assert(
    std::is_same_v<double, simd_friendly_equivalent_scalar_t<double>>);

// enum
enum SomeInt : std::int16_t {};
enum class SomeIntClass : std::uint32_t {};

static_assert( //
    std::is_same_v< //
        std::int16_t,
        simd_friendly_equivalent_scalar_t<SomeInt>>);
static_assert( //
    std::is_same_v<
        std::uint32_t,
        simd_friendly_equivalent_scalar_t<SomeIntClass>>);

// const

static_assert( //
    std::is_same_v<
        const std::int32_t,
        simd_friendly_equivalent_scalar_t<const int>>);

// pointers

static_assert(
    sizeof(std::int64_t) != sizeof(void*) ||
    std::is_same_v<std::int64_t, simd_friendly_equivalent_scalar_t<void*>>);

// sfinae

struct sfinae_call {
  template <typename T>
  simd_friendly_equivalent_scalar_t<T> operator()(T) const {
    return {};
  }
};

static_assert(std::is_invocable_v<sfinae_call, int>);

struct NotSimdFriendly {};
static_assert(!std::is_invocable_v<sfinae_call, NotSimdFriendly>);

} // namespace simd_friendly_equivalent_scalar_test

namespace as_simd_friendly_type_test {

template <typename T>
using AsSimdFriendlyResult = std::invoke_result_t<AsSimdFriendlyFn, T>;

static_assert( //
    std::is_same_v<
        folly::span<std::int32_t>,
        AsSimdFriendlyResult<folly::span<std::int32_t>>>);

static_assert( //
    std::is_same_v<
        folly::span<std::int32_t>,
        AsSimdFriendlyResult<folly::span<int>>>);

static_assert( //
    std::is_same_v<
        folly::span<std::int32_t>,
        AsSimdFriendlyResult<std::vector<int>&>>);

static_assert( //
    std::is_same_v<
        folly::span<const std::int32_t>,
        AsSimdFriendlyResult<const std::vector<int>&>>);

static_assert( //
    std::is_same_v<
        folly::span<const double>,
        AsSimdFriendlyResult<const std::vector<double>&>>);

static_assert(std::is_same_v<double, AsSimdFriendlyResult<double>>);

static_assert(!std::is_invocable_v<AsSimdFriendlyFn, std::set<int>>);

} // namespace as_simd_friendly_type_test

namespace as_simd_friendly_uint_type_test {

template <typename T>
using AsSimdFriendlyUintResult = std::invoke_result_t<AsSimdFriendlyUintFn, T>;

static_assert( //
    std::is_same_v<
        folly::span<std::uint32_t>,
        AsSimdFriendlyUintResult<folly::span<std::uint32_t>>>);

static_assert( //
    std::is_same_v<
        folly::span<std::uint32_t>,
        AsSimdFriendlyUintResult<folly::span<std::int32_t>>>);

static_assert( //
    std::is_same_v<
        folly::span<std::uint32_t>,
        AsSimdFriendlyUintResult<folly::span<int>>>);

static_assert( //
    std::is_same_v<
        folly::span<std::uint32_t>,
        AsSimdFriendlyUintResult<std::vector<int>&>>);

static_assert( //
    std::is_same_v<
        folly::span<const std::uint32_t>,
        AsSimdFriendlyUintResult<const std::vector<std::uint32_t>&>>);

static_assert(
    std::is_same_v<std::uint32_t, AsSimdFriendlyUintResult<std::int32_t>>);

static_assert(
    !std::is_invocable_v<AsSimdFriendlyUintFn, const std::vector<double>&>);

static_assert(
    !std::is_invocable_v<AsSimdFriendlyUintFn, const std::vector<double>&>);

static_assert(!std::is_invocable_v<AsSimdFriendlyUintFn, std::set<int>>);

} // namespace as_simd_friendly_uint_type_test

TEST_F(SimdTraitsTest, AsSimdFriendly) {
  enum SomeEnum : int { Foo = 1, Bar, Baz };

  static_assert(asSimdFriendly(SomeEnum::Foo) == 1);

  std::array arr{SomeEnum::Foo, SomeEnum::Bar, SomeEnum::Baz};
  folly::span<int, 3> castSpan = asSimdFriendly(folly::span(arr));
  ASSERT_THAT(castSpan, testing::ElementsAre(1, 2, 3));

  // pointer
  {
    auto expected = folly::bit_cast<std::intptr_t>(arr.data());
    auto actual = asSimdFriendly(arr.data());
    ASSERT_EQ(expected, actual);
  }
}

TEST_F(SimdTraitsTest, AsSimdFriendlyUint) {
  enum SomeEnum : int { Foo = 1, Bar, Baz };

  static_assert(asSimdFriendlyUint(SomeEnum::Foo) == 1U);

  std::array arr{SomeEnum::Foo, SomeEnum::Bar, SomeEnum::Baz};
  folly::span<std::uint32_t, 3> castSpan = asSimdFriendlyUint(folly::span(arr));
  ASSERT_THAT(castSpan, testing::ElementsAre(1, 2, 3));

  // pointer
  {
    auto expected = folly::bit_cast<std::uintptr_t>(arr.data());
    auto actual = asSimdFriendlyUint(arr.data());
    ASSERT_EQ(expected, actual);
  }
}

} // namespace folly::simd::detail
