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

#include <folly/portability/GTest.h>
#include <folly/result/try.h>

#if FOLLY_HAS_RESULT

namespace folly {

struct ThrowingMove {
  ThrowingMove(const ThrowingMove&) = default;
  ThrowingMove& operator=(const ThrowingMove&) = default;
  ThrowingMove(ThrowingMove&&) {}
  ThrowingMove& operator=(ThrowingMove&&) { return *this; }
};
struct NothrowMove {};

static_assert(noexcept(result_to_try(FOLLY_DECLVAL(result<int>))));
static_assert(noexcept(result_to_try(FOLLY_DECLVAL(result<NothrowMove>))));
static_assert(!noexcept(result_to_try(FOLLY_DECLVAL(result<ThrowingMove>))));

static_assert(noexcept(try_to_result(FOLLY_DECLVAL(Try<int>))));
static_assert(noexcept(try_to_result(FOLLY_DECLVAL(Try<NothrowMove>))));
static_assert(!noexcept(try_to_result(FOLLY_DECLVAL(Try<ThrowingMove>))));

TEST(ResultTry, result_to_try) {
  auto tInt = result_to_try(result<int>{5});
  static_assert(std::is_same_v<Try<int>, decltype(tInt)>);
  EXPECT_EQ(5, *tInt);

  auto tErr =
      result_to_try(result<int>{non_value_result{std::runtime_error{"foo"}}});
  static_assert(std::is_same_v<Try<int>, decltype(tErr)>);
  EXPECT_STREQ("foo", tErr.tryGetExceptionObject<std::runtime_error>()->what());

  // `void` -> `void` is a special case
  auto tVoid = result_to_try(result<void>{});
  static_assert(std::is_same_v<Try<void>, decltype(tVoid)>);
  EXPECT_TRUE(tVoid.hasValue());
}

TEST(ResultTry, nonempty_try_to_result) {
  auto rInt = try_to_result(Try<int>{5});
  static_assert(std::is_same_v<result<int>, decltype(rInt)>);
  EXPECT_EQ(5, rInt.value_or_throw());

  auto rErr = try_to_result(
      Try<int>{make_exception_wrapper<std::runtime_error>("foo")});
  static_assert(std::is_same_v<result<int>, decltype(rErr)>);
  EXPECT_STREQ("foo", get_exception<std::runtime_error>(rErr)->what());

  // `void` -> `void` is a special case
  auto rVoid = try_to_result(Try<void>{});
  static_assert(std::is_same_v<result<void>, decltype(rVoid)>);
  EXPECT_TRUE(rVoid.has_value());
}

TEST(ResultTry, empty_try_to_result_error) {
  auto rErr = try_to_result(Try<int>{});
  static_assert(std::is_same_v<result<int>, decltype(rErr)>);
  EXPECT_NE(nullptr, get_exception<UsingUninitializedTry>(rErr));
}

TEST(ResultTry, empty_try_to_result_default) {
  auto rInt = try_to_result(Try<int>{}, empty_try_with{[]() { return 1337; }});
  static_assert(std::is_same_v<result<int>, decltype(rInt)>);
  EXPECT_EQ(1337, rInt.value_or_throw());

  auto rErr = try_to_result(
      Try<int>{}, empty_try_with{[]() {
        return non_value_result{std::runtime_error{"baz"}};
      }});
  static_assert(std::is_same_v<result<int>, decltype(rErr)>);
  EXPECT_STREQ("baz", get_exception<std::runtime_error>(rErr)->what());
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
