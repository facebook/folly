/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/functional/Invoke.h>

#include <folly/CppAttributes.h>
#include <folly/portability/GTest.h>

class InvokeTest : public testing::Test {};

namespace {

struct from_any {
  template <typename T>
  /* implicit */ from_any(T&&) {}
};

struct Fn {
  char operator()(int, int) noexcept {
    return 'a';
  }
  int volatile&& operator()(int, char const*) {
    return std::move(x_);
  }
  float operator()(float, float) {
    return 3.14;
  }
  int volatile x_ = 17;
};

FOLLY_CREATE_MEMBER_INVOKER(test_invoker, test);

struct Obj {
  char test(int, int) noexcept {
    return 'a';
  }
  int volatile&& test(int, char const*) {
    return std::move(x_);
  }
  float test(float, float) {
    return 3.14;
  }
  int volatile x_ = 17;
};

namespace x {
struct Obj {};
int go(Obj const&, int) noexcept {
  return 3;
}
} // namespace x

namespace y {
struct Obj {};
char go(Obj const&, char const*) {
  return 'a';
}
} // namespace y

namespace z {
struct Obj {};
} // namespace z
FOLLY_MAYBE_UNUSED float go(z::Obj const&, int) {
  return 9;
}

namespace swappable {
struct Obj {
  int x_;
};
void swap(Obj&, Obj&) noexcept {} // no-op
} // namespace swappable

struct AltSwappable;
struct AltSwappableRet {};
namespace unswappable {
FOLLY_MAYBE_UNUSED AltSwappableRet swap(AltSwappable&, AltSwappable&);
} // namespace unswappable

FOLLY_CREATE_FREE_INVOKER(go_invoker, go);
FOLLY_CREATE_FREE_INVOKER(swap_invoker, swap, std, unswappable);
FOLLY_CREATE_FREE_INVOKER(unused_invoker, definitely_unused_name_);

} // namespace

TEST_F(InvokeTest, invoke) {
  Fn fn;

  EXPECT_TRUE(noexcept(folly::invoke(fn, 1, 2)));
  EXPECT_FALSE(noexcept(folly::invoke(fn, 1, "2")));

  EXPECT_EQ('a', folly::invoke(fn, 1, 2));
  EXPECT_EQ(17, folly::invoke(fn, 1, "2"));

  using FnA = char (Fn::*)(int, int);
  using FnB = int volatile && (Fn::*)(int, char const*);
  EXPECT_EQ('a', folly::invoke(static_cast<FnA>(&Fn::operator()), fn, 1, 2));
  EXPECT_EQ(17, folly::invoke(static_cast<FnB>(&Fn::operator()), fn, 1, "2"));
}

TEST_F(InvokeTest, invoke_result) {
  EXPECT_TRUE(
      (std::is_same<char, folly::invoke_result_t<Fn, int, char>>::value));
  EXPECT_TRUE(
      (std::is_same<int volatile&&, folly::invoke_result_t<Fn, int, char*>>::
           value));
}

TEST_F(InvokeTest, is_invocable) {
  EXPECT_TRUE((folly::is_invocable_v<Fn, int, char>));
  EXPECT_TRUE((folly::is_invocable_v<Fn, int, char*>));
  EXPECT_FALSE((folly::is_invocable_v<Fn, int>));
}

TEST_F(InvokeTest, is_invocable_r) {
  EXPECT_TRUE((folly::is_invocable_r_v<int, Fn, int, char>));
  EXPECT_TRUE((folly::is_invocable_r_v<int, Fn, int, char*>));
  EXPECT_FALSE((folly::is_invocable_r_v<int, Fn, int>));
}

TEST_F(InvokeTest, is_nothrow_invocable) {
  EXPECT_TRUE((folly::is_nothrow_invocable_v<Fn, int, char>));
  EXPECT_FALSE((folly::is_nothrow_invocable_v<Fn, int, char*>));
  EXPECT_FALSE((folly::is_nothrow_invocable_v<Fn, int>));
}

TEST_F(InvokeTest, is_nothrow_invocable_r) {
  EXPECT_TRUE((folly::is_nothrow_invocable_r_v<int, Fn, int, char>));
  EXPECT_FALSE((folly::is_nothrow_invocable_r_v<int, Fn, int, char*>));
  EXPECT_FALSE((folly::is_nothrow_invocable_r_v<int, Fn, int>));
}

