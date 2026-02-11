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

#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Traits.h>
#include <folly/coro/safe/NowTask.h>
#include <folly/result/gtest_helpers.h>
#include <folly/result/or_unwind_epitaph.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

// IMPORTANT: Changes here should PROBABLY be mirrored to
// `value_only_result_test.cpp`.

// This tests `result.h` -- `coro.h` is tested incidentally.  A full test
// matrix for `or_unwind` combinations is covered by `or_unwind_test.cpp`

namespace folly::test {

using namespace folly::detail;

class MyError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Type that always throws on move, for testing exception handling.
struct ThrowOnMove : private MoveOnly {
  int value{};
  explicit ThrowOnMove(int v) : value(v) {}
  // NOLINTNEXTLINE(performance-noexcept-move-constructor)
  [[noreturn]] ThrowOnMove(ThrowOnMove&&) { throw MyError{"move ctor"}; }
  // NOLINTNEXTLINE(performance-noexcept-move-constructor)
  [[noreturn]] ThrowOnMove& operator=(ThrowOnMove&&) {
    throw MyError{"move assignment"};
  }
};

// `result<void>` uses no storage for value
static_assert(sizeof(result<void>) == sizeof(error_or_stopped));
// `result<int>` is compact (`error_or_stopped` + padding for `int`)
static_assert(
    sizeof(result<int>) <= sizeof(error_or_stopped) +
        std::max(sizeof(int), alignof(error_or_stopped)));

// If you came here, you probably want `result` to have `->` or `*` operators,
// or `.value()`, just like `folly::Try` or `std::expected`.  This comment will
// try to dissuade you.
//
// Instead, prefer to use `co_await or_unwind()`.  You could even add a macro
// to your `.cpp` files for brevity -- for debuggability, use
// `or_unwind_epitaph`.
//
//   #define OR_UNWIND(...) (co_await or_unwind(__VA_ARGS__))
//
// So, why NOT have throwing operators?  Many teams that use `result` use it
// because they systematically want to avoid throwing, either for reasons of
// `throw` performance, or for explicitness, or because of the safety problems
// with throwing in async code (un-awaited work may lead to use-after-free).
//
// In all of those applications, adding a throwing dereference operator is very
// counterproductive, since it hides the throw site.
template <typename T>
constexpr bool gettingValueDoesNotImplicitlyThrow() {
  return !requires(T t) { *t; } && //
      !requires(T t) { t.operator->(); } && //
      !requires(T t) { t.value(); };
}
static_assert(gettingValueDoesNotImplicitlyThrow<result<int>>());
static_assert(gettingValueDoesNotImplicitlyThrow<result<void>>());
static_assert(gettingValueDoesNotImplicitlyThrow<result<void*>>());

// Smoke test to ensure we use packed `rich_exception_ptr` on 64-bit Linux.
static_assert(
    sizeof(error_or_stopped) == sizeof(void*) || !kIsLinux ||
    sizeof(void*) != 8);

// result<T>: not (yet) copyable, but movable
static_assert(!std::is_copy_constructible_v<result<int>>);
static_assert(!std::is_copy_assignable_v<result<int>>);
static_assert(std::is_move_constructible_v<result<int>>);
static_assert(std::is_move_assignable_v<result<int>>);

// result<T&>: not (yet) copyable, but movable
//
// WARNING: When adding copyability, forbid copies from `const result<T&>&`,
// details in `DefineMovableDeepConstLrefCopyable.h`.
static_assert(!std::is_copy_constructible_v<result<int&>>);
static_assert(!std::is_copy_assignable_v<result<int&>>);
static_assert(std::is_move_constructible_v<result<int&>>);
static_assert(std::is_move_assignable_v<result<int&>>);

// result<T&&> will always be move-only, following `rvalue_reference_wrapper`
static_assert(!std::is_copy_constructible_v<result<int&&>>);
static_assert(!std::is_copy_assignable_v<result<int&&>>);
static_assert(std::is_move_constructible_v<result<int&&>>);
static_assert(std::is_move_assignable_v<result<int&&>>);

// result propagates noexcept from the value type
static_assert(std::is_nothrow_move_constructible_v<result<int>>);
static_assert(std::is_nothrow_move_assignable_v<result<int>>);
static_assert(std::is_move_constructible_v<ThrowOnMove>);
static_assert(!std::is_nothrow_move_constructible_v<ThrowOnMove>);
static_assert(std::is_move_assignable_v<ThrowOnMove>);
static_assert(!std::is_nothrow_move_assignable_v<ThrowOnMove>);

// Simple conversions between compatible result types
static_assert(std::is_constructible_v<result<int>, result<long>>);
static_assert(std::is_constructible_v<result<float>, result<int>>);
static_assert(
    std::is_constructible_v<result<std::string>, result<const char*>>);
static_assert(std::is_constructible_v<result<const int&>, result<int&>>);

// Conversion correctly reflects value category of Arg, not hardcoded &&.
// `RvalueOnly(int&&)` is constructible from rvalue `int` but not lvalue.
struct RvalueOnly {
  explicit RvalueOnly(int&&) {}
};
static_assert(std::is_constructible_v<result<RvalueOnly>, result<int>&&>);
static_assert(!std::is_constructible_v<result<RvalueOnly>, result<int>&>);

// Conversions that CANNOT happen:

// - `result<T>` has no default ctor (intentional), but `result<void>` does
static_assert(!std::is_default_constructible_v<result<int>>);
static_assert(std::is_default_constructible_v<result<void>>);

// - Unrelated types are properly rejected
static_assert(!std::is_constructible_v<result<int>, std::vector<int>>);
static_assert(!std::is_constructible_v<result<int>, int*>);

// - Incompatible value types should not match the fallible conversion.
static_assert(!std::is_constructible_v<result<int>, result<std::string>>);

// - Cannot silently discard values when converting to result<void>.
static_assert(!std::is_constructible_v<result<void>, result<int>>);

// - Non-result types with `storage_type` member should not match.
struct FakeResultLike {
  using storage_type = std::string;
  bool has_value() const { return true; }
  std::string value_or_throw() const { return "fake"; }
  error_or_stopped error_or_stopped() const {
    return folly::error_or_stopped{stopped_result};
  }
};
static_assert(!std::is_constructible_v<result<int>, FakeResultLike>);

// Test self-move-assignment without triggering `-Wself-move`.
template <typename T>
void selfMove(T& x) {
  x = std::move(x);
}

// Fully tests `void`-specific behaviors.  Loosely covers common features from
// `result_crtp` -- they're covered in-depth by the non-`void` tests below.
TEST(Result, resultOfVoid) {
  // Cover the handful of things specific to the `result<void>` specialization,
  // plus copyability & movability.
  {
    static_assert(!std::is_copy_constructible_v<result<>>);
    static_assert(!std::is_copy_assignable_v<result<>>);

    // Exception-safety of result's move assignment relies on error_or_stopped
    // being nothrow movable (so eos_ updates in move operations never throw).
    static_assert(std::is_nothrow_move_constructible_v<error_or_stopped>);
    static_assert(std::is_nothrow_move_assignable_v<error_or_stopped>);

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
    result<> r(error_or_stopped{MyError{"soup"}}); // ctor
    EXPECT_STREQ("soup", get_exception<MyError>(r)->what());
    EXPECT_STREQ("soup", get_exception<MyError>(std::as_const(r))->what());
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(r.has_stopped());

    r = error_or_stopped{MyError{"cake"}}; // assignment
    EXPECT_STREQ("cake", get_exception<MyError>(r)->what());
  }
  { // `result<void>` coro with "error" and "success" exit paths.
    auto voidResFn = [](bool fail) -> result<> {
      if (!fail) {
        co_return;
      }
      co_await or_unwind(error_or_stopped{MyError{"failed"}});
    };
    EXPECT_TRUE(voidResFn(false).has_value());
    auto r = voidResFn(true);
    EXPECT_STREQ("failed", get_exception<MyError>(r)->what());
  }
  { // A convert-to-`result<void>` analog of `ConversionCanFail` below
    struct ConvertToResultVoid {
      /*implicit*/ operator result<void>() const {
        if (fail_) {
          return error_or_stopped{MyError{"failed"}};
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
          (void)error_or_stopped::from_exception_ptr_slow(std::exception_ptr{});
        },
        "`result` may not contain an empty `std::exception_ptr`");
  } else {
    EXPECT_FALSE(
        error_or_stopped::from_exception_ptr_slow(std::exception_ptr{})
            .to_exception_ptr_slow());
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
    selfMove(r);
    EXPECT_TRUE(r.has_stopped());
    // Using `exception_ptr`-like accessors when `has_stopped()` is debug-fatal
    if (kIsDebug) {
      EXPECT_DEATH(
          { (void)std::move(r).error_or_stopped().to_exception_ptr_slow(); },
          deathRe);
    } else {
      auto ew = std::move(r).error_or_stopped().to_exception_ptr_slow();
      EXPECT_TRUE(get_exception<OperationCancelled>(ew));
    }
  };
  check(tag<void>, stopped_result);
  check(tag<int>, stopped_result);
  auto ocEw = make_exception_wrapper<OperationCancelled>();
  auto stoppedNvr = error_or_stopped::make_legacy_error_or_cancellation_slow(
      result_private_t{}, ocEw);
  check(tag<void>, stoppedNvr);
  check(tag<int>, stoppedNvr);
  // Constructing with `OperationCancelled` without the legacy path
  // is debug-fatal.
  if (kIsDebug) {
    EXPECT_DEATH(
        {
          (void)error_or_stopped::from_exception_ptr_slow(ocEw.exception_ptr());
        },
        deathRe);
  } else {
    auto eptr =
        error_or_stopped::from_exception_ptr_slow(ocEw.exception_ptr())
            .to_exception_ptr_slow();
    EXPECT_TRUE(get_exception<OperationCancelled>(eptr));
  }
}

TEST(Result, awaitStoppedResult) {
  auto innerFn = []() -> result<> {
    co_await or_unwind(stopped_result);
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
  selfMove(rVoid);
  EXPECT_TRUE(rVoid.has_value());

  result<std::unique_ptr<int>> mIntPtrSrc = std::make_unique<int>(1337);
  auto mIntPtr = std::move(mIntPtrSrc);
  EXPECT_EQ(1337, *mIntPtr.value_or_throw());
  selfMove(mIntPtr);
  EXPECT_EQ(1337, *mIntPtr.value_or_throw());
}

TEST(Result, refCopiable) {
  auto intPtr = std::make_unique<int>(1337);
  result mIntPtrRef1 = std::ref(intPtr);
  static_assert(
      std::is_same_v<result<std::unique_ptr<int>&>, decltype(mIntPtrRef1)>);
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

  result<int> rErr{error_or_stopped{MyError{"grr"}}};
  auto rErrToo = rErr.copy();
  EXPECT_EQ(rErr.error_or_stopped(), rErrToo.error_or_stopped());
  EXPECT_TRUE(rErr == rErrToo);

  EXPECT_TRUE(rErr != r);
  EXPECT_TRUE(rErrToo != rToo);
}

// `stopped_result` is covered separately
TEST(Result, fromErrorOrStopped) {
  error_or_stopped eos{MyError{"nay"}};
  {
    result<int> r{eos}; // ctor
    EXPECT_EQ(std::string("nay"), get_exception<MyError>(r)->what());
    r = copy(eos); // assignment
    EXPECT_EQ(std::string("nay"), get_exception<MyError>(r)->what());
    selfMove(r);
    EXPECT_EQ(std::string("nay"), get_exception<MyError>(r)->what());
  }
  eos = error_or_stopped{MyError{"nein"}};
  {
    auto r = [&eos]() -> result<> {
      co_await or_unwind(std::move(eos)); // await
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
    result<std::reference_wrapper<int>> r = std::ref(n);
    // The `.get()` is here to show that a ref-wrapper is being returned.
    EXPECT_EQ(3, (co_await or_unwind(std::move(r))).get());
  }
  // To store `result<V&>`, you can use CTAD
  {
    result r = std::ref(n);
    static_assert(std::is_same_v<result<int&>, decltype(r)>);
    EXPECT_EQ(3, co_await or_unwind(std::move(r)));
  }
}

RESULT_CO_TEST(Result, ofRvalueReferenceWrapper) {
  // Yes, you can declare `result<rvalue_reference_wrapper<V>>`.
  {
    int n = 3;
    result<rvalue_reference_wrapper<int>> r = rref(std::move(n));
    // The `.get()` is here to show that a ref-wrapper is being returned.
    EXPECT_EQ(3, (co_await or_unwind(std::move(r))).get());
  }
  // To store `result<V&&>`, you can use CTAD
  {
    int n = 3;
    result r = rref(std::move(n));
    static_assert(std::is_same_v<result<int&&>, decltype(r)>);
    EXPECT_EQ(3, co_await or_unwind(std::move(r)));
  }
}

RESULT_CO_TEST(Result, forbidUnsafeCopyOfResultRef) {
  int n = 42;

  result rc = std::cref(n);
  static_assert(std::is_same_v<result<const int&>, decltype(rc)>);
  { // Safe copies of ref -- `rc` has `const` inside, cannot be discarded
    result rc2 = rc.copy();
    EXPECT_EQ(42, (co_await or_unwind(rc2)));
    result rc3 = std::as_const(rc).copy();
    EXPECT_EQ(42, (co_await or_unwind(rc3)));
  }
  static_assert(requires { rc.copy(); });
  static_assert(requires { std::as_const(rc).copy(); });

  result<int&> r = std::ref(n);
  { // Safe copy of ref -- `r` has no `const` to discard
    result r2 = r.copy();
    EXPECT_EQ(42, (co_await or_unwind(r2)));
  }
  // Unsafe: copying `const result<int&>` would discard the outer `const`
  //   result r3 = std::as_const(r).copy();
  // The next assert shows the above `.copy()` is SFINAE-deleted.
  //
  // NB: This `requires` won't compile without using a dependent type.
  static_assert(![](const auto& r2) { return requires { r2.copy(); }; }(r));
  // Copy-from-mutable still works
  static_assert([](auto& r2) { return requires { r2.copy(); }; }(r));
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
    selfMove(rLref);
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

// FIXME: This test is aspirational for after the `result`-without-`Expected`
// refactor -- the "Sad trombone" sections show the current bad-empty state.
//
// Unlike `folly::Expected` or `std::variant`, `result` never becomes
// "valueless by exception" because we construct the new value before updating
// `eos_`.  If construction throws, `eos_` remains in its original state.
TEST(Result, throwingMove) {
  struct ThrowOnNegative : private MoveOnly {
    int value;
    explicit ThrowOnNegative(int v) : value(v) {}
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    ThrowOnNegative(ThrowOnNegative&& o) : value(o.value) {
      if (value < 0) {
        throw MyError{"move threw"};
      }
    }
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    ThrowOnNegative& operator=(ThrowOnNegative&& o) {
      if (o.value < 0) {
        throw MyError{"move assign threw"};
      }
      value = o.value;
      return *this;
    }
  };

  { // Move constructor: can throw, exception propagates
    result src{ThrowOnNegative{42}};
    src.value_or_throw().value = -1;
    EXPECT_THROW(result<ThrowOnNegative> dest{std::move(src)}, MyError);
  }
  { // Move assignment: error to value, stays in error on throw
    result src{ThrowOnNegative{42}};
    src.value_or_throw().value = -1;
    result<ThrowOnNegative> dest{error_or_stopped{MyError{"initial error"}}};
    EXPECT_STREQ("initial error", get_exception<MyError>(dest)->what());

    EXPECT_THROW(dest = std::move(src), MyError);

    // After throw, dest should still be in original error state
    EXPECT_FALSE(dest.has_value());
    EXPECT_STREQ("initial error", get_exception<MyError>(dest)->what());
  }
  { // Move assignment: value to value, throws but stays in value state
    result src{ThrowOnNegative{42}};
    src.value_or_throw().value = -1;
    result<ThrowOnNegative> dest{ThrowOnNegative{10}};

    EXPECT_THROW(dest = std::move(src), MyError);

    EXPECT_EQ(10, dest.value_or_throw().value);
  }
  { // Successful move (non-throwing case)
    result src{ThrowOnNegative{42}};
    result<ThrowOnNegative> dest{error_or_stopped{MyError{"initial error"}}};

    dest = std::move(src);

    EXPECT_EQ(42, dest.value_or_throw().value);
  }
  { // Value assignment from error state, stays in error on throw
    result<ThrowOnNegative> r{error_or_stopped{MyError{"initial error"}}};
    ThrowOnNegative val{-1};

    EXPECT_THROW(r = std::move(val), MyError);

    // After throw, r should still be in original error state
    EXPECT_FALSE(r.has_value());
    EXPECT_STREQ("initial error", get_exception<MyError>(r)->what());
  }
}

// Test that throwing during co_return (in return_value's placement new) is
// correctly caught by unhandled_exception and produces an error result.
TEST(Result, throwingMoveInCoReturn) {
  auto throwingCoro = []() -> result<ThrowOnMove> {
    ThrowOnMove val{42};
    co_return val; // Will throw during `return_value`'s placement `new`
  };
  // The exception from placement `new` in `return_value` should be caught by
  // `unhandled_exception`, which sets `eos_` to error state.
  auto r = throwingCoro();
  EXPECT_FALSE(r.has_value());
  EXPECT_STREQ("move ctor", get_exception<MyError>(r)->what());
}

// Test co_return of result<T> -- the result unwrapping path in return_value.
// Unwrapping only happens when co_return type exactly matches the coro type.
TEST(Result, coReturnResultUnwrapping) {
  { // co_return result<T> with value; unwraps and forwards the value
    auto r = []() -> result<int> { co_return result{42}; }();
    EXPECT_EQ(42, r.value_or_throw());
  }
  { // co_return result<T> with error; forwards the error
    auto r = []() -> result<int> {
      co_return result<int>{error_or_stopped{MyError{"inner error"}}};
    }();
    EXPECT_STREQ("inner error", get_exception<MyError>(r)->what());
  }
  // co_return result<T> where extracting value throws -- caught by
  // unhandled_exception (tests value_or_throw() in return_value)
  {
    auto r = []() -> result<ThrowOnMove> {
      co_return result{ThrowOnMove{42}};
    }();
    EXPECT_STREQ("move ctor", get_exception<MyError>(r)->what());
  }
}

// Test that co_return of result<U> where U != T does NOT unwrap.
// This is important for result<result<T>> -- the inner result should be
// stored as a value, not unwrapped (which would incorrectly propagate errors).
TEST(Result, coReturnNestedResultNoUnwrap) {
  { // Inner result with value -- stored as value, not unwrapped
    auto r = []() -> result<result<int>> { co_return result{42}; }();
    EXPECT_EQ(42, r.value_or_throw().value_or_throw());
  }
  { // Inner result with error -- stored as value, error does NOT propagate
    auto r = []() -> result<result<int>> {
      co_return result<int>{error_or_stopped{MyError{"inner error"}}};
    }();
    EXPECT_TRUE(r.has_value()); // outer has value, but ...
    EXPECT_STREQ( // ... inner has error
        "inner error", get_exception<MyError>(r.value_or_throw())->what());
  }
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
      auto err = error_or_stopped{MyError{"foo"}};
      return err;
    };
    auto res = fn();
    EXPECT_STREQ("foo", get_exception<MyError>(res)->what());
  }
  {
    auto fn = []() -> result<std::unique_ptr<int>> {
      auto err = error_or_stopped{MyError{"foo"}};
      co_return err;
    };
    auto res = fn();
    EXPECT_STREQ("foo", get_exception<MyError>(res)->what());
  }
}

// It is easy to mis-implement `result_promise::return_value` so that it plumbs
// the value category of the value / error_or_stopped incorrectly.  The worst
// failure would be if `co_return`ing an lvalue moved from the original.
TEST(Result, coReturnLvalueResultDoesNotMove) {
  // `hasError(...)` / `hasValue(...)` compare `res` with an `co_return lvalue`
  // coming from an inline lambda coro.  These trigger copies, since
  // reference-captures are not implicitly movable.
  {
    result<int> res{error_or_stopped{MyError{"original error"}}};
    auto hasError = [](const result<int>& r) {
      EXPECT_FALSE(r.has_value());
      EXPECT_STREQ("original error", get_exception<MyError>(r)->what());
    };
    // Both the returned and original results have the error.
    hasError([&res]() -> result<int> { co_return res; }());
    hasError(res);
  }
  { // shared_ptr is copyable and becomes null when moved-from.
    auto ptr = std::make_shared<int>(42);
    result<std::shared_ptr<int>> res{copy(ptr)};
    auto hasValue = [&ptr](const result<std::shared_ptr<int>>& r) {
      EXPECT_EQ(ptr, r.value_or_throw());
    };
    // Both the returned and original results share the pointer.
    hasValue([&res]() -> result<std::shared_ptr<int>> { co_return res; }());
    hasValue(res);
  }
}

// Check that `co_return lvalue_result<T&>` preserves const-correctness, like
// the `.copy()` method constraints in `forbidUnsafeCopyOfResultRef`.
//
// The concern: `const result<int&>` only gives `const int&` access. If we could
// `co_return` a const lvalue `result<int&>`, we'd get a new `result<int&>` with
// mutable `int&` access, violating const-correctness.
//
// The implementation prevents this: `value_or_throw() const& -> const int&`,
// but `std::reference_wrapper<int>` is not constructible from `const int&`.
TEST(Result, coReturnLvalueResultRefConstCorrectness) {
  int n = 42;
  { // OK to copy mutable `result<int&>`
    result<int&> r = std::ref(n);
    auto r2 = [](result<int&>& ref) -> result<int&> { co_return ref; }(r);
    EXPECT_EQ(42, r2.value_or_throw());
    r2.value_or_throw() = 100;
    EXPECT_EQ(100, n);
    n = 42; // restore
  }

  auto testConstLvalueCoReturn = [&]<typename T>(tag_t<T>) {
    using RefWrap = std::conditional_t<
        std::is_const_v<std::remove_reference_t<T>>,
        std::reference_wrapper<const int>,
        std::reference_wrapper<int>>;
    result<T> r{RefWrap{n}};
    // The `const` here is what causes the "manual test" to be a build error.
    return [](const result<T>& ref) -> result<T> { co_return ref; }(r);
  };

  // OK to copy `const result<const int&>` -- inner const is preserved
  EXPECT_EQ(42, testConstLvalueCoReturn(tag<const int&>).value_or_throw());

#if 0 // Manual test -- verifies const-correctness violation doesn't compile
  // Cannot copy `const result<int&>` -- would return mutable `result<int&>`
  // error: no matching constructor for 'reference_wrapper<int>' from 'const int'
  EXPECT_EQ(42, testConstLvalueCoReturn(tag<int&>).value_or_throw());
#endif
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

// Document narrowing/widening behaviors of `result`'s simple conversion ctor
// (it mentions `conversionTypes`).  If that ctor had {}-initialization instead
// of (), then narrowing conversions that work today would break.
CO_TEST(Result, conversionTypes) {
  // short -> int: Widening
  result<short> rShort{static_cast<short>(42)};
  EXPECT_EQ(42, result<int>{std::move(rShort)}.value_or_throw());

  // int -> float: Narrowing because large ints may lose precision.
  result<int> rInt{1000};
  EXPECT_EQ(1000.0f, result<float>{std::move(rInt)}.value_or_throw());

  // result coroutine `co_return` allows narrowing
  EXPECT_EQ(129, []() -> result<uint8_t> { co_return 129; }().value_or_throw());

  // Comparison: plain functions and task coros also allow narrowing.
  EXPECT_EQ(129, []() -> uint8_t { return 129; }());
  EXPECT_EQ(129, co_await []() -> coro::now_task<uint8_t> { co_return 129; }());
}

// Test error/stopped state propagation through type conversions.
TEST(Result, conversionPropagatesErrorAndStopped) {
  { // "Error" propagates through result<int> -> result<float> conversion
    result<float> r{result<int>{error_or_stopped{MyError{"propagate"}}}};
    EXPECT_STREQ("propagate", get_exception<MyError>(r)->what());
  }
  { // "Stopped" propagates through result<int> -> result<float> conversion
    result<float> r{result<int>{stopped_result}};
    EXPECT_TRUE(r.has_stopped());
  }
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
        return error_or_stopped{MyError{"zero"}};
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

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wunneeded-internal-declaration")
bool is_bad_result_access(const error_or_stopped& eos) {
  return bool{get_exception<bad_result_access_error>(eos)};
}
FOLLY_POP_WARNING

[[maybe_unused]] const char* const bad_access_re =
    "Used `error_or_stopped\\(\\)` accessor for `folly::result` in value";

TEST(Result, accessValue) {
  result<int> r{555};
  EXPECT_TRUE(r.has_value());

  EXPECT_FALSE(get_exception<std::exception>(r));
  EXPECT_FALSE(get_exception<std::exception>(std::as_const(r)));

  if constexpr (!kIsDebug) {
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).error_or_stopped()));
    EXPECT_TRUE(is_bad_result_access(r.copy().error_or_stopped()));
  } else {
    EXPECT_DEATH(std::as_const(r).error_or_stopped(), bad_access_re);
    EXPECT_DEATH((void)r.copy().error_or_stopped(), bad_access_re);
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

  result<int> rErr{error_or_stopped{MyError{"hi"}}};
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
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).error_or_stopped()));
    EXPECT_TRUE(is_bad_result_access(r.copy().error_or_stopped()));
  } else {
    EXPECT_DEATH(std::as_const(r).error_or_stopped(), bad_access_re);
    EXPECT_DEATH((void)r.copy().error_or_stopped(), bad_access_re);
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

  result<const int&> rErr{error_or_stopped{MyError{"hi"}}};
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
    EXPECT_TRUE(is_bad_result_access(std::as_const(r).error_or_stopped()));
    EXPECT_TRUE(is_bad_result_access(std::move(r).error_or_stopped()));
  } else {
    EXPECT_DEATH(std::as_const(r).error_or_stopped(), bad_access_re);
    EXPECT_DEATH((void)std::move(r).error_or_stopped(), bad_access_re);
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

  result<int&&> rErr{error_or_stopped{MyError{"hi"}}};
  EXPECT_TRUE(r != rErr);
  EXPECT_FALSE(rErr == r);
  */
}

