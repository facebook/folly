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

#include <folly/coro/Traits.h>
#include <folly/result/gtest_helpers.h>

#if FOLLY_HAS_RESULT

namespace folly {

class MyError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Fully tests `void`-specific behaviors.  Loosely covers common features from
// `result_crtp` -- they're covered in-depth by the non-`void` tests below.
TEST(Result, resultOfVoid) {
  // Cover the handful of things specific to the `result<void>` specialization,
  // plus copyability & movability.
  {
    static_assert(!std::is_copy_constructible_v<result<>>);
    static_assert(!std::is_copy_assignable_v<result<>>);

    result<> r;
    static_assert(std::is_same_v<decltype(r), result<void>>);
    static_assert(std::is_void_v<decltype(r.value_or_throw())>);
    EXPECT_TRUE(r.has_value());
    r.value_or_throw();

    auto r2{std::move(r)}; // move-construct
    result<> r3;
    r3 = std::move(r2); // move-assign

    r3.copy().value_or_throw();
  }
  { // `result<void>` in an error state
    result<> r(non_value_result{MyError{"soup"}}); // ctor
    EXPECT_STREQ("soup", get_exception<MyError>(r)->what());
    EXPECT_STREQ("soup", get_exception<MyError>(std::as_const(r))->what());
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(r.has_stopped());

    r = non_value_result{MyError{"cake"}}; // assignment
    EXPECT_STREQ("cake", get_exception<MyError>(r)->what());
  }
  { // `result<void>` coro with "error" and "success" exit paths.
    auto voidResFn = [](bool fail) -> result<> {
      if (!fail) {
        co_return;
      }
      co_await non_value_result{MyError{"failed"}};
    };
    EXPECT_TRUE(voidResFn(false).has_value());
    auto r = voidResFn(true);
    EXPECT_STREQ("failed", get_exception<MyError>(r)->what());
  }
  { // A convert-to-`result<void>` analog of `ConversionCanFail` below
    struct ConvertToResultVoid {
      /*implicit*/ operator result<void>() const {
        if (fail_) {
          return non_value_result{MyError{"failed"}};
        }
        return {};
      }
      bool fail_;
    };
    result<ConvertToResultVoid> convOk = ConvertToResultVoid{.fail_ = false};
    result<void> ok{convOk};
    EXPECT_TRUE(convOk.has_value());

    result<ConvertToResultVoid> convFail = ConvertToResultVoid{.fail_ = true};
    result<void> fail{convFail};
    ASSERT_EQ(std::string("failed"), get_exception<MyError>(fail)->what());
  }
}

TEST(Result, noEmptyError) {
  if (kIsDebug) {
    EXPECT_DEATH(
        {
          (void)non_value_result::from_exception_wrapper(exception_wrapper{});
        },
        "`result` may not contain an empty `std::exception_ptr`");
  } else {
    EXPECT_FALSE(
        non_value_result::from_exception_wrapper(exception_wrapper{})
            .to_exception_wrapper()
            .has_exception_ptr());
  }
}

// "has_stopped() == false" is tested in other tests
TEST(Result, storeAndGetStoppedResult) {
  const char* deathRe =
      "Do not store `OperationCancelled` in `result`.* while extracting";
  auto check = [&]<typename T>(tag_t<T>, auto in) {
    result<T> r = std::move(in);
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.has_stopped());
    // Using `exception_ptr`-like accessors when `has_stopped()` is debug-fatal
    if (kIsDebug) {
      EXPECT_DEATH(
          { std::move(r).non_value().to_exception_wrapper(); }, deathRe);
    } else {
      auto ew = std::move(r).non_value().to_exception_wrapper();
      EXPECT_TRUE(get_exception<OperationCancelled>(ew));
    }
  };
  check(tag<void>, stopped_result);
  check(tag<int>, stopped_result);
  auto ocEw = make_exception_wrapper<OperationCancelled>();
  auto stoppedNvr = non_value_result::make_legacy_error_or_cancellation(ocEw);
  check(tag<void>, stoppedNvr);
  check(tag<int>, stoppedNvr);
  // Constructing with `OperationCancelled` without the legacy path
  // is debug-fatal.
  if (kIsDebug) {
    EXPECT_DEATH(
        { (void)non_value_result::from_exception_wrapper(ocEw); }, deathRe);
  } else {
    auto ew =
        non_value_result::from_exception_wrapper(ocEw).to_exception_wrapper();
    EXPECT_TRUE(get_exception<OperationCancelled>(ew));
  }
}

