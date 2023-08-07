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

#include <folly/Utility.h>

#include <type_traits>

#include <folly/lang/Keep.h>
#include <folly/portability/GTest.h>

namespace folly {

extern "C" FOLLY_KEEP int check_unsafe_default_initialized_int_ret() {
  int a = folly::unsafe_default_initialized;
  return a;
}

extern "C" FOLLY_KEEP void check_unsafe_default_initialized_int_set(int* p) {
  int a = folly::unsafe_default_initialized;
  *p = a;
}

extern "C" FOLLY_KEEP void check_unsafe_default_initialized_int_pass() {
  int a = folly::unsafe_default_initialized;
  folly::detail::keep_sink_nx(a);
}

extern "C" FOLLY_KEEP int check_unsafe_default_initialized_int_cexpr() {
  constexpr int a = folly::unsafe_default_initialized;
  return a;
}

// for ADL test below.
template <class T>
int square(const T& x) {
  return x * x;
}

} // namespace folly

class UtilityTest : public testing::Test {};

namespace my_type {

struct MoveInt : folly::MoveOnly {
  int x = 0;
};

struct NoMoveInt : folly::NonCopyableNonMovable {
  int x = 0;
};

template <class M>
int square(const M& m) {
  return m.x * m.x;
}

} // namespace my_type

TEST_F(UtilityTest, NonCopyableAggregateInit) {
  // Ensure classes inheriting from folly::MoveOnly, etc, do not find all of
  // folly::* by ADL.
  EXPECT_EQ(16, square(my_type::MoveInt{.x = 4}));
  EXPECT_EQ(25, square(my_type::NoMoveInt{.x = 5}));
  using folly::square;
  EXPECT_EQ(36, square(6));
  // Ambiguous: EXPECT_EQ(16, square(my_type::MoveInt{.x = 4}));
}

// Tests for FOLLY_DECLVAL macro:

static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int)), int>);
static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int&)), int&>);
static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int&&)), int&&>);
static_assert(noexcept(FOLLY_DECLVAL(int)));

// Tests for folly::decay_t:

template <typename T>
using dec = folly::decay_t<T>;
struct incomplete;
struct abstract {
  virtual ~abstract() = 0;
};
struct immobile {
  immobile() = delete;
  immobile(immobile&&) = delete;
  void operator=(immobile&&) = delete;
  ~immobile() = delete;
};
static_assert(std::is_same_v<int, dec<int>>);
static_assert(std::is_same_v<int, dec<int&>>);
static_assert(std::is_same_v<int, dec<int&&>>);
static_assert(std::is_same_v<int, dec<int const>>);
static_assert(std::is_same_v<int, dec<int const&&>>);
static_assert(std::is_same_v<int, dec<int const&>>);
static_assert(std::is_same_v<int, dec<int volatile>>);
static_assert(std::is_same_v<int, dec<int volatile&>>);
static_assert(std::is_same_v<int, dec<int volatile&&>>);
static_assert(std::is_same_v<int, dec<int const volatile>>);
static_assert(std::is_same_v<int, dec<int const volatile&>>);
static_assert(std::is_same_v<int, dec<int const volatile&&>>);
static_assert(std::is_same_v<int*, dec<int*>>);
static_assert(std::is_same_v<int*, dec<int[]>>);
static_assert(std::is_same_v<int*, dec<int[7]>>);
static_assert(std::is_same_v<int*, dec<int*&>>);
static_assert(std::is_same_v<int*, dec<int (&)[]>>);
static_assert(std::is_same_v<int*, dec<int (&)[7]>>);
static_assert(std::is_same_v<int const*, dec<int const*>>);
static_assert(std::is_same_v<int const*, dec<int const[]>>);
static_assert(std::is_same_v<int const*, dec<int const[7]>>);
static_assert(std::is_same_v<int const*, dec<int const*&>>);
static_assert(std::is_same_v<int const*, dec<int const (&)[]>>);
static_assert(std::is_same_v<int const*, dec<int const (&)[7]>>);
static_assert(std::is_same_v<int (*)(), dec<int (*)()>>);
static_assert(std::is_same_v<int (*)() noexcept, dec<int (*)() noexcept>>);
static_assert(std::is_same_v<int (*)(), dec<int (&)()>>);
static_assert(std::is_same_v<int (*)() noexcept, dec<int (&)() noexcept>>);
static_assert(std::is_same_v<void, dec<void>>);
static_assert(std::is_same_v<void, dec<void const>>);
static_assert(std::is_same_v<void, dec<void volatile>>);
static_assert(std::is_same_v<void, dec<void const volatile>>);
static_assert(std::is_same_v<incomplete, dec<incomplete>>);
static_assert(std::is_same_v<incomplete, dec<incomplete const>>);
static_assert(std::is_same_v<abstract, dec<abstract>>);
static_assert(std::is_same_v<abstract, dec<abstract const>>);
static_assert(std::is_same_v<immobile, dec<immobile>>);
static_assert(std::is_same_v<immobile, dec<immobile const>>);

