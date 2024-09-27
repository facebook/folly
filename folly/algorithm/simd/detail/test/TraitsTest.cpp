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

namespace folly::detail {

struct FollySimdTraitsTest : testing::Test {};

namespace simd_friendly_equivalent_test {

// ints
static_assert(
    std::is_same_v<std::int8_t, simd_friendly_equivalent_t<signed char>>);
static_assert(
    std::is_same_v<std::uint8_t, simd_friendly_equivalent_t<unsigned char>>);

static_assert(std::is_same_v<std::int16_t, simd_friendly_equivalent_t<short>>);
static_assert(
    std::is_same_v<std::uint16_t, simd_friendly_equivalent_t<unsigned short>>);

static_assert(std::is_same_v<std::int32_t, simd_friendly_equivalent_t<int>>);
static_assert(
    std::is_same_v<std::uint32_t, simd_friendly_equivalent_t<unsigned int>>);

static_assert(
    std::is_same_v<std::int64_t, simd_friendly_equivalent_t<std::int64_t>>);
static_assert(
    std::is_same_v<std::uint64_t, simd_friendly_equivalent_t<std::uint64_t>>);

// floats
static_assert(std::is_same_v<float, simd_friendly_equivalent_t<float>>);
static_assert(std::is_same_v<double, simd_friendly_equivalent_t<double>>);

// enum
enum SomeInt {};
enum class SomeIntClass : std::int32_t {};

static_assert(
    std::is_same_v<std::uint32_t, simd_friendly_equivalent_t<SomeInt>>);
static_assert(
    std::is_same_v<std::int32_t, simd_friendly_equivalent_t<SomeIntClass>>);

// const

static_assert(
    std::is_same_v<const std::int32_t, simd_friendly_equivalent_t<const int>>);

// sfinae
constexpr auto sfinae_call =
    []<typename T>(T) -> simd_friendly_equivalent_t<T> { return {}; };

static_assert(std::invocable<decltype(sfinae_call), int>);

struct NotSimdFriendly {};
static_assert(!std::invocable<decltype(sfinae_call), NotSimdFriendly>);

} // namespace simd_friendly_equivalent_test

namespace integral_simd_friendly_equivalent_test {

static_assert(std::is_same_v< //
              std::int8_t,
              integral_simd_friendly_equivalent<signed char>>);

struct Overloading {
  constexpr int operator()(auto) { return 0; }
  constexpr int operator()(has_simd_friendly_equivalent auto) { return 1; }
  constexpr int operator()(has_integral_simd_friendly_equivalent auto) {
    return 2;
  }
};

// Subsumption tests
struct NotSimdFriendly {};
enum class SomeInt {};

static_assert(Overloading{}(NotSimdFriendly{}) == 0);
static_assert(Overloading{}(float{}) == 1);
static_assert(Overloading{}(int{}) == 2);
static_assert(Overloading{}(SomeInt{}) == 2);

} // namespace integral_simd_friendly_equivalent_test

TEST_F(FollySimdTraitsTest, AsSimdFriendly) {
  enum SomeEnum : int { Foo = 1, Bar, Baz };

  static_assert(asSimdFriendly(SomeEnum::Foo) == 1);

  std::array arr{SomeEnum::Foo, SomeEnum::Bar, SomeEnum::Baz};
  folly::span<int, 3> castSpan = asSimdFriendly(folly::span(arr));
  ASSERT_THAT(castSpan, testing::ElementsAre(1, 2, 3));
}

template <typename T, typename U>
void isSameTest(const T&, const U&) = delete;

template <typename T>
void isSameTest(const T&, const T&) {}

template <typename From, typename To>
void asSimdFriendlyUintTypeTest() {
  isSameTest(asSimdFriendlyUint(From{}), To{});
  isSameTest(asSimdFriendlyUint(std::span<From>{}), std::span<To>{});
  isSameTest(
      asSimdFriendlyUint(std::span<const From>{}), std::span<const To>{});
}

TEST_F(FollySimdTraitsTest, AsSimdFriendlyUint) {
  enum SomeEnum : int { Foo = 1, Bar, Baz };

  static_assert(asSimdFriendlyUint(SomeEnum::Foo) == 1U);

  asSimdFriendlyUintTypeTest<char, std::uint8_t>();
  asSimdFriendlyUintTypeTest<short, std::uint16_t>();
  asSimdFriendlyUintTypeTest<int, std::uint32_t>();
  asSimdFriendlyUintTypeTest<unsigned, std::uint32_t>();
  asSimdFriendlyUintTypeTest<float, std::uint32_t>();
  asSimdFriendlyUintTypeTest<int64_t, std::uint64_t>();
  asSimdFriendlyUintTypeTest<double, std::uint64_t>();
}

} // namespace folly::detail
