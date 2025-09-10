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

#include <gtest/gtest.h>

#include <folly/result/gtest_helpers.h>
#include <folly/result/value_only_result_coro.h>

#if FOLLY_HAS_RESULT

namespace folly {

// Fully tests `void`-specific behaviors.  Loosely covers common features from
// `value_only_result_crtp` -- the subsequent non-`void` tests cover them more.
TEST(ValueOnlyResult, ofVoid) {
  // Cover the handful of things specific to the `void` specialization, plus
  // copyability & movability.
  {
    static_assert(!std::is_copy_constructible_v<value_only_result<>>);
    static_assert(!std::is_copy_assignable_v<value_only_result<>>);

    value_only_result<> r;
    static_assert(std::is_same_v<decltype(r), value_only_result<void>>);
    static_assert(std::is_void_v<decltype(r.value_or_throw())>);
    static_assert(std::is_void_v<decltype(r.value_only())>);
    EXPECT_TRUE(r.has_value());
    r.value_or_throw();
    r.value_only();

    auto r2{std::move(r)}; // move-construct
    value_only_result<> r3;
    r3 = std::move(r2); // move-assign

    r3.copy().value_or_throw();
    r3.copy().value_only();
  }
}

RESULT_CO_TEST(ValueOnlyResult, CTAD) {
  value_only_result ri = 42;
  static_assert(std::is_same_v<value_only_result<int>, decltype(ri)>);
  EXPECT_EQ(42, co_await or_unwind(std::move(ri)));

  value_only_result rsp = std::make_unique<std::string>("foo");
  static_assert(
      std::is_same_v<
          value_only_result<std::unique_ptr<std::string>>,
          decltype(rsp)>);
  EXPECT_EQ(std::string{"foo"}, *std::as_const(rsp).value_or_throw());
  EXPECT_EQ(std::string{"foo"}, *std::as_const(rsp).value_only());
}

TEST(ValueOnlyResult, movable) {
  value_only_result<> rVoidSrc;
  auto rVoid = std::move(rVoidSrc);
  EXPECT_TRUE(rVoid.has_value());

  value_only_result<std::unique_ptr<int>> mIntPtrSrc =
      std::make_unique<int>(1337);
  auto mIntPtr = std::move(mIntPtrSrc);
  EXPECT_EQ(1337, *mIntPtr.value_or_throw());
  EXPECT_EQ(1337, *mIntPtr.value_only());
}

TEST(ValueOnlyResult, refCopiable) {
  auto intPtr = std::make_unique<int>(1337);
  value_only_result<std::unique_ptr<int>&> mIntPtrRef1 = std::ref(intPtr);
  auto mIntPtrRef2 = mIntPtrRef1.copy();
  *(mIntPtrRef2.value_or_throw()) += 1;
  *(mIntPtrRef2.value_only()) += 10;
  EXPECT_EQ(1348, *mIntPtrRef1.value_or_throw());
  EXPECT_EQ(1348, *mIntPtrRef1.value_only());
  EXPECT_EQ(1348, *intPtr);
}

TEST(ValueOnlyResult, copyMethod) {
  value_only_result<int> r{1337};
  auto rToo = r.copy();
  EXPECT_EQ(r.value_or_throw(), rToo.value_only());
  EXPECT_TRUE(r == rToo);
}

// Check `co_await` / `.value_or_throw()` / `.value_only()` for various ways of
// accessing `value_only_result<V&>` and `value_only_result<V&&>`.
//
// See `checkAwaitResumeTypeForRefResult` in `result_test.cpp` for a more
// detailed discussion.
template <typename T>
void checkAwaitResumeTypeForRefResult() {
  // `result`s with r-value refs must be r-value qualified for access, since
  // `folly::rref` models a "use-once" / "destructive-access" reference.
  using AwRR =
      decltype(or_unwind(std::move(FOLLY_DECLVAL(value_only_result<T&&>&&))));
  static_assert( // co_await or_unwind(resFn())
      std::is_same_v<T&&, coro::await_result_t<AwRR>>);
  static_assert( // `value_or_throw` follows `co_await`
      std::is_same_v<
          T&&,
          decltype(FOLLY_DECLVAL(value_only_result<T&&>&&).value_or_throw())>);
  static_assert( // `value_only` follows `co_await`
      std::is_same_v<
          T&&,
          decltype(FOLLY_DECLVAL(value_only_result<T&&>&&).value_only())>);

  // `co_await`ing a `result` with an l-value ref always returns the ref

  // `co_await or_unwind(r)`
  using AwLL = decltype(or_unwind(FOLLY_DECLVAL(value_only_result<T&>&)));
  static_assert(std::is_same_v<T&, coro::await_result_t<AwLL>>);

  // `co_await or_unwind(std::as_const(r))`
  using AwCLL =
      decltype(or_unwind(FOLLY_DECLVAL(const value_only_result<T&>&)));
  static_assert(std::is_same_v<const T&, coro::await_result_t<AwCLL>>);

  // `co_await or_unwind(std::move(r))`
  using AwLR = decltype(or_unwind(FOLLY_DECLVAL(value_only_result<T&>&&)));
  static_assert(std::is_same_v<T&, coro::await_result_t<AwLR>>);

  // `value_or_throw()` behaves like `co_await` for l-value ref `result`s
  static_assert(
      std::is_same_v< // `r.value_or_throw()`
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>&).value_or_throw())>);
  static_assert(
      std::is_same_v< // `std::as_const(r).value_or_throw()`
          const T&,
          decltype(FOLLY_DECLVAL(const value_only_result<T&>&)
                       .value_or_throw())>);
  static_assert(
      std::is_same_v< // `std::move(r).value_or_throw()`
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>&&).value_or_throw())>);
  static_assert( // prvalue `result`, for good measure
      std::is_same_v<
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>).value_or_throw())>);

  // `value_only()` behaves like `co_await` for l-value ref `result`s
  static_assert(
      std::is_same_v< // `r.value_only()`
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>&).value_only())>);
  static_assert(
      std::is_same_v< // `std::as_const(r).value_only()`
          const T&,
          decltype(FOLLY_DECLVAL(const value_only_result<T&>&).value_only())>);
  static_assert(
      std::is_same_v< // `std::move(r).value_only()`
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>&&).value_only())>);
  static_assert( // prvalue `result`, for good measure
      std::is_same_v<
          T&,
          decltype(FOLLY_DECLVAL(value_only_result<T&>).value_only())>);
}