TEST_F(InvokeTest, free_invoke) {
  using traits = folly::invoke_traits<go_invoker>;

  x::Obj x_;
  y::Obj y_;

  EXPECT_TRUE(noexcept(traits::invoke(x_, 3)));
  EXPECT_FALSE(noexcept(traits::invoke(y_, "hello")));

  EXPECT_EQ(3, traits::invoke(x_, 3));
  EXPECT_EQ('a', traits::invoke(y_, "hello"));
}

TEST_F(InvokeTest, free_invoke_result) {
  using traits = folly::invoke_traits<go_invoker>;

  EXPECT_TRUE((std::is_same<int, traits::invoke_result_t<x::Obj, int>>::value));
  EXPECT_TRUE((
      std::is_same<char, traits::invoke_result_t<y::Obj, char const*>>::value));
}

TEST_F(InvokeTest, free_is_invocable) {
  using traits = folly::invoke_traits<go_invoker>;

  EXPECT_TRUE((traits::is_invocable_v<x::Obj, int>));
  EXPECT_TRUE((traits::is_invocable_v<y::Obj, char const*>));
  EXPECT_FALSE((traits::is_invocable_v<z::Obj, int>));
  EXPECT_FALSE((traits::is_invocable_v<float>));
}

TEST_F(InvokeTest, free_is_invocable_r) {
  using traits = folly::invoke_traits<go_invoker>;

  EXPECT_TRUE((traits::is_invocable_r_v<int, x::Obj, int>));
  EXPECT_TRUE((traits::is_invocable_r_v<char, y::Obj, char const*>));
  EXPECT_FALSE((traits::is_invocable_r_v<float, z::Obj, int>));
  EXPECT_FALSE((traits::is_invocable_r_v<from_any, float>));
}

TEST_F(InvokeTest, free_is_nothrow_invocable) {
  using traits = folly::invoke_traits<go_invoker>;

  EXPECT_TRUE((traits::is_nothrow_invocable_v<x::Obj, int>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<y::Obj, char const*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<z::Obj, int>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<float>));
}

TEST_F(InvokeTest, free_is_nothrow_invocable_r) {
  using traits = folly::invoke_traits<go_invoker>;

  EXPECT_TRUE((traits::is_nothrow_invocable_r_v<int, x::Obj, int>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<char, y::Obj, char const*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<float, z::Obj, int>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<from_any, float>));
}

TEST_F(InvokeTest, free_invoke_swap) {
  using traits = folly::invoke_traits<swap_invoker>;

  int a = 3;
  int b = 4;

  traits::invoke(a, b);
  EXPECT_EQ(4, a);
  EXPECT_EQ(3, b);

  swappable::Obj x{3};
  swappable::Obj y{4};

  traits::invoke(x, y);
  EXPECT_EQ(3, x.x_);
  EXPECT_EQ(4, y.x_);

  std::swap(x, y);
  EXPECT_EQ(4, x.x_);
  EXPECT_EQ(3, y.x_);

  EXPECT_TRUE((
      traits::is_invocable_r_v<AltSwappableRet, AltSwappable&, AltSwappable&>));
}

TEST_F(InvokeTest, member_invoke) {
  using traits = folly::invoke_traits<test_invoker>;

  Obj fn;

  EXPECT_TRUE(noexcept(traits::invoke(fn, 1, 2)));
  EXPECT_FALSE(noexcept(traits::invoke(fn, 1, "2")));

  EXPECT_EQ('a', traits::invoke(fn, 1, 2));
  EXPECT_EQ(17, traits::invoke(fn, 1, "2"));
}

TEST_F(InvokeTest, member_invoke_result) {
  using traits = folly::invoke_traits<test_invoker>;

  EXPECT_TRUE(
      (std::is_same<char, traits::invoke_result_t<Obj, int, char>>::value));
  EXPECT_TRUE(
      (std::is_same<int volatile&&, traits::invoke_result_t<Obj, int, char*>>::
           value));
}

TEST_F(InvokeTest, member_is_invocable) {
  using traits = folly::invoke_traits<test_invoker>;

  EXPECT_TRUE((traits::is_invocable_v<Obj, int, char>));
  EXPECT_TRUE((traits::is_invocable_v<Obj, int, char*>));
  EXPECT_FALSE((traits::is_invocable_v<Obj, int>));
}