TEST_F(UtilityTest, copy) {
  struct MyData {};
  struct Worker {
    size_t rrefs = 0, crefs = 0;
    void something(MyData&&) { ++rrefs; }
    void something(const MyData&) { ++crefs; }
  };

  MyData data;
  Worker worker;
  worker.something(folly::copy(data));
  worker.something(std::move(data));
  worker.something(data);
  EXPECT_EQ(2, worker.rrefs);
  EXPECT_EQ(1, worker.crefs);
}

TEST_F(UtilityTest, copy_noexcept_spec) {
  struct MyNoexceptCopyable {};
  MyNoexceptCopyable noe;
  EXPECT_TRUE(noexcept(folly::copy(noe)));
  EXPECT_TRUE(noexcept(folly::copy(std::move(noe))));

  struct MyThrowingCopyable {
    MyThrowingCopyable() {}
    MyThrowingCopyable(const MyThrowingCopyable&) noexcept(false) {}
    MyThrowingCopyable(MyThrowingCopyable&&) = default;
  };
  MyThrowingCopyable thr;
  EXPECT_FALSE(noexcept(folly::copy(thr)));
  EXPECT_TRUE(noexcept(folly::copy(std::move(thr)))); // note: does not copy
}

TEST_F(UtilityTest, as_const) {
  struct S {
    bool member() { return false; }
    bool member() const { return true; }
  };
  S s;
  EXPECT_FALSE(s.member());
  EXPECT_TRUE(folly::as_const(s).member());
  EXPECT_EQ(&s, &folly::as_const(s));
  EXPECT_TRUE(noexcept(folly::as_const(s)));
}

template <typename T>
static T& as_mutable(T const& t) {
  return const_cast<T&>(t);
}

TEST_F(UtilityTest, forward_like) {
  int x = 0;
  // just show that it may be invoked, and that it is purely a cast
  // the real work is done by like_t, in terms of which forward_like is defined
  EXPECT_EQ(&x, std::addressof(folly::forward_like<char&>(x)));
  EXPECT_EQ(&x, std::addressof(as_mutable(folly::forward_like<char const>(x))));

  // Should not be able to turn rvalues into lvalues
  // Uncomment to produce expected compile-time errors
  // std::forward<const int&>(1);
  // folly::forward_like<const int&>(1);
}

TEST_F(UtilityTest, inheritable) {
  struct foo : folly::index_constant<47> {};
  struct bar final : folly::index_constant<89> {};
  using ifoo = folly::detail::inheritable<foo>;
  using ibar = folly::detail::inheritable<bar>;
  EXPECT_TRUE(std::is_empty_v<ifoo>);
  EXPECT_FALSE(std::is_empty_v<ibar>);
  struct tester : ifoo, ibar {};
  EXPECT_EQ(47, static_cast<foo const&>(tester{}));
  EXPECT_EQ(89, static_cast<bar const&>(tester{}));
  EXPECT_EQ(47, static_cast<foo const&>(folly::copy(tester{})));
  EXPECT_EQ(89, static_cast<bar const&>(folly::copy(tester{})));
}

template <bool Copy, bool Move>
class TestCopyMove {
  using T = folly::moveonly_::EnableCopyMove<Copy, Move>;
  static_assert(std::is_copy_constructible_v<T> == Copy);

  // Allowing copy but disallowing move is not effective from a base class;
  // calls to move the child class will just resolve to copy.
  static_assert(std::is_move_constructible_v<T> == (Move || Copy));
};