TEST(Result, accessError) {
  result<int> r{error_or_stopped{MyError{"ohai"}}};
  EXPECT_FALSE(r.has_value());
  EXPECT_FALSE(r.has_stopped());

  std::string msg1{"ohai"};
  EXPECT_EQ(msg1, get_exception<MyError>(r)->what());
  EXPECT_EQ(msg1, get_exception<MyError>(std::as_const(r))->what());
  EXPECT_EQ(msg1, get_exception<MyError>(r.error_or_stopped())->what());

  // We can mutate the exception in-place.
  auto newErr = MyError{"buh-bye"};
  *get_mutable_exception<MyError>(r) = std::move(newErr);
  std::string msg2{"buh-bye"};
  EXPECT_EQ(msg2, get_exception<MyError>(r)->what());

  EXPECT_THROW((void)r.value_or_throw(), MyError);
  EXPECT_THROW((void)std::as_const(r).value_or_throw(), MyError);
  EXPECT_THROW((void)std::move(r).value_or_throw(), MyError);

  // `r` is moved out, so let's store a new error.
  r = error_or_stopped{MyError{"farewell"}};
  EXPECT_EQ(std::string("farewell"), get_exception<MyError>(r)->what());
  EXPECT_TRUE(r == r);
  EXPECT_FALSE(r != r);

  result<int> rSame1{r.error_or_stopped()};
  result<int> rSame2 = error_or_stopped::from_exception_ptr_slow(
      r.copy().error_or_stopped().to_exception_ptr_slow());
  result<int> rDiff{error_or_stopped{MyError{"farewell"}}};
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

// Test that result<T> works as a coroutine return type for
// non-default-constructible T.
RESULT_CO_TEST(Result, nonDefaultConstructible) {
  struct NoDefaultCtor {
    int value;
    explicit NoDefaultCtor(int v) : value(v) {}
  };
  static_assert(!std::is_default_constructible_v<NoDefaultCtor>);

  { // result_promise::return_value(T)
    auto r = []() -> result<NoDefaultCtor> { co_return NoDefaultCtor{42}; }();
    EXPECT_EQ(42, (co_await or_unwind(r)).value);
  }

  { // result_promise::return_value(error_or_stopped)
    auto r = []() -> result<NoDefaultCtor> {
      co_return error_or_stopped{MyError{"error path"}};
    }();
    EXPECT_STREQ("error path", get_exception<MyError>(r)->what());
  }

  { // result_promise::unhandled_exception
    auto r = []() -> result<NoDefaultCtor> {
      throw MyError{"oops"};
      co_return NoDefaultCtor{5}; // only a coro is an exception boundary
    }();
    EXPECT_STREQ("oops", get_exception<MyError>(r)->what());
  }
}

TEST(Result, checkCustomError) {
  auto returnsMyError = []() -> result<> {
    return error_or_stopped{MyError{"kthxbai"}};
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
       {[]() -> result<> {
          co_await or_unwind(error_or_stopped{MyError{"eep"}});
        }(),
        []() -> result<> {
          (void)co_await or_unwind(
              result<int>{error_or_stopped{MyError{"eep"}}});
        }()}) {
    EXPECT_EQ(std::string("eep"), get_exception<MyError>(r)->what());
  }
}

TEST(Result, awaitRefError) {
  auto resultErrFn = []() -> result<> {
    result<std::unique_ptr<int>> resultErr{error_or_stopped{MyError{"e"}}};
    (void)co_await or_unwind(std::as_const(resultErr));
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
      return error_or_stopped{std::logic_error{"bop"}};
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

// Even though `error_or_stopped` is a `rich_exception_ptr`, the latter can
// still be used as a value type.
TEST(Result, of_rich_exception_ptr) {
  result<rich_exception_ptr> rVal{rich_exception_ptr{MyError{"rep"}}};
  EXPECT_TRUE(rVal.has_value());
  EXPECT_STREQ("rep", get_exception<MyError>(rVal.value_or_throw())->what());

  result<rich_exception_ptr> rErr{error_or_stopped{MyError{"err"}}};
  EXPECT_FALSE(rErr.has_value());
  EXPECT_STREQ("err", get_exception<MyError>(rErr)->what());
}

// This was more interesting when `error_or_stopped` wrapped
// `exception_wrapper`
TEST(Result, of_exception_wrapper) {
  result<exception_wrapper> rVal{make_exception_wrapper<MyError>("ew")};
  EXPECT_TRUE(rVal.has_value());
  EXPECT_EQ("folly::test::MyError: ew", rVal.value_or_throw().what());

  result<exception_wrapper> rErr{error_or_stopped{MyError{"err"}}};
  EXPECT_FALSE(rErr.has_value());
  EXPECT_STREQ("err", get_exception<MyError>(rErr)->what());
}

// Minimal test for error_or_stopped fmt/ostream formatting.
// See `rich_exception_ptr_fmt_test.cpp` for comprehensive formatting tests.
TEST(Result, error_or_stopped_format) {
  auto line = source_location::current().line() + 1;
  auto eos = epitaph(error_or_stopped{stopped_result}, "ctx");
  checkFormat(
      eos,
      fmt::format(
          "folly::OperationCancelled: coroutine operation cancelled"
          " \\[via\\] ctx @ {}:{}",
          source_location::current().file_name(),
          line));
}

// To examine RESULT_CO_TEST failure messages:
//   buck run test:result_test -- --gtest_also_run_disabled_tests
RESULT_CO_TEST(Result, DISABLED_checkEpitaphErrorMessage) {
  co_await or_unwind_epitaph(error_or_stopped{MyError{"inner error"}}, "ctx");
}

RESULT_CO_TEST(Result, DISABLED_checkEpitaphStoppedMessage) {
  co_await or_unwind_epitaph(error_or_stopped{stopped_result}, "ctx");
}

} // namespace folly::test

#endif // FOLLY_HAS_RESULT