TEST(Result, awaitStoppedResult) {
  auto innerFn = []() -> result<> {
    co_await stopped_result;
    LOG(FATAL) << "not reached";
  };
  auto outerFn = [&]() -> result<> {
    co_await or_unwind(innerFn());
    LOG(FATAL) << "not reached";
  };
  auto r = outerFn();
  EXPECT_FALSE(r.has_value());
  EXPECT_TRUE(r.has_stopped());
}

RESULT_CO_TEST(Result, CTAD) {
  result ri = 42;
  static_assert(std::is_same_v<result<int>, decltype(ri)>);
  EXPECT_EQ(42, co_await or_unwind(std::move(ri)));

  result rsp = std::make_unique<std::string>("foo");
  static_assert(
      std::is_same_v<result<std::unique_ptr<std::string>>, decltype(rsp)>);
  EXPECT_EQ(std::string{"foo"}, *(co_await or_unwind(std::as_const(rsp))));
}

TEST(Result, movable) {
  result<> rVoidSrc;
  auto rVoid = std::move(rVoidSrc);
  EXPECT_TRUE(rVoid.has_value());

  result<std::unique_ptr<int>> mIntPtrSrc = std::make_unique<int>(1337);
  auto mIntPtr = std::move(mIntPtrSrc);
  EXPECT_EQ(1337, *mIntPtr.value_or_throw());
}

TEST(Result, refCopiable) {
  auto intPtr = std::make_unique<int>(1337);
  result<std::unique_ptr<int>&> mIntPtrRef1 = std::ref(intPtr);
  auto mIntPtrRef2 = mIntPtrRef1.copy();
  *(mIntPtrRef2.value_or_throw()) += 1;
  EXPECT_EQ(1338, *mIntPtrRef1.value_or_throw());
  EXPECT_EQ(1338, *intPtr);
}

TEST(Result, copyMethod) {
  result<int> r{1337};
  auto rToo = r.copy();
  EXPECT_EQ(r.value_or_throw(), rToo.value_or_throw());
  EXPECT_TRUE(r == rToo);

  result<int> rErr{non_value_result{MyError{"grr"}}};
  auto rErrToo = rErr.copy();
  EXPECT_EQ(rErr.non_value(), rErrToo.non_value());
  EXPECT_TRUE(rErr == rErrToo);

  EXPECT_TRUE(rErr != r);
  EXPECT_TRUE(rErrToo != rToo);
}

// `stopped_result` is covered separately
TEST(Result, fromNonValue) {
  non_value_result nvr{MyError{"nay"}};
  {
    result<int> r{nvr}; // ctor
    EXPECT_EQ(std::string("nay"), get_exception<MyError>(r)->what());
    r = copy(nvr); // assignment
    EXPECT_EQ(std::string("nay"), get_exception<MyError>(r)->what());
  }
  nvr = non_value_result{MyError{"nein"}};
  {
    auto r = [&nvr]() -> result<> {
      co_await std::move(nvr); // await
      LOG(FATAL) << "not reached";
    }();
    EXPECT_EQ(std::string("nein"), get_exception<MyError>(r)->what());
  }
}

RESULT_CO_TEST(Result, ofLvalueReferenceWrapper) {
  int n = 3;
  // Yes, you can declare `result<reference_wrapper<V>>`.  This is one way to
  // mutate values through `const result<Ref>` (the other being `result<V*>`).
  {
    result r = std::ref(n);
    static_assert(
        std::is_same_v<result<std::reference_wrapper<int>>, decltype(r)>);
    // The `.get()` is here to show that a ref-wrapper is being returned.
    EXPECT_EQ(3, (co_await or_unwind(std::move(r))).get());
  }
  // To store `result<V&>` you must declare the type -- no CTAD.
  {
    result<int&> r = std::ref(n);
    EXPECT_EQ(3, co_await or_unwind(std::move(r)));
  }
}