RESULT_CO_TEST(ValueOnlyResult, fromRefWrapperAndRefAccess) {
  using T = std::unique_ptr<int>;
  checkAwaitResumeTypeForRefResult<T>();

  T t1 = std::make_unique<int>(321);
  T t2 = std::make_unique<int>(567);
  {
    value_only_result<T&> rLref = std::ref(t1);
    EXPECT_EQ(321, *(co_await or_unwind(rLref.copy())));
    EXPECT_EQ(321, *rLref.value_or_throw());
    EXPECT_EQ(321, *rLref.value_only());
    *(co_await or_unwind(rLref)) += 1;
    EXPECT_EQ(322, *t1);
    (co_await or_unwind(std::move(rLref))) = std::move(t2);
    EXPECT_EQ(567, *t1);
  }

  {
    value_only_result<const T&> rCref = std::cref(t1);
    EXPECT_EQ(567, *(co_await or_unwind(rCref.copy())));
    EXPECT_EQ(567, *rCref.value_or_throw());
    EXPECT_EQ(567, *rCref.value_only());
    // can change the int, not the unique_ptr --
    *(co_await or_unwind(std::as_const(rCref))) += 1;
    EXPECT_EQ(568, *t1);

    EXPECT_TRUE(t2 == nullptr); // was moved out above
    t2 = std::make_unique<int>(42);
    rCref = std::cref(t2); // assignment uses the implict ctor
    EXPECT_EQ(42, *(co_await or_unwind(rCref.copy())));
  }

  {
    value_only_result<T&&> rRref1 = rref(std::move(t1));
    value_only_result<T&&> rRref2 = rref(std::move(rRref1).value_or_throw());
    value_only_result<T&&> rRref3 = rref(std::move(rRref2).value_only());
    t2 = std::move(rRref3).value_or_throw();
    EXPECT_EQ(568, *t2);
  }
}