template class TestCopyMove<false, false>;
template class TestCopyMove<false, true>;
template class TestCopyMove<true, false>;
template class TestCopyMove<true, true>;

TEST_F(UtilityTest, MoveOnly) {
  class FooBar : folly::MoveOnly {
    int a;
  };

  static_assert(
      !std::is_copy_constructible<FooBar>::value,
      "Should not be copy constructible");

  // Test that move actually works.
  FooBar foobar;
  FooBar foobar2(std::move(foobar));
  (void)foobar2;

  // Test that inheriting from MoveOnly doesn't prevent the move
  // constructor from being noexcept.
  static_assert(
      std::is_nothrow_move_constructible<FooBar>::value,
      "Should have noexcept move constructor");
}

TEST_F(UtilityTest, NonCopyableNonMovable) {
  class FooBar : folly::NonCopyableNonMovable {
    int a;
  };

  static_assert(
      !std::is_copy_constructible<FooBar>::value,
      "Should not be copy constructible");
  static_assert(
      !std::is_move_constructible<FooBar>::value,
      "Should not be move constructible");
}

TEST_F(UtilityTest, to_signed) {
  {
    constexpr auto actual = folly::to_signed(int32_t(-12));
    EXPECT_TRUE(std::is_signed<decltype(actual)>::value);
    EXPECT_EQ(-12, actual);
  }
  {
    constexpr auto actual = folly::to_signed(uint32_t(-12));
    EXPECT_TRUE(std::is_signed<decltype(actual)>::value);
    EXPECT_EQ(-12, actual);
  }
}

TEST_F(UtilityTest, to_unsigned) {
  {
    constexpr auto actual = folly::to_unsigned(int32_t(-12));
    EXPECT_TRUE(!std::is_signed<decltype(actual)>::value);
    EXPECT_EQ(-12, actual);
  }
  {
    constexpr auto actual = folly::to_unsigned(uint32_t(-12));
    EXPECT_TRUE(!std::is_signed<decltype(actual)>::value);
    EXPECT_EQ(-12, actual);
  }
}

TEST_F(UtilityTest, to_narrow) {
  {
    constexpr uint32_t actual = folly::to_narrow(uint64_t(100));
    EXPECT_EQ(100, actual);
  }
}

TEST_F(UtilityTest, to_integral) {
  {
    constexpr uint32_t actual = folly::to_integral(100.0f);
    EXPECT_EQ(100, actual);
  }
}

TEST_F(UtilityTest, to_floating_point) {
  {
    constexpr float actual = folly::to_floating_point(100);
    EXPECT_EQ(100.f, actual);
  }
}

TEST_F(UtilityTest, invocable_to_basic) {
  struct foo {
    int value = 0;
    explicit foo(int value_) noexcept : value{value_} {}
    foo(foo const&) = delete;
    void operator=(foo const&) = delete;
  };
  foo f = folly::invocable_to([] { return foo(17); });
  EXPECT_EQ(17, f.value);
}