RESULT_CO_TEST(Result, ofRvalueReferenceWrapper) {
  // Yes, you can declare `result<rvalue_reference_wrapper<V>>`.
  {
    int n = 3;
    result r = rref(std::move(n));
    static_assert(
        std::is_same_v<result<rvalue_reference_wrapper<int>>, decltype(r)>);
    // The `.get()` is here to show that a ref-wrapper is being returned.
    EXPECT_EQ(3, (co_await or_unwind(std::move(r))).get());
  }
  // To store `result<V&&>` you must declare the type -- no CTAD.
  {
    int n = 3;
    result<int&&> r = rref(std::move(n));
    EXPECT_EQ(3, co_await or_unwind(std::move(r)));
  }
}

// Check `?.value_or_throw()` and `co_await ?` return types for various ways of
// accessing `result<V&>` and `result<V&&>`.
//
// For `result<int&>`, the following mappings are uncontroversial -- it's what
// is needed for e.g. map getter methods like `result<Val&> at(Key)`.
//
//   result<int&> n();
//   n().value_or_throw() -> int&
//   co_await n() -> int&
//
//   result<int&> rn = n();
//   rn.value_or_throw() -> int&
//   co_await or_unwind(rn) -> int&
//   co_await or_unwind(std::move(rn)) -> int&&
//
//   !! `result.md` explains why these return ref-to-const !!
//
//   std::as_const(rn).value_or_throw() -> const int&
//   co_await or_unwind(std::as_const(rn)) -> const int&
template <typename T>
void checkAwaitResumeTypeForRefResult() {
  // `result`s with r-value refs must be r-value qualified for access, since
  // `folly::rref` models a "use-once" / "destructive-access" reference.
  using AwRR = decltype(or_unwind(std::move(FOLLY_DECLVAL(result<T&&>&&))));
  static_assert( // co_await or_unwind(resFn())
      std::is_same_v<T&&, coro::await_result_t<AwRR>>);
  static_assert( // `value_or_throw` follows `co_await`
      std::is_same_v<
          T&&,
          decltype(FOLLY_DECLVAL(result<T&&>&&).value_or_throw())>);

  // `co_await`ing a `result` with an l-value ref always returns the ref

  // `co_await or_unwind(r)`
  using AwLL = decltype(or_unwind(FOLLY_DECLVAL(result<T&>&)));
  static_assert(std::is_same_v<T&, coro::await_result_t<AwLL>>);

  // `co_await or_unwind(std::as_const(r))`
  using AwCLL = decltype(or_unwind(FOLLY_DECLVAL(const result<T&>&)));
  static_assert(std::is_same_v<const T&, coro::await_result_t<AwCLL>>);

  // `co_await or_unwind(std::move(r))`
  using AwLR = decltype(or_unwind(FOLLY_DECLVAL(result<T&>&&)));
  static_assert(std::is_same_v<T&, coro::await_result_t<AwLR>>);

  // `value_or_throw()` behaves like `co_await` for l-value ref `result`s
  static_assert(
      std::is_same_v< // `r.value_or_throw()`
          T&,
          decltype(FOLLY_DECLVAL(result<T&>&).value_or_throw())>);
  static_assert(
      std::is_same_v< // `std::as_const(r).value_or_throw()`
          const T&,
          decltype(FOLLY_DECLVAL(const result<T&>&).value_or_throw())>);
  static_assert(
      std::is_same_v< // `std::move(r).value_or_throw()`
          T&,
          decltype(FOLLY_DECLVAL(result<T&>&&).value_or_throw())>);
  static_assert( // prvalue `result`, for good measure
      std::is_same_v<T&, decltype(FOLLY_DECLVAL(result<T&>).value_or_throw())>);
}

