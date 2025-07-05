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

#include <folly/coro/safe/SafeAlias.h>
#include <folly/detail/tuple.h>
#include <folly/portability/GTest.h>

using namespace folly;

using SA = safe_alias;

TEST(SafeAliasTest, basics) {
  class X {};
  static_assert(safe_alias_of_v<X> == SA::maybe_value);
  static_assert(safe_alias_of_v<int> == SA::maybe_value);
  static_assert(safe_alias_of_v<int*> == SA::unsafe);
  static_assert(safe_alias_of_v<const int*> == SA::unsafe);
  static_assert(safe_alias_of_v<int&> == SA::unsafe);
  static_assert(safe_alias_of_v<const int&&> == SA::unsafe);
  static_assert(safe_alias_of_v<std::reference_wrapper<int>> == SA::unsafe);
  static_assert(
      safe_alias_of_v<const std::reference_wrapper<int>> == SA::unsafe);
  static_assert(
      safe_alias_of_v<const std::reference_wrapper<int>&> == SA::unsafe);
  static_assert(
      safe_alias_of_v<folly::rvalue_reference_wrapper<X>> == SA::unsafe);
}

template <template <typename...> class Tuple>
constexpr bool checkTuple() {
  class X {};
  static_assert(safe_alias_of_v<Tuple<X, int>> == SA::maybe_value);
  static_assert(safe_alias_of_v<Tuple<X&, int>> == SA::unsafe);
  static_assert(safe_alias_of_v<Tuple<X, int*>> == SA::unsafe);
  return true;
}

static_assert(checkTuple<std::tuple>());
static_assert(checkTuple<folly::detail::lite_tuple::tuple>());

TEST(SafeAliasTest, safety_ordering) {
  class X {};
  static_assert(safe_alias_of_v<std::tuple<X, int>> == SA::maybe_value);
  static_assert(
      safe_alias_of_v<
          std::
              tuple<X, int, manual_safe_ref_t<SA::co_cleanup_safe_ref, int>>> ==
      SA::co_cleanup_safe_ref);
  static_assert(
      safe_alias_of_v<std::tuple<
          X,
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, int>,
          manual_safe_ref_t<SA::co_cleanup_safe_ref, int>>> ==
      SA::after_cleanup_ref);
  static_assert(
      safe_alias_of_v<std::tuple<
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, int>,
          manual_safe_ref_t<SA::shared_cleanup, int>>> == SA::shared_cleanup);
  static_assert(
      safe_alias_of_v<std::tuple<
          X,
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, bool>,
          int&>> == SA::unsafe);
}

TEST(SafeAliasTest, pair) {
  class X {};
  static_assert(safe_alias_of_v<std::pair<X, int>> == SA::maybe_value);
  static_assert(safe_alias_of_v<std::pair<X&, int>> == SA::unsafe);
  static_assert(safe_alias_of_v<std::pair<X, int*>> == SA::unsafe);
}

TEST(SafeAliasTest, vector) {
  class X {};
  static_assert(safe_alias_of_v<std::vector<X>> == SA::maybe_value);
  static_assert(safe_alias_of_v<std::vector<int>> == SA::maybe_value);
  static_assert(safe_alias_of_v<std::vector<X*>> == SA::unsafe);
  static_assert(safe_alias_of_v<std::vector<int*>> == SA::unsafe);
}

TEST(SafeAliasTest, manual_safe_ref) {
  int* x = nullptr;
  auto r1 = manual_safe_ref(x);
  auto r2 = manual_safe_ref<safe_alias::co_cleanup_safe_ref>(x);
  auto r3 = manual_safe_ref<safe_alias::after_cleanup_ref>(x);
  static_assert(safe_alias_of_v<decltype(x)> == SA::unsafe);
  static_assert(safe_alias_of_v<decltype(r1)> == SA::maybe_value);
  static_assert(safe_alias_of_v<decltype(r2)> == SA::co_cleanup_safe_ref);
  static_assert(safe_alias_of_v<decltype(r3)> == SA::after_cleanup_ref);
}

TEST(SafeAliasTest, manual_safe_val) {
  auto n = std::make_unique<int>(17);
  int* x = n.get();
  auto r1 = manual_safe_val(x);
  auto r2 = manual_safe_val<safe_alias::co_cleanup_safe_ref>(x);
  auto r3 = manual_safe_with<safe_alias::after_cleanup_ref>([&]() {
    return std::move(n);
  });
  static_assert(safe_alias_of_v<decltype(x)> == SA::unsafe);
  static_assert(safe_alias_of_v<decltype(r1)> == SA::maybe_value);
  static_assert(safe_alias_of_v<decltype(r2)> == SA::co_cleanup_safe_ref);
  static_assert(safe_alias_of_v<decltype(r3)> == SA::after_cleanup_ref);

  EXPECT_EQ(r1.get(), std::as_const(r2).get());
  // Test `get` on move-only `r3` to check for accidental copies
  EXPECT_EQ(std::move(r3).get().get(), r2.get());
}