namespace folly::detail::invocable_to_test {

template <typename T, bool C, bool R, bool X>
struct fun;
template <typename T, bool X>
struct fun<T, 0, 0, X> {
  constexpr T operator()() & noexcept(X);
};
template <typename T, bool X>
struct fun<T, 0, 1, X> {
  constexpr T operator()() && noexcept(X);
};
template <typename T, bool X>
struct fun<T, 1, 0, X> {
  constexpr T operator()() const& noexcept(X);
};
template <typename T, bool X>
struct fun<T, 1, 1, X> {
  constexpr T operator()() const&& noexcept(X);
};

template <typename T, bool C, bool R, bool X>
using of = std::invoke_result_t<folly::invocable_to_fn, fun<T, C, R, X>>;

template <typename S, typename D>
static constexpr bool is_conv_v = std::is_convertible_v<S, D>;

template <typename S, typename D>
static constexpr bool is_nx_conv_v = //
    is_conv_v<S, D>&& std::is_nothrow_constructible_v<D, S>;

static_assert(is_conv_v<of<int, 0, 0, 0>&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 0> const&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 0>&&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 0> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 0>&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 0> const&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 0>&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 0> const&&, int>);

static_assert(is_conv_v<of<int, 0, 0, 1>&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 1> const&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 1>&&, int>);
static_assert(!is_conv_v<of<int, 0, 0, 1> const&&, int>);
static_assert(is_nx_conv_v<of<int, 0, 0, 1>&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 1> const&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 1>&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 0, 1> const&&, int>);

static_assert(!is_conv_v<of<int, 0, 1, 0>&, int>);
static_assert(!is_conv_v<of<int, 0, 1, 0> const&, int>);
static_assert(is_conv_v<of<int, 0, 1, 0>&&, int>);
static_assert(!is_conv_v<of<int, 0, 1, 0> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 0>&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 0> const&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 0>&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 0> const&&, int>);

static_assert(!is_conv_v<of<int, 0, 1, 1>&, int>);
static_assert(!is_conv_v<of<int, 0, 1, 1> const&, int>);
static_assert(is_conv_v<of<int, 0, 1, 1>&&, int>);
static_assert(!is_conv_v<of<int, 0, 1, 1> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 1>&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 1> const&, int>);
static_assert(is_nx_conv_v<of<int, 0, 1, 1>&&, int>);
static_assert(!is_nx_conv_v<of<int, 0, 1, 1> const&&, int>);

static_assert(is_conv_v<of<int, 1, 0, 0>&, int>);
static_assert(is_conv_v<of<int, 1, 0, 0> const&, int>);
static_assert(is_conv_v<of<int, 1, 0, 0>&&, int>);
static_assert(is_conv_v<of<int, 1, 0, 0> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 0, 0>&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 0, 0> const&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 0, 0>&&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 0, 0> const&&, int>);

static_assert(is_conv_v<of<int, 1, 0, 1>&, int>);
static_assert(is_conv_v<of<int, 1, 0, 1> const&, int>);
static_assert(is_conv_v<of<int, 1, 0, 1>&&, int>);
static_assert(is_conv_v<of<int, 1, 0, 1> const&&, int>);
static_assert(is_nx_conv_v<of<int, 1, 0, 1>&, int>);
static_assert(is_nx_conv_v<of<int, 1, 0, 1> const&, int>);
static_assert(is_nx_conv_v<of<int, 1, 0, 1>&&, int>);
static_assert(is_nx_conv_v<of<int, 1, 0, 1> const&&, int>);

static_assert(!is_conv_v<of<int, 1, 1, 0>&, int>);
static_assert(!is_conv_v<of<int, 1, 1, 0> const&, int>);
static_assert(is_conv_v<of<int, 1, 1, 0>&&, int>);
static_assert(is_conv_v<of<int, 1, 1, 0> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 0>&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 0> const&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 0>&&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 0> const&&, int>);

static_assert(!is_conv_v<of<int, 1, 1, 1>&, int>);
static_assert(!is_conv_v<of<int, 1, 1, 1> const&, int>);
static_assert(is_conv_v<of<int, 1, 1, 1>&&, int>);
static_assert(is_conv_v<of<int, 1, 1, 1> const&&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 1>&, int>);
static_assert(!is_nx_conv_v<of<int, 1, 1, 1> const&, int>);
static_assert(is_nx_conv_v<of<int, 1, 1, 1>&&, int>);
static_assert(is_nx_conv_v<of<int, 1, 1, 1> const&&, int>);

} // namespace folly::detail::invocable_to_test

template <class T>
constexpr bool is_vector = folly::detail::is_instantiation_of_v<std::vector, T>;

TEST_F(UtilityTest, if_constexpr) {
  static_assert(folly::if_constexpr<true>(1, 2) == 1);
  static_assert(folly::if_constexpr<false>(1, 2) == 2);
  static_assert(
      folly::if_constexpr<true>([] { return 1; }, [] { return 2; })() == 1);
  static_assert(
      folly::if_constexpr<false>([] { return 1; }, [] { return 2; })() == 2);

  std::vector<int> v;
  std::set<int> e;

  auto pushback = [](auto& c) { return c.push_back(42); };
  folly::if_constexpr<is_vector<decltype(v)>>(pushback, [](auto&) {})(v);
  folly::if_constexpr<is_vector<decltype(e)>>(pushback, [](auto&) {})(e);

  ASSERT_EQ(v.size(), 1);
  EXPECT_EQ(v.at(0), 42);
  EXPECT_TRUE(e.empty());
}