RESULT_CO_TEST(Result, fromRefWrapperAndRefAccess) {
  using T = std::unique_ptr<int>;
  checkAwaitResumeTypeForRefResult<T>();

  T t1 = std::make_unique<int>(321);
  T t2 = std::make_unique<int>(567);
  {
    result<T&> rLref = std::ref(t1);
    EXPECT_EQ(321, *(co_await or_unwind(rLref.copy())));
    EXPECT_EQ(321, *rLref.value_or_throw());
    *(co_await or_unwind(rLref)) += 1;
    EXPECT_EQ(322, *t1);
    (co_await or_unwind(std::move(rLref))) = std::move(t2);
    EXPECT_EQ(567, *t1);
  }

  {
    result<const T&> rCref = std::cref(t1);
    EXPECT_EQ(567, *(co_await or_unwind(rCref.copy())));
    EXPECT_EQ(567, *rCref.value_or_throw());
    *(co_await or_unwind(std::as_const(rCref))) +=
        1; // can change the int, not the unique_ptr
    EXPECT_EQ(568, *t1);

    EXPECT_TRUE(t2 == nullptr); // was moved out above
    t2 = std::make_unique<int>(42);
    rCref = std::cref(t2); // assignment uses the implict ctor
    EXPECT_EQ(42, *(co_await or_unwind(rCref.copy())));
  }

  {
    result<T&&> rRref1 = rref(std::move(t1));
    result<T&&> rRref2 = rref(std::move(rRref1).value_or_throw());
    t2 = std::move(co_await or_unwind(std::move(rRref2)));
    EXPECT_EQ(568, *t2);
  }
}

TEST(Result, moveFromUnderlying) {
  using T = std::unique_ptr<int>;
  result<T> r(std::make_unique<int>(6)); // move ctor
  EXPECT_EQ(6, *r.value_or_throw());
  r = std::make_unique<int>(28); // move assignment
  EXPECT_EQ(28, *r.value_or_throw());
}

// The point is that `std::move` is not required in `return` / `co_return`,
// even when there's an "value/error" -> "result" conversion taking place.
RESULT_CO_TEST(Result, movableContexts) {
  {
    auto fn = []() -> result<std::unique_ptr<int>> {
      auto fivePtr = std::make_unique<int>(5);
      co_return fivePtr;
    };
    EXPECT_EQ(5, *(co_await or_unwind(fn())));
  }
  {
    auto fn = []() -> result<std::unique_ptr<int>> {
      auto err = non_value_result{MyError{"foo"}};
      return err;
    };
    auto res = fn();
    EXPECT_STREQ("foo", get_exception<MyError>(res)->what());
  }
  {
    auto fn = []() -> result<std::unique_ptr<int>> {
      auto err = non_value_result{MyError{"foo"}};
      co_return err;
    };
    auto res = fn();
    EXPECT_STREQ("foo", get_exception<MyError>(res)->what());
  }
}

RESULT_CO_TEST(Result, copySmallTrivialUnderlying) {
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
  result<Smol> m{s};
  EXPECT_EQ(s, co_await or_unwind(m));
  EXPECT_EQ(s, co_await or_unwind(std::move(m)));
  EXPECT_EQ(s, co_await or_unwind(result<Smol>{s}));
}

RESULT_CO_TEST(Result, simpleConversion) {
  int n1 = 1000, n2 = 2000;
  result<const int&> rCrefN1 = std::cref(n1), rCrefN2 = std::cref(n2);

  // Justification for making the conversion implicit.
  {
    // Non-`result` returns interconvert, so we mimic that.
    auto fn = []() -> std::string { return "fn"; };
    EXPECT_EQ(std::string("fn"), fn());

    auto resFn = []() -> result<std::string> {
      // Future: It should be possible to make this prettier with fancy CTAD
      // magic for string literals, but that's not the main point of this test.
      return result<const char*>{"resFn"};
    };
    EXPECT_EQ(std::string("resFn"), co_await or_unwind(resFn()));

    auto coResFn = []() -> result<std::string> {
      return result<const char*>{"coResFn"};
    };
    EXPECT_EQ(std::string("coResFn"), co_await or_unwind(coResFn()));
  }

  // Copying conversion with `&` binding
  {
    result<int> rn{rCrefN1}; // ctor converts from ref wrapper
    result<float> rf{rn}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(std::move(rn)));
    EXPECT_EQ(1000, co_await or_unwind(std::move(rf)));

    rn = rCrefN2; // assignment converts from ref wrapper
    rf = rn; // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(std::move(rn)));
    EXPECT_EQ(2000, co_await or_unwind(std::move(rf)));
  }

  // Copying conversion with `const &` binding
  {
    result<int> rn{std::as_const(rCrefN1)}; // ctor converts from ref wrapper
    EXPECT_EQ(1000, co_await or_unwind(rn));

    result<float> rf{std::as_const(rn)}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(rf));

    rn = std::as_const(rCrefN2); // assignment converts from ref wrapper
    EXPECT_EQ(2000, co_await or_unwind(rn));

    rf = std::as_const(rn); // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(rf));
  }

  // (Possibly) move conversion with `&&` binding
  {
    result<int> rn{std::move(rCrefN1)}; // ctor converts from ref wrapper
    EXPECT_EQ(1000, co_await or_unwind(std::as_const(rn)));

    result<float> rf{std::move(rn)}; // ctor converts from value
    EXPECT_EQ(1000, co_await or_unwind(std::as_const(rf)));

    rn = std::move(rCrefN2); // assignment converts from ref wrapper
    EXPECT_EQ(2000, co_await or_unwind(std::as_const(rn)));

    rf = std::move(rn); // assignment converts from value
    EXPECT_EQ(2000, co_await or_unwind(std::as_const(rf)));
  }
}

