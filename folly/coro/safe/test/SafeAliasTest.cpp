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

#include <folly/detail/tuple.h>
#include <folly/lang/SafeAlias.h>
#include <folly/portability/GTest.h>

using namespace folly;

using SA = safe_alias;

class Empty {};
class NonEmpty {
  int x;
};

template <typename T, SA Lenient, SA Strict = Lenient>
constexpr void check_safe_alias() {
  static_assert(lenient_safe_alias_of_v<T> == Lenient);
  static_assert(strict_safe_alias_of_v<T> == Strict);
}

TEST(SafeAliasTest, basics) {
  check_safe_alias<Empty, SA::maybe_value>();
  check_safe_alias<NonEmpty, SA::maybe_value, SA::unsafe>();
  check_safe_alias<void, SA::maybe_value>();
  check_safe_alias<int, SA::maybe_value>();
  check_safe_alias<int*, SA::unsafe>();
  check_safe_alias<const int*, SA::unsafe>();
  check_safe_alias<int&, SA::unsafe>();
  check_safe_alias<const int&&, SA::unsafe>();
  check_safe_alias<std::reference_wrapper<int>, SA::unsafe>();
  check_safe_alias<const std::reference_wrapper<int>, SA::unsafe>();
  check_safe_alias<const std::reference_wrapper<int>&, SA::unsafe>();
  check_safe_alias<folly::rvalue_reference_wrapper<Empty>, SA::unsafe>();

  // Function pointers
  check_safe_alias<int (*)(), SA::maybe_value>();
  check_safe_alias<int (*)(int, char), SA::maybe_value>();
  check_safe_alias<void (*)(int&), SA::maybe_value>();
  check_safe_alias<int (*)() noexcept, SA::maybe_value>();
  check_safe_alias<int (*)(int, char) noexcept, SA::maybe_value>();
  check_safe_alias<void (*)(int&) noexcept, SA::maybe_value>();
}

template <template <typename...> class Tuple>
constexpr bool checkTuple() {
  check_safe_alias<Tuple<Empty, int>, SA::maybe_value>();
  check_safe_alias<Tuple<NonEmpty, int>, SA::maybe_value, SA::unsafe>();
  check_safe_alias<Tuple<Empty&, int>, SA::unsafe>();
  check_safe_alias<Tuple<Empty, int*>, SA::unsafe>();
  return true;
}

static_assert(checkTuple<std::tuple>());
static_assert(checkTuple<folly::detail::lite_tuple::tuple>());

TEST(SafeAliasTest, safety_ordering) {
  check_safe_alias<std::tuple<Empty, int>, SA::maybe_value>();
  check_safe_alias<std::tuple<NonEmpty, int>, SA::maybe_value, SA::unsafe>();
  check_safe_alias<
      std::tuple<Empty, int, manual_safe_ref_t<SA::co_cleanup_safe_ref, int>>,
      SA::co_cleanup_safe_ref>();
  check_safe_alias<
      std::tuple<
          Empty,
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, int>,
          manual_safe_ref_t<SA::co_cleanup_safe_ref, int>>,
      SA::after_cleanup_ref>();
  check_safe_alias<
      std::tuple<
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, int>,
          manual_safe_ref_t<SA::shared_cleanup, int>>,
      SA::shared_cleanup>();
  check_safe_alias<
      std::tuple<
          Empty,
          int,
          manual_safe_ref_t<SA::after_cleanup_ref, bool>,
          int&>,
      SA::unsafe>();
}

TEST(SafeAliasTest, pair) {
  check_safe_alias<std::pair<Empty, int>, SA::maybe_value>();
  check_safe_alias<std::pair<NonEmpty, int>, SA::maybe_value, SA::unsafe>();
  check_safe_alias<std::pair<Empty&, int>, SA::unsafe>();
  check_safe_alias<std::pair<Empty, int*>, SA::unsafe>();
}

TEST(SafeAliasTest, vector) {
  check_safe_alias<std::vector<Empty>, SA::maybe_value>();
  check_safe_alias<std::vector<NonEmpty>, SA::maybe_value, SA::unsafe>();
  check_safe_alias<std::vector<int>, SA::maybe_value>();
  check_safe_alias<std::vector<Empty*>, SA::unsafe>();
  check_safe_alias<std::vector<int*>, SA::unsafe>();
}