TEST_F(InvokeTest, member_is_invocable_r) {
  using traits = folly::invoke_traits<test_invoker>;

  EXPECT_TRUE((traits::is_invocable_r_v<int, Obj, int, char>));
  EXPECT_TRUE((traits::is_invocable_r_v<int, Obj, int, char*>));
  EXPECT_FALSE((traits::is_invocable_r_v<int, Obj, int>));
}

TEST_F(InvokeTest, member_is_nothrow_invocable) {
  using traits = folly::invoke_traits<test_invoker>;

  EXPECT_TRUE((traits::is_nothrow_invocable_v<Obj, int, char>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<Obj, int, char*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<Obj, int>));
}

TEST_F(InvokeTest, member_is_nothrow_invocable_r) {
  using traits = folly::invoke_traits<test_invoker>;

  EXPECT_TRUE((traits::is_nothrow_invocable_r_v<int, Obj, int, char>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<int, Obj, int, char*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<int, Obj, int>));
}

FOLLY_CREATE_STATIC_MEMBER_INVOKER(stat_invoker, stat);

TEST_F(InvokeTest, static_member_invoke) {
  struct HasStat {
    static char stat(int, int) noexcept {
      return 'a';
    }
    static int volatile&& stat(int, char const*) {
      static int volatile x_ = 17;
      return std::move(x_);
    }
    static float stat(float, float) {
      return 3.14;
    }
  };
  using traits = folly::invoke_traits<stat_invoker<HasStat>>;

  EXPECT_TRUE((traits::is_invocable_v<int, char>));
  EXPECT_TRUE((traits::is_invocable_v<int, char>));
  EXPECT_TRUE((traits::is_invocable_v<int, char*>));
  EXPECT_FALSE((traits::is_invocable_v<int>));

  EXPECT_TRUE((traits::is_invocable_r_v<int, int, char>));
  EXPECT_TRUE((traits::is_invocable_r_v<int, int, char*>));
  EXPECT_FALSE((traits::is_invocable_r_v<int, int>));

  EXPECT_TRUE((traits::is_nothrow_invocable_v<int, char>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<int, char*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_v<int>));

  EXPECT_TRUE((traits::is_nothrow_invocable_r_v<int, int, char>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<int, int, char*>));
  EXPECT_FALSE((traits::is_nothrow_invocable_r_v<int, int>));
}

namespace {

struct TestCustomisationPointFn {
  template <typename T, typename U>
  constexpr auto operator()(T&& t, U&& u) const noexcept(
      folly::is_nothrow_tag_invocable_v<TestCustomisationPointFn, T, U>)
      -> folly::tag_invoke_result_t<TestCustomisationPointFn, T, U> {
    return folly::tag_invoke(*this, static_cast<T&&>(t), static_cast<U&&>(u));
  }
};

FOLLY_DEFINE_CPO(TestCustomisationPointFn, testCustomisationPoint)

struct TypeA {
  constexpr friend int
  tag_invoke(folly::cpo_t<testCustomisationPoint>, const TypeA&, int value) {
    return value * 2;
  }
  constexpr friend bool tag_invoke(
      folly::cpo_t<testCustomisationPoint>,
      const TypeA&,
      bool value) noexcept {
    return !value;
  }
};

} // namespace

static_assert(
    folly::is_invocable<decltype(testCustomisationPoint), TypeA, int>::value);
static_assert(
    !folly::is_invocable<decltype(testCustomisationPoint), TypeA, TypeA>::
        value);
static_assert(
    folly::is_nothrow_invocable<decltype(testCustomisationPoint), TypeA, bool>::
        value);
static_assert(
    !folly::is_nothrow_invocable<decltype(testCustomisationPoint), TypeA, int>::
        value);
static_assert(
    std::is_same<
        folly::invoke_result_t<decltype(testCustomisationPoint), TypeA, int>,
        int>::value);

// Test that the CPO forwards through constexpr-ness of the
// customisations by evaluating the CPO in a static_assert()
// which forces compile-time evaluation.
static_assert(testCustomisationPoint(TypeA{}, 10) == 20);
static_assert(!testCustomisationPoint(TypeA{}, true));

TEST_F(InvokeTest, TagInvokeCustomisationPoint) {
  const TypeA a;
  EXPECT_EQ(10, testCustomisationPoint(a, 5));
  EXPECT_EQ(false, testCustomisationPoint(a, true));
}
