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

#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

// IMPORTANT PRINCIPLES:
//   - For a static test to run, it must be added to `check_static_tests()`.
//   - Runtime test functions should be invoked one per gtest `TEST()`.
//   - When possible, the tests here are `static_assert` compile-time.
//     The goal is to ensure that the entire API is `constexpr`-correct.
//   - The tests are written against the `litetup` and `stdtup` facades to make
//     it clear where we deviate from the `std::tuple` interface.

FOLLY_PUSH_WARNING
FOLLY_DETAIL_LITE_TUPLE_ADJUST_WARNINGS

namespace folly::detail {

// It's important that `lite_tuple` remain aggregate for fast runtime & builds.
static_assert(std::is_aggregate_v<lite_tuple::tuple<>>);
static_assert(std::is_aggregate_v<lite_tuple::tuple<int, char>>);
static_assert(std::is_aggregate_v<lite_tuple::tuple<lite_tuple::tuple<int&&>>>);

// The value list type `vtag_t` from `Traits.h` (and `value_list_concat_t`) is
// fine, so it is not very compelling to use `lite_tuple::tuple` as a
// structural type -- but it currently is one, unlike `std::tuple`.
inline constexpr vtag_t<lite_tuple::tuple{1337, lite_tuple::tuple{'c'}}>
    lite_tuple_is_structural;

// Better UX than `assert()` in constexpr tests.
constexpr void test(bool ok) {
  if (!ok) {
    throw std::exception(); // Throwing in constexpr code is a compile error
  }
}

// These forwarding facades for the utility functions let us test `std` and
// `lite_tuple` with the same code without horrible macros.
struct stdtup {
  template <size_t Idx>
  static constexpr auto get = FOLLY_INVOKE_QUAL(std::get<Idx>);
  static constexpr auto apply = FOLLY_INVOKE_QUAL(std::apply);
  static constexpr auto forward_as_tuple =
      FOLLY_INVOKE_QUAL(std::forward_as_tuple);
  static constexpr auto tuple_cat = FOLLY_INVOKE_QUAL(std::tuple_cat);
};
struct litetup {
  template <size_t Idx>
  static constexpr auto get = FOLLY_INVOKE_QUAL(lite_tuple::get<Idx>);
  static constexpr auto apply = FOLLY_INVOKE_QUAL(lite_tuple::apply);
  static constexpr auto forward_as_tuple =
      FOLLY_INVOKE_QUAL(lite_tuple::forward_as_tuple);
  static constexpr auto tuple_cat = FOLLY_INVOKE_QUAL(lite_tuple::tuple_cat);
};

// Records ctor order, and confirms that dtor order the reverse
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct OrderTracker {
  int myN_;
  int& nRef_;
  constexpr /*implicit*/ OrderTracker(int& n) : myN_(n++), nRef_(n) {}
  constexpr ~OrderTracker() { test(myN_ == --nRef_); }
};

template <template <typename...> class Tup, typename TupFn>
struct TupleTests {
  // Also covers `std::tuple_size_v` and `std::tuple_element_t`
  static constexpr bool check_get_refs_and_values() {
    Tup e{};
    static_assert(0 == std::tuple_size_v<decltype(e)>);

    int n = 5;
    // NOLINTNEXTLINE(performance-move-const-arg)
    Tup<int, int&, int&&> t{n, n, std::move(n)};
    static_assert(3 == std::tuple_size_v<decltype(t)>);
    static_assert(std::is_same_v<int, std::tuple_element_t<0, decltype(t)>>);
    static_assert(std::is_same_v<int&, std::tuple_element_t<1, decltype(t)>>);
    static_assert(std::is_same_v<int&&, std::tuple_element_t<2, decltype(t)>>);

    TupFn{}.template get<1>(t) += 10;
    test(15 == TupFn{}.template get<2>(std::as_const(t))); // same address!
    test(5 == TupFn{}.template get<0>(std::move(t)));

    static_assert(std::is_same_v<int&, decltype(TupFn{}.template get<0>(t))>);
    static_assert(std::is_same_v<int&, decltype(TupFn{}.template get<1>(t))>);
    static_assert(std::is_same_v<int&, decltype(TupFn{}.template get<2>(t))>);

    static_assert(
        std::is_same_v<int&&, decltype(TupFn{}.template get<0>(std::move(t)))>);
    static_assert(
        std::is_same_v<int&, decltype(TupFn{}.template get<1>(std::move(t)))>);
    static_assert(
        std::is_same_v<int&&, decltype(TupFn{}.template get<2>(std::move(t)))>);

    return true;
  }