TEST(ValueOnlyResult, moveFromUnderlying) {
  using T = std::unique_ptr<int>;
  value_only_result<T> r(std::make_unique<int>(6)); // move ctor
  EXPECT_EQ(6, *r.value_or_throw());
  EXPECT_EQ(6, *r.value_only());
  r = std::make_unique<int>(28); // move assignment
  EXPECT_EQ(28, *r.value_or_throw());
  EXPECT_EQ(28, *r.value_only());
}

// The point is that `std::move` is not required in `return` even when a
// "value/error" -> "value_only_result" conversion happens.
RESULT_CO_TEST(ValueOnlyResult, movableContexts) {
  auto fn = []() -> value_only_result<std::unique_ptr<int>> {
    auto fivePtr = std::make_unique<int>(5);
    return fivePtr;
  };
  EXPECT_EQ(5, *(co_await or_unwind(fn())));
}

RESULT_CO_TEST(ValueOnlyResult, copySmallTrivialUnderlying) {
  struct Smol {
    size_t a;
    size_t b;
    FOLLY_PUSH_WARNING
    // https://fb.workplace.com/groups/263608737897745/posts/1520838108841462
    FOLLY_CLANG_DISABLE_WARNING("-Wunused-member-function") // Clang bug!
    auto operator<=>(const Smol&) const = default;
    FOLLY_POP_WARNING
  };
  Smol s{13, 21};
  value_only_result<Smol> m{s};
  EXPECT_EQ(s, co_await or_unwind(m));
  EXPECT_EQ(s, co_await or_unwind(std::move(m)));
  EXPECT_EQ(s, co_await or_unwind(value_only_result<Smol>{s}));
}

RESULT_CO_TEST(ValueOnlyResult, simpleConversion) {
  int n1 = 1000, n2 = 2000;
  value_only_result<const int&> rCrefN1 = std::cref(n1),
                                rCrefN2 = std::cref(n2);

  // Justification for making the conversion implicit.
  {
    // Non-`value_only_result` returns interconvert, so we mimic that.
    auto fn = []() -> std::string { return "fn"; };
    EXPECT_EQ(std::string("fn"), fn());

    auto resFn = []() -> value_only_result<std::string> {
      // Future: It should be possible to make this prettier with fancy CTAD
      // magic for string literals, but that's not the main point of this test.
      return value_only_result<const char*>{"resFn"};
    };
    EXPECT_EQ(std::string("resFn"), co_await or_unwind(resFn()));
  }

  // Copying conversion with `&` binding
  {
    value_only_result<int> rn{rCrefN1}; // ctor converts from ref wrapper
    value_only_result<float> rf{rn}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(std::move(rn)));
    EXPECT_EQ(1000, co_await or_unwind(std::move(rf)));

    rn = rCrefN2; // assignment converts from ref wrapper
    rf = rn; // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(std::move(rn)));
    EXPECT_EQ(2000, co_await or_unwind(std::move(rf)));
  }

  // Copying conversion with `const &` binding
  {
    value_only_result<int> rn{
        std::as_const(rCrefN1)}; // ctor converts from ref wrapper
    EXPECT_EQ(1000, co_await or_unwind(rn));

    value_only_result<float> rf{std::as_const(rn)}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(rf));

    rn = std::as_const(rCrefN2); // assignment converts from ref wrapper
    EXPECT_EQ(2000, co_await or_unwind(rn));

    rf = std::as_const(rn); // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(rf));
  }

  // (Possibly) move conversion with `&&` binding
  {
    value_only_result<int> rn{
        std::move(rCrefN1)}; // ctor converts from ref wrapper
    EXPECT_EQ(1000, co_await or_unwind(std::as_const(rn)));

    value_only_result<float> rf{std::move(rn)}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(std::as_const(rf)));

    rn = std::move(rCrefN2); // assignment converts from ref wrapper
    EXPECT_EQ(2000, co_await or_unwind(std::as_const(rn)));

    rf = std::move(rn); // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(std::as_const(rf)));
  }
}