RESULT_CO_TEST(Result, fallibleConversion) {
  struct ConversionCanFail {
    // Note that the user conversion can be explicit -- and indeed, the `for`
    // loop below is "explicitly" selecting the destination type.
    explicit operator result<int>() const {
      if (v_ == 0) {
        return non_value_result{MyError{"zero"}};
      }
      return v_;
    }
    int v_;
  };

  // This is the actual use-case for the "fallible conversion".  Here, the
  // iterator itself can fail, so it outputs `result`s.  Further, the object
  // inside each `result` represents a sync operation that can fail.
  // Conversion corresponds to running the operation.  For example, this can be
  // a good way of modeling iteration over DB rows.
  //
  // This would not compile if the fallible conversion were not implicit.  The
  // reason has to do with the fact that range-based for loops expand to this:
  //   result<int> r = *__begin;
  // Since the `=` syntax is in use, an implicit conversion is required.
  {
    std::vector<result<ConversionCanFail>> vs;
    vs.emplace_back(ConversionCanFail{8});
    vs.emplace_back(ConversionCanFail{3});
    int sum = 0;
    for (result<int> r : vs) {
      sum += co_await or_unwind(std::move(r));
    }
    EXPECT_EQ(11, sum);
  }

  // Bind `&`
  {
    result<ConversionCanFail> convOk5 = ConversionCanFail{.v_ = 5};
    result<int> ok5{convOk5};
    EXPECT_EQ(5, co_await or_unwind(std::move(ok5)));

    result<ConversionCanFail> convFail0 = ConversionCanFail{.v_ = 0};
    result<int> fail0{convFail0};
    EXPECT_EQ(std::string("zero"), get_exception<MyError>(fail0)->what());
  }

  // Bind `const &`
  {
    const result<ConversionCanFail> convOk5 = ConversionCanFail{.v_ = 5};
    result<int> ok5{convOk5};
    EXPECT_EQ(5, co_await or_unwind(std::move(ok5)));

    const result<ConversionCanFail> convFail0 = ConversionCanFail{.v_ = 0};
    result<int> fail0{convFail0};
    EXPECT_EQ(std::string("zero"), get_exception<MyError>(fail0)->what());
  }

  // Bind r-value
  {
    result<ConversionCanFail> convOk5 = ConversionCanFail{.v_ = 5};
    result<int> ok5{std::move(convOk5)};
    EXPECT_EQ(5, co_await or_unwind(std::move(ok5)));

    result<ConversionCanFail> convFail0 = ConversionCanFail{.v_ = 0};
    result<int> fail0{std::move(convFail0)};
    EXPECT_EQ(std::string("zero"), get_exception<MyError>(fail0)->what());
  }
}