  static constexpr bool check_move_only() {
    Tup t{42, MoveOnly{}};
    // Drive-by check of empty type optimization
    static_assert(sizeof(int) == sizeof(t));
    auto m = TupFn{}.template get<1>(std::move(t));
    static_assert(std::is_same_v<decltype(m), MoveOnly>);
    return true;
  }

  static constexpr bool check_ctor_dtor_ordering() {
    int n = 0;
    {
      Tup<OrderTracker, OrderTracker, OrderTracker> t{n, n, n};
      test(3 == n);
      test(0 == TupFn{}.template get<0>(t).myN_);
      test(1 == TupFn{}.template get<1>(t).myN_);
      test(2 == TupFn{}.template get<2>(t).myN_);
    }
    test(0 == n);
    return true;
  }

  static constexpr bool check_apply() {
    Tup empty{};
    test(17 == TupFn{}.apply([]() { return 17; }, empty));

    Tup t{1, 2, 3};
    TupFn{}.apply([t](auto... n) { test(t == Tup{n...}); }, t);
    test(6 == TupFn{}.apply([](auto... n) { return (n + ...); }, t));
    TupFn{}.apply([](auto&... n) { return ((n *= n), ...); }, t);
    test(Tup{1, 4, 9} == t);

    return true;
  }

  static constexpr bool check_apply_move() {
    Tup moveT{MoveOnly{}, std::optional<MoveOnly>{}};
    auto m = TupFn{}.apply(
        [](auto&& m1, auto m2) {
          static_assert(std::is_same_v<decltype(m1), MoveOnly&&>);
          static_assert(std::is_same_v<decltype(m2), std::optional<MoveOnly>>);
          return std::move(m1);
        },
        std::move(moveT));
    static_assert(std::is_same_v<decltype(m), MoveOnly>);
    return true;
  }

  static constexpr bool check_apply_ref() {
    int n = 5;
    TupFn{}.apply(
        [](auto&& rref, auto&& lref) {
          static_assert(std::is_same_v<int&&, decltype(rref)>);
          static_assert(std::is_same_v<int&, decltype(lref)>);
        },
        // NOLINTNEXTLINE(performance-move-const-arg)
        // @lint-ignore CLANGTIDY facebook-hte-MoveEvaluationOrder
        TupFn{}.forward_as_tuple(std::move(n), n));
    return true;
  }

  static constexpr bool check_forward_as_tuple() {
    int x = 5;
    // NOLINTNEXTLINE(performance-move-const-arg)
    Tup t{x, std::move(x)};
    static_assert(std::is_same_v<decltype(t), Tup<int, int>>);

    int y = 5;
    // NOLINTNEXTLINE(performance-move-const-arg)
    // @lint-ignore CLANGTIDY facebook-hte-MoveEvaluationOrder
    auto ft = TupFn{}.forward_as_tuple(y, std::move(y));
    static_assert(std::is_same_v<decltype(ft), Tup<int&, int&&>>);
    return true;
  }

