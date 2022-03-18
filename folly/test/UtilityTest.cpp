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

} // namespace folly

namespace {

class UtilityTest : public testing::Test {};
} // namespace

// Tests for FOLLY_DECLVAL macro:

static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int)), int>);
static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int&)), int&>);
static_assert(std::is_same_v<decltype(FOLLY_DECLVAL(int&&)), int&&>);
static_assert(noexcept(FOLLY_DECLVAL(int)));

// Tests for folly::decay_t:

template <typename T>
using dec = folly::detail::decay_t<T>;
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
static_assert(std::is_same_v<int (*)(), dec<int (*)()>>);
static_assert(std::is_same_v<int (*)(), dec<int (&)()>>);

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