void test_bad_empty_result(auto bad) {
  EXPECT_FALSE(bad.has_value());
  // `detail::empty_result_error` derives from `std::exception` but is not
  // exposed via this accessor.
  EXPECT_FALSE(get_exception<std::exception>(bad));
  if constexpr (!kIsDebug) {
    EXPECT_TRUE(get_exception<detail::empty_result_error>(bad.non_value()));
  } else {
    EXPECT_DEATH(
        (void)bad.non_value(), "`folly::result` had an empty underlying");
  }
  if constexpr (!kIsDebug) {
    EXPECT_THROW(bad.value_or_throw(), detail::empty_result_error);
  } else {
    EXPECT_DEATH(
        bad.value_or_throw(), "`folly::result` had an empty underlying");
  }
  // not default-constructible:
  decltype(bad) valRes{typename decltype(bad)::value_type{}};
  EXPECT_TRUE(valRes.has_value());
  decltype(bad) errRes = non_value_result{MyError{"fiddlesticks"}};
  EXPECT_FALSE(errRes.has_value());
  EXPECT_FALSE(valRes == bad);
  EXPECT_TRUE(valRes != bad);
  EXPECT_FALSE(errRes == bad);
  EXPECT_TRUE(errRes != bad);
  EXPECT_TRUE(bad == bad);
  auto awaitsBad = [&]() -> result<> {
    co_await or_unwind(std::as_const(bad));
  };
  if constexpr (!kIsDebug) {
    auto res = awaitsBad();
    EXPECT_TRUE(get_exception<detail::empty_result_error>(res));
  } else {
    EXPECT_DEATH((void)awaitsBad(), "`folly::result` had an empty underlying");
  }
}

// This state will no longer be possible with `std::expected`.  The tests
// use a protected ctor via FRIEND_TEST, which can then be removed.
//
// Testing `int` and `std::string` since `Expected` uses different storage for
// PoD and non-PoD types.  Separate death tests run faster.
TEST(Result, BadEmptyStateInt) {
  test_bad_empty_result(result<int>{expected_detail::EmptyTag{}});
}
TEST(Result, BadEmptyStateString) {
  test_bad_empty_result(result<std::string>{expected_detail::EmptyTag{}});
}

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wunneeded-internal-declaration")
bool is_bad_result_access(const non_value_result& nvr) {
  return get_exception<detail::bad_result_access_error>(nvr);
}
FOLLY_POP_WARNING

const char* bad_access_re =
    "Used `non_value\\(\\)` accessor for `folly::result` in value";

TEST(Result, accessValue) {
  result<int> r{555};
  EXPECT_TRUE(r.has_value());

  EXPECT_FALSE(get_exception<std::exception>(r));
  EXPECT_FALSE(get_exception<std::exception>(std::as_const(r)));

  if constexpr (!kIsDebug) {
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).non_value()));
    EXPECT_TRUE(is_bad_result_access(r.copy().non_value()));
  } else {
    EXPECT_DEATH(std::as_const(r).non_value(), bad_access_re);
    EXPECT_DEATH((void)r.copy().non_value(), bad_access_re);
  }

  EXPECT_EQ(555, r.value_or_throw());
  EXPECT_EQ(555, std::as_const(r).value_or_throw());
  EXPECT_EQ(555, std::move(r).value_or_throw());

  // `r` is moved out, so let's store a new value.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  r.value_or_throw() = 7;
  EXPECT_EQ(7, r.value_or_throw());

  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  result<int> rSame{7};
  result<int> rDiff{2};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);

  result<int> rErr{non_value_result{MyError{"hi"}}};
  EXPECT_TRUE(r != rErr);
  EXPECT_FALSE(rErr == r);
}

TEST(Result, accessLvalueRef) {
  const int n1 = 555;
  result<const int&> r{std::ref(n1)};
  EXPECT_TRUE(r.has_value());

  EXPECT_FALSE(get_exception<std::exception>(r));
  EXPECT_FALSE(get_exception<std::exception>(std::as_const(r)));

  if constexpr (!kIsDebug) {
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).non_value()));
    EXPECT_TRUE(is_bad_result_access(r.copy().non_value()));
  } else {
    EXPECT_DEATH(std::as_const(r).non_value(), bad_access_re);
    EXPECT_DEATH((void)r.copy().non_value(), bad_access_re);
  }

  EXPECT_EQ(555, r.value_or_throw());
  EXPECT_EQ(555, std::as_const(r).value_or_throw());
  EXPECT_EQ(555, std::move(r).value_or_throw());

  // `r` is moved out, so let's store a new value.
  const int n2 = 7;
  r = std::ref(n2);
  EXPECT_EQ(7, r.value_or_throw());

  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  result<const int&> rSame{std::ref(n2)};
  result<const int&> rDiff{std::ref(n1)};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);

  result<const int&> rErr{non_value_result{MyError{"hi"}}};
  EXPECT_TRUE(r != rErr);
  EXPECT_FALSE(rErr == r);
}