  static constexpr bool check_tuple_cat() {
    auto empty = TupFn{}.tuple_cat();
    static_assert(0 == std::tuple_size_v<decltype(empty)>);

    Tup a{2, 'a'};
    test(TupFn{}.tuple_cat(a) == a);
    test(TupFn{}.tuple_cat(empty, a) == a);
    test(TupFn{}.tuple_cat(a, empty) == a);

    Tup<Tup<>> b{};
    Tup a_b{2, 'a', Tup{}};
    test(TupFn{}.tuple_cat(a, b) == a_b);
    test(TupFn{}.tuple_cat(a, empty, b) == a_b);

    Tup<Tup<char>> c{{'c'}};
    Tup a_b_c{2, 'a', Tup{}, Tup{'c'}};
    test(TupFn{}.tuple_cat(a, b, c) == a_b_c);
    test(TupFn{}.tuple_cat(empty, a, empty, b, empty, c, empty) == a_b_c);

    // Identical types to ensure we correctly disambiguateby the "outer" base.
    Tup t1{1}, t2{2}, t3{3}, t4{4};
    test(TupFn{}.tuple_cat(t1, t2, t3, t4) == Tup{1, 2, 3, 4});

    return true;
  }

  static constexpr bool check_tuple_cat_move() {
    Tup t{MoveOnly{}};
    auto t2 = TupFn{}.tuple_cat(std::move(t));
    static_assert(std::is_same_v<decltype(t2), Tup<MoveOnly>>);
    return true;
  }

  static constexpr bool check_tuple_cat_const() {
    Tup t{2};
    test(TupFn{}.tuple_cat(std::as_const(t)) == t);
    return true;
  }

  static constexpr bool check_tuple_cat_refs() {
    int n = 5;
    { // lvalue ref only -- no `std::move`
      auto t = TupFn{}.forward_as_tuple(n);
      auto t2 = TupFn{}.tuple_cat(t);
      test(&TupFn{}.template get<0>(t) == &TupFn{}.template get<0>(t2));
      static_assert(std::is_same_v<decltype(t), decltype(t2)>);
    }
    { // adding an rvalue ref forces `std::move`
      // NOLINTNEXTLINE(performance-move-const-arg)
      // @lint-ignore CLANGTIDY facebook-hte-MoveEvaluationOrder
      auto t = TupFn{}.forward_as_tuple(std::move(n), n);
      // FIXME?: `lite_tuple` currently compiles without `std::move()` here,
      // which is not ideal -- it'd be better to require it for rvalue refs.
      auto t2 = TupFn{}.tuple_cat(std::move(t));
      test(&TupFn{}.template get<0>(t) == &TupFn{}.template get<0>(t2));
      test(&TupFn{}.template get<1>(t) == &TupFn{}.template get<1>(t2));
      static_assert(std::is_same_v<decltype(t), decltype(t2)>);
    }
    return true;
  }

  static constexpr bool check_structured_binding() {
    Tup t{'a', 7};
    auto [a, seven] = t;
    static_assert(std::is_same_v<decltype(a), char>);
    test(a == 'a');
    static_assert(std::is_same_v<decltype(seven), int>);
    test(seven == 7);
    return true;
  }

  static constexpr bool check_static_tests() {
    constexpr bool is_lite = std::is_same_v<TupFn, litetup>;
    static_assert(check_get_refs_and_values());
    static_assert(check_move_only());
    // `std::tuple` has unspecified ordering, which as of 2025 differs between
    // `libc++` and `libstdc++` :'( :'(
    if constexpr (is_lite) {
      static_assert(check_ctor_dtor_ordering());
    }
    static_assert(check_apply());
    static_assert(check_apply_move());
    static_assert(check_apply_ref());
    static_assert(check_forward_as_tuple());
    static_assert(check_tuple_cat());
    static_assert(check_tuple_cat_move());
    static_assert(check_tuple_cat_const());
    static_assert(check_tuple_cat_refs());
    static_assert(check_structured_binding());
    return true;
  }

  // Dynamic tests

  static void check_move_only_unique_ptr() {
    Tup t{std::make_unique<int>(7)};
    auto p = TupFn{}.template get<0>(std::move(t));
    EXPECT_EQ(7, *p);
    EXPECT_EQ(nullptr, TupFn{}.template get<0>(t).get());
  }

  static void check_apply_move_unique_ptr() {
    Tup moveT{std::make_unique<int>(7), MoveOnly{}};
    auto p = TupFn{}.apply(
        [](auto&& p, auto m) {
          static_assert(std::is_same_v<decltype(m), MoveOnly>);
          return std::move(p);
        },
        std::move(moveT));
    EXPECT_EQ(7, *p);
    EXPECT_EQ(nullptr, TupFn{}.template get<0>(moveT).get());
  }