TEST(ValueOnlyResult, accessValue) {
  {
    value_only_result<int> r{555};
    EXPECT_TRUE(r.has_value());

    EXPECT_EQ(555, r.value_only());
    EXPECT_EQ(555, std::as_const(r).value_only());
    EXPECT_EQ(555, std::move(r).value_only());
  }

  value_only_result<int> r{555};
  EXPECT_TRUE(r.has_value());

  EXPECT_EQ(555, r.value_or_throw());
  EXPECT_EQ(555, std::as_const(r).value_or_throw());
  EXPECT_EQ(555, std::move(r).value_or_throw());

  // `r` is moved out, so let's store a new value.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  r.value_only() = 37;
  EXPECT_EQ(37, r.value_only());
  r.value_or_throw() = 7;
  EXPECT_EQ(7, r.value_or_throw());

  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  value_only_result<int> rSame{7};
  value_only_result<int> rDiff{2};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);
}

TEST(ValueOnlyResult, accessLvalueRef) {
  {
    const int n1 = 555;
    value_only_result<const int&> r{std::ref(n1)};
    EXPECT_TRUE(r.has_value());

    EXPECT_EQ(555, r.value_only());
    EXPECT_EQ(555, std::as_const(r).value_only());
    EXPECT_EQ(555, std::move(r).value_only());
  }

  const int n1 = 555;
  value_only_result<const int&> r{std::ref(n1)};
  EXPECT_TRUE(r.has_value());

  EXPECT_EQ(555, r.value_or_throw());
  EXPECT_EQ(555, std::as_const(r).value_or_throw());
  EXPECT_EQ(555, std::move(r).value_or_throw());

  // `r` is moved out, so let's store a new value.
  const int n2 = 7;
  r = std::ref(n2);
  EXPECT_EQ(7, r.value_or_throw());
  EXPECT_EQ(7, r.value_only());

  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  value_only_result<const int&> rSame{std::ref(n2)};
  value_only_result<const int&> rDiff{std::ref(n1)};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);
}

TEST(ValueOnlyResult, accessRvalueRef) {
  int n1 = 555;
  value_only_result<int&&> r{rref((int&&)n1)};
  EXPECT_TRUE(r.has_value());

  r = rref((int&&)n1); // satisfy the moved-out linter
  EXPECT_EQ(555, std::move(r).value_or_throw());

  int n2 = 7;
  r = rref((int&&)n2); // satisfy the moved-out linter
  EXPECT_EQ(7, std::move(r).value_only());

  /* FIXME: Implement rvalue_reference_wrapper::operator==

  r = rref((int&&)n2); // was moved out, again
  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  value_only_result<int&&> rSame{rref((int&&)n2)};
  value_only_result<int&&> rDiff{rref((int&&)n1)};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);
  */
}

RESULT_CO_TEST(ValueOnlyResult, awaitValue) {
  auto returnsValue = []() -> value_only_result<uint8_t> { return 129; };
  // Test both EXPECT and CO_ASSERT
  EXPECT_EQ(129, co_await or_unwind(returnsValue()));
  CO_ASSERT_EQ(129, co_await or_unwind(returnsValue()));
}

RESULT_CO_TEST(ValueOnlyResult, awaitRef) {
  // Use a non-copiable type to show that `co_await` doesn't copy.
  value_only_result<std::unique_ptr<int>> var = std::make_unique<int>(17);
  auto& varRef = *(co_await or_unwind(var));
  EXPECT_EQ(17, varRef);
  varRef = 42;
  EXPECT_EQ(42, *(co_await or_unwind(std::as_const(var))));

  EXPECT_EQ(42, *(co_await or_unwind(std::move(var))));
  // `var` is not moved out yet! We just took a reference above.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(42, *(co_await or_unwind(var)));

  // NOLINTNEXTLINE(bugprone-use-after-move)
  auto ptr = co_await or_unwind(std::move(var));
  EXPECT_EQ(42, *ptr);
  // Now, `var` is moved out & null.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_FALSE(co_await or_unwind(std::as_const(var)));
}

TEST(ValueOnlyResult, containsRValueRef) {
  std::string moveMe = "hello";
  auto inner = [&]() -> value_only_result<std::string&&> {
    return rref(std::move(moveMe));
  };
  auto outer = [&]() -> value_only_result<std::string&&> { return inner(); };

  auto&& moveMeRef = outer().value_or_throw();
  EXPECT_EQ("hello", moveMe);
  EXPECT_EQ("hello", moveMeRef);

  auto moved = outer().value_only();
  static_assert(std::is_same_v<decltype(moved), std::string>);
  EXPECT_EQ("", moveMe);
  EXPECT_EQ("hello", moved);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