TEST(Result, accessRvalueRef) {
  int n1 = 555;
  result<int&&> r{rref((int&&)n1)};
  EXPECT_TRUE(r.has_value());

  EXPECT_FALSE(get_exception<std::exception>(r));
  EXPECT_FALSE(get_exception<std::exception>(std::as_const(r)));

  if constexpr (!kIsDebug) {
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).non_value()));
    EXPECT_TRUE(is_bad_result_access(std::move(r).non_value()));
  } else {
    EXPECT_DEATH(std::as_const(r).non_value(), bad_access_re);
    EXPECT_DEATH((void)std::move(r).non_value(), bad_access_re);
  }

  r = rref((int&&)n1); // satisfy the moved-out linter
  EXPECT_EQ(555, std::move(r).value_or_throw());

  int n2 = 7;
  r = rref((int&&)n2); // satisfy the moved-out linter
  EXPECT_EQ(7, std::move(r).value_or_throw());

  /* FIXME: Implement rvalue_reference_wrapper::operator==

  r = rref((int&&)n2); // was moved out, again
  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  result<int&&> rSame{rref((int&&)n2)};
  result<int&&> rDiff{rref((int&&)n1)};
  EXPECT_TRUE(r == rSame);
  EXPECT_FALSE(rSame != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);

  result<int&&> rErr{non_value_result{MyError{"hi"}}};
  EXPECT_TRUE(r != rErr);
  EXPECT_FALSE(rErr == r);
  */
}

TEST(Result, accessError) {
  result<int> r{non_value_result{MyError{"ohai"}}};
  EXPECT_FALSE(r.has_value());
  EXPECT_FALSE(r.has_stopped());

  std::string msg1{"ohai"};
  EXPECT_EQ(msg1, get_exception<MyError>(r)->what());
  EXPECT_EQ(msg1, get_exception<MyError>(std::as_const(r))->what());
  EXPECT_EQ(msg1, get_exception<MyError>(r.non_value())->what());

  // We can mutate the exception in-place.
  auto newErr = MyError{"buh-bye"};
  *get_mutable_exception<MyError>(r) = std::move(newErr);
  std::string msg2{"buh-bye"};
  EXPECT_EQ(msg2, get_exception<MyError>(r)->what());

  EXPECT_THROW(r.value_or_throw(), MyError);
  EXPECT_THROW(std::as_const(r).value_or_throw(), MyError);
  EXPECT_THROW(std::move(r).value_or_throw(), MyError);

  // `r` is moved out, so let's store a new error.
  r = non_value_result{MyError{"farewell"}};
  EXPECT_EQ(std::string("farewell"), get_exception<MyError>(r)->what());
  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  result<int> rSame1{r.non_value()};
  result<int> rSame2 = non_value_result::from_exception_wrapper(
      r.copy().non_value().to_exception_wrapper());
  result<int> rDiff{non_value_result{MyError{"farewell"}}};
  EXPECT_TRUE(r == rSame1);
  EXPECT_TRUE(r == rSame2);
  EXPECT_FALSE(rSame1 != r);
  EXPECT_FALSE(rSame2 != r);
  EXPECT_TRUE(r != rDiff);
  EXPECT_FALSE(rDiff == r);

  result<int> rVal{1337};
  EXPECT_TRUE(r != rVal);
  EXPECT_FALSE(rVal == r);
}

// Tests `U` template default in `result_promise::return_value`
RESULT_CO_TEST(Result, returnImplicitCtor) {
  auto returnImplicitCtor = []() -> result<std::pair<int, int>> {
    co_return {3, 4};
  };
  EXPECT_EQ(4, (co_await or_unwind(returnImplicitCtor())).second);
}