  static constexpr void check_tuple_cat_move_unique_ptr() {
    Tup t{std::make_unique<int>(2)};
    auto t2 = TupFn{}.tuple_cat(std::move(t));
    static_assert(std::is_same_v<decltype(t2), Tup<std::unique_ptr<int>>>);
    EXPECT_EQ(2, *TupFn{}.template get<0>(t2));
    EXPECT_EQ(nullptr, TupFn{}.template get<0>(t));
  }
};

using StdTests = TupleTests<std::tuple, stdtup>;
using LiteTests = TupleTests<lite_tuple::tuple, litetup>;

static_assert(StdTests::check_static_tests());
static_assert(LiteTests::check_static_tests());

TEST(LiteTupleTest, std_move_only_unique_ptr) {
  StdTests::check_move_only_unique_ptr();
}
TEST(LiteTupleTest, lite_move_only_unique_ptr) {
  LiteTests::check_move_only_unique_ptr();
}

TEST(LiteTupleTest, std_apply_move_unique_ptr) {
  StdTests::check_apply_move_unique_ptr();
}
TEST(LiteTupleTest, lite_apply_move_unique_ptr) {
  LiteTests::check_apply_move_unique_ptr();
}

TEST(LiteTupleTest, std_tuple_cat_move_unique_ptr) {
  StdTests::check_tuple_cat_move_unique_ptr();
}
TEST(LiteTupleTest, lite_tuple_cat_move_unique_ptr) {
  LiteTests::check_tuple_cat_move_unique_ptr();
}

constexpr bool check_lite_tuple_reverse_apply() { // no `std::reverse_apply`
  lite_tuple::tuple empty{};
  test(17 == lite_tuple::reverse_apply([]() { return 17; }, empty));

  lite_tuple::tuple t{1, 2, 3};
  lite_tuple::reverse_apply(
      [](auto... n) {
        test(lite_tuple::tuple{3, 2, 1} == lite_tuple::tuple{n...});
      },
      t);
  lite_tuple::reverse_apply([](auto&... n) { return ((n *= n), ...); }, t);
  test(lite_tuple::tuple{1, 4, 9} == t);

  lite_tuple::tuple moveT{MoveOnly{}, std::optional<MoveOnly>{}};
  auto m = lite_tuple::reverse_apply(
      [](auto&& m1, auto m2) {
        static_assert(std::is_same_v<decltype(m1), std::optional<MoveOnly>&&>);
        static_assert(std::is_same_v<decltype(m2), MoveOnly>);
        return std::move(m1);
      },
      std::move(moveT));
  static_assert(std::is_same_v<decltype(m), std::optional<MoveOnly>>);

  int n = 5;
  lite_tuple::reverse_apply(
      [](auto&& lref, auto&& rref) {
        static_assert(std::is_same_v<int&&, decltype(rref)>);
        static_assert(std::is_same_v<int&, decltype(lref)>);
      },
      // NOLINTNEXTLINE(performance-move-const-arg)
      // @lint-ignore CLANGTIDY facebook-hte-MoveEvaluationOrder
      lite_tuple::forward_as_tuple(std::move(n), n));

  return true;
}

static_assert(check_lite_tuple_reverse_apply());

TEST(LiteTupleTest, lite_tuple_reverse_apply) { // no `std::reverse_apply`
  lite_tuple::tuple moveT{std::make_unique<int>(7), MoveOnly{}};
  auto p = lite_tuple::reverse_apply(
      [](auto m, auto&& p) {
        static_assert(std::is_same_v<decltype(m), MoveOnly>);
        return std::move(p);
      },
      std::move(moveT));
  EXPECT_EQ(7, *p);
  EXPECT_EQ(nullptr, lite_tuple::get<0>(moveT).get());
}

} // namespace folly::detail

FOLLY_POP_WARNING