TEST(SafeAliasTest, manual_safe_ref) {
  int* x = nullptr;
  auto r1 = manual_safe_ref(x);
  auto r2 = manual_safe_ref<safe_alias::co_cleanup_safe_ref>(x);
  auto r3 = manual_safe_ref<safe_alias::after_cleanup_ref>(x);
  check_safe_alias<decltype(x), SA::unsafe>();
  check_safe_alias<decltype(r1), SA::maybe_value>();
  check_safe_alias<decltype(r2), SA::co_cleanup_safe_ref>();
  check_safe_alias<decltype(r3), SA::after_cleanup_ref>();
}

TEST(SafeAliasTest, manual_safe_val) {
  auto n = std::make_unique<int>(17);
  int* x = n.get();
  auto r1 = manual_safe_val(x);
  auto r2 = manual_safe_val<safe_alias::co_cleanup_safe_ref>(x);
  auto r3 = manual_safe_with<safe_alias::after_cleanup_ref>([&]() {
    return std::move(n);
  });
  check_safe_alias<decltype(x), SA::unsafe>();
  check_safe_alias<decltype(r1), SA::maybe_value>();
  check_safe_alias<decltype(r2), SA::co_cleanup_safe_ref>();
  check_safe_alias<decltype(r3), SA::after_cleanup_ref>();

  EXPECT_EQ(r1.get(), std::as_const(r2).get());
  // Test `get` on move-only `r3` to check for accidental copies
  EXPECT_EQ(std::move(r3).get().get(), r2.get());
}

TEST(SafeAliasTest, manual_safe_callable_safe_alias) {
  auto c1 = manual_safe_callable([](int x) { return x * 2; });
  auto c2 = manual_safe_callable<safe_alias::co_cleanup_safe_ref>([](int x) {
    return x * 3;
  });
  auto c3 = manual_safe_callable<safe_alias::after_cleanup_ref>([](int x) {
    return x * 4;
  });

  check_safe_alias<decltype(c1), SA::maybe_value>();
  check_safe_alias<decltype(c2), SA::co_cleanup_safe_ref>();
  check_safe_alias<decltype(c3), SA::after_cleanup_ref>();
}

TEST(SafeAliasTest, manual_safe_callable_invoke) {
  auto c1 = manual_safe_callable([](int x) { return x * 2; });
  auto c2 = manual_safe_callable([](int x) { return x * 3; });
  auto c3 = manual_safe_callable([](int x) { return x * 4; });

  EXPECT_EQ(c1(5), 10);
  EXPECT_EQ(c2(5), 15);
  EXPECT_EQ(c3(5), 20);

  auto forward_fn = manual_safe_callable([](int& x, const int& y, int&& z) {
    return x + y + z;
  });
  int a = 1;
  const int b = 2;
  EXPECT_EQ(forward_fn(a, b, 3), 6);
}

TEST(SafeAliasTest, manual_safe_callable_return_type) {
  auto value_fn = manual_safe_callable([](int x) -> int { return x + 1; });
  static_assert(std::is_same_v<decltype(value_fn(1)), int>);
  EXPECT_EQ(value_fn(42), 43);

  int storage = 100;
  auto ref_fn = manual_safe_callable([&storage](int x) -> int& {
    storage = x;
    return storage;
  });
  static_assert(std::is_same_v<decltype(ref_fn(1)), int&>);
  int& result_ref = ref_fn(200);
  EXPECT_EQ(result_ref, 200);
  EXPECT_EQ(&result_ref, &storage);
}

TEST(SafeAliasTest, manual_safe_callable_noexcept) {
  auto noexcept_fn = manual_safe_callable([](int x) noexcept -> int {
    return x * 2;
  });
  auto throwing_fn = manual_safe_callable([](int x) -> int { return x * 2; });

  static_assert(noexcept(noexcept_fn(1)));
  static_assert(!noexcept(throwing_fn(1)));

  EXPECT_EQ(noexcept_fn(21), 42);
  EXPECT_EQ(throwing_fn(21), 42);
}

TEST(SafeAliasTest, manual_safe_callable_qualifiers) {
  struct TestCallable {
    int operator()(int x) & { return x + 1; }
    int operator()(int x) const& { return x + 2; }
    int operator()(int x) && { return x + 3; }
  };

  auto wrapped = manual_safe_callable(TestCallable{});
  EXPECT_EQ(wrapped(10), 11);
  EXPECT_EQ(std::as_const(wrapped)(10), 12);
  EXPECT_EQ(std::move(wrapped)(10), 13);
}