TEST(Result, checkCustomError) {
  auto returnsMyError = []() -> result<> {
    return non_value_result{MyError{"kthxbai"}};
  };
  auto shortCircuitOnError = [&]() -> result<std::string> {
    co_await or_unwind(returnsMyError());
    co_return "not reached";
  };
  auto r = shortCircuitOnError();
  ASSERT_EQ(std::string("kthxbai"), get_exception<>(r)->what());
  ASSERT_TRUE(get_exception<MyError>(r));
}

TEST(Result, isExceptionBoundary) {
  auto r = []() -> result<uint8_t> {
    throw MyError{"catch me"};
    co_return 129;
  }();
  EXPECT_EQ(std::string("catch me"), get_exception<MyError>(r)->what());
}

RESULT_CO_TEST(Result, awaitValue) {
  auto returnsValue = []() -> result<uint8_t> { return 129; };
  // Test both EXPECT and CO_ASSERT
  EXPECT_EQ(129, co_await or_unwind(returnsValue()));
  CO_ASSERT_EQ(129, co_await or_unwind(returnsValue()));
}

RESULT_CO_TEST(Result, awaitRef) {
  // Use a non-copiable type to show that `co_await` doesn't copy.
  result<std::unique_ptr<int>> var = std::make_unique<int>(17);
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

TEST(Result, awaitError) {
  for (auto& r :
       {[]() -> result<> { co_await non_value_result{MyError{"eep"}}; }(),
        []() -> result<> {
          co_await or_unwind(result<int>{non_value_result{MyError{"eep"}}});
        }()}) {
    EXPECT_EQ(std::string("eep"), get_exception<MyError>(r)->what());
  }
}

TEST(Result, awaitRefError) {
  auto resultErrFn = []() -> result<> {
    result<std::unique_ptr<int>> resultErr{non_value_result{MyError{"e"}}};
    co_await or_unwind(std::as_const(resultErr));
  };
  auto res = resultErrFn();
  EXPECT_TRUE(get_exception<MyError>(res));
}

TEST(Result, containsRValueRef) {
  std::string moveMe = "hello";
  auto inner = [&]() -> result<std::string&&> {
    return rref(std::move(moveMe));
  };
  auto middle = [&]() -> result<std::string&&> { co_return inner(); };
  auto outer = [&]() -> result<std::string&&> {
    co_return rref(co_await or_unwind(middle()));
  };

  auto&& moveMeRef = outer().value_or_throw();
  EXPECT_EQ("hello", moveMe);
  EXPECT_EQ("hello", moveMeRef);

  auto moved = outer().value_or_throw();
  static_assert(std::is_same_v<decltype(moved), std::string>);
  EXPECT_EQ("", moveMe);
  EXPECT_EQ("hello", moved);
}

class ResultTest : public testing::Test {
 public:
  int getMember() { return 42; }
};

RESULT_CO_TEST_F(ResultTest, check_TEST_F) {
  result<int> r{getMember()};
  EXPECT_EQ(co_await or_unwind(r), 42);
}

TEST(Result, catch_all_returns_result_error) {
  auto res = []() -> result<uint8_t> {
    return result_catch_all([]() -> result<uint8_t> {
      return non_value_result{std::logic_error{"bop"}};
    });
  }();
  ASSERT_STREQ("bop", get_exception<std::logic_error>(res)->what());
}

TEST(Result, catch_all_throws_error) {
  auto res = []() -> result<uint8_t> {
    return result_catch_all([]() -> uint8_t {
      throw std::logic_error("foobar");
    });
  }();
  ASSERT_STREQ("foobar", get_exception<std::logic_error>(res)->what());
}

TEST(Result, catch_all_throws_error_returns_result) {
  auto res = []() -> result<uint8_t> {
    return result_catch_all([]() -> result<uint8_t> {
      throw std::logic_error("foobar");
    });
  }();
  ASSERT_STREQ("foobar", get_exception<std::logic_error>(res)->what());
}

TEST(Result, catch_all_void_throws_error) {
  auto res = []() -> result<> {
    return result_catch_all([]() { throw std::logic_error("baz"); });
  }();
  ASSERT_STREQ("baz", get_exception<std::logic_error>(res)->what());
}

TEST(Result, catch_all_returns_value) {
  auto fn = []() -> result<uint8_t> {
    return result_catch_all([]() -> uint8_t { return 129; });
  };
  ASSERT_EQ(129, fn().value_or_throw());
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
