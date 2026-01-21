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
#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/ValueOrError.h>
#include <folly/coro/safe/NowTask.h>
#include <folly/lang/MustUseImmediately.h>
#include <folly/lang/RValueReferenceWrapper.h>
#include <folly/portability/GTest.h>
#include <folly/result/coro.h>
#include <folly/result/value_only_result_coro.h>

// After some ad-hoc manual tests, this has a test matrix covering:
// - Awaiter: `or_unwind` (&, const&, &&), `or_unwind_owning`
// - Context: `result` coroutine, `now_task` coroutine
// - Inner type: `void`, `int` and refs, `string`, `unique_ptr` and refs
// - Result-like container: `result`, `value_only_result`, `non_value_result`
// - States: value, error, stopped

#if FOLLY_HAS_RESULT

namespace folly {
namespace {

using ext::must_use_immediately_unsafe_mover;

template <template <typename> class ResultT>
result<void> orUnwindOfTemporaryDangles() {
  auto returnTemp = [] { return ResultT<std::string>{"hello_dangling_test"}; };
  {
    // FIXME: This manual test actual FAILS with an ASAN error today, whereas
    // the design intent was for this to fail the build with `-Wdangling`.
    // Search `result.md` for "LLVM issue #177023" for details.
#if 0 // BAD: temporary destroyed on the first line, `ref` dangles
    auto&& ref = co_await or_unwind(returnTemp());
#else // OK: `ref` points into stored `res`.
    auto res = returnTemp();
    auto&& ref = co_await or_unwind(std::move(res));
#endif
    EXPECT_EQ("hello_dangling_test", ref);
  }
  { // OK: Extract by value, no dangling reference.
    auto val = co_await or_unwind(returnTemp());
    EXPECT_EQ("hello_dangling_test", val);
  }
  { // AVOID: Works due to lifetime extension, non-obvious.
    auto&& ref = co_await or_unwind_owning(returnTemp());
    EXPECT_EQ("hello_dangling_test", ref);
  }
}

// Verify the dangling reference is caught by ASAN (with heap-allocated string).
TEST(OrUnwind, temporaryDangles) {
  orUnwindOfTemporaryDangles<result>().value_or_throw();
  orUnwindOfTemporaryDangles<value_only_result>().value_or_throw();
}

// Async generators do NOT support `or_unwind`, because `or_unwind` aborts
// the coroutine, whereas generators continue after yielding an error.
[[maybe_unused]] coro::AsyncGenerator<int> noAsyncGeneratorSupport() {
// Manual test: should fail to compile with something like
//   error: no matching member function for call to 'await_suspend'
#if 0
  co_await or_unwind(result<int>{42});
#endif
  co_yield 1;
}

// Check that an awaitable's `await_resume()` returns the expected type.
template <typename Expected, typename Awaitable>
consteval void checkAwaitResumeType() {
  static_assert(
      std::is_same_v<
          Expected,
          decltype(std::declval<Awaitable>().await_resume())>);
}

// Check `await_resume()` types for rvalue-only types (e.g. `non_value_result`).
template <typename ResultT, typename RvalueT, typename OwningT>
consteval void checkRvalueAwaitResumeTypes() {
  checkAwaitResumeType<RvalueT, decltype(or_unwind(FOLLY_DECLVAL(ResultT)))>();
  checkAwaitResumeType<
      OwningT,
      decltype(or_unwind_owning(FOLLY_DECLVAL(ResultT)))>();
}

// Check `await_resume()` return types for all ref variants.
template <
    typename ResultT,
    typename LvalueT,
    typename ConstLvalueT,
    typename RvalueT,
    typename OwningT>
consteval void checkAllAwaitResumeTypes() {
  checkAwaitResumeType<LvalueT, decltype(or_unwind(FOLLY_DECLVAL(ResultT&)))>();
  checkAwaitResumeType<
      ConstLvalueT,
      decltype(or_unwind(FOLLY_DECLVAL(const ResultT&)))>();
  checkRvalueAwaitResumeTypes<ResultT, RvalueT, OwningT>();
}

struct InResult { // Test in `result` coro context
  template <typename T>
  using Coro = result<T>;

  template <typename F>
  static auto run(F&& fn) {
    return fn();
  }
};

struct InTask { // Test in `now_task` coro context
  template <typename T>
  using Coro = coro::now_task<T>;

  template <typename F>
  static auto run(F&& fn) {
    return coro::blockingWait(coro::value_or_error_or_stopped(fn()));
  }
};

// Make a result in a value state.
template <typename ResultT, typename MakeInner>
ResultT makeResultWithValue(MakeInner&& makeInner) {
  using T = typename ResultT::value_type;
  if constexpr (std::is_void_v<T>) {
    return ResultT{};
  } else if constexpr (std::is_rvalue_reference_v<T>) {
    // `rvalue_reference_wrapper` must be explicitly constructed.
    return ResultT{rref(makeInner())};
  } else {
    // Handle inner value type, or lvalue ref (`std::ref(t)` or `t`) --
    // `std::reference_wrapper<T>` is implicitly constructible from `T&`.
    return ResultT{makeInner()};
  }
}

// An empty `non_value_result` made from an empty `exception_wrapper`.
inline non_value_result emptyNonValue() {
  return non_value_result::make_legacy_error_or_cancellation_slow(
      detail::result_private_t{}, exception_wrapper{});
}

// Test all supported `or_unwind...` variants for the given result type.
template <typename Context, typename MakeResult, typename Check>
void forEachOrUnwindVariant(MakeResult makeResult, Check&& check) {
  auto val = makeResult();
  bool hasValue = false;
  if constexpr (requires { val.has_value(); }) {
    hasValue = val.has_value(); // Ward against `or_unwind` mutating it
  }
  // By lvalue ref, only for copyable results
  if constexpr (requires { val.copy(); }) {
    check(tag_t<Context>{}, or_unwind(val));
    check(tag_t<Context>{}, or_unwind(std::as_const(val)));
    // Lvalue refs must NOT mutate the original (error is copied, not moved).
    // Users don't expect `co_await or_unwind(r)` to mutate `r`.
    if constexpr (requires { val.non_value(); }) {
      EXPECT_TRUE(hasValue || (emptyNonValue() != val.non_value()));
    }
  }
  check(tag_t<Context>{}, or_unwind_owning(makeResult()));
  // By rvalue ref -- error SHOULD be moved out.
  check(tag_t<Context>{}, or_unwind(std::move(val)));
  if constexpr (requires { val.non_value(); }) {
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_TRUE(hasValue || (emptyNonValue() == val.non_value()));
  }
}

// Check value extraction via `co_await`.
template <typename T>
struct CheckExtract {
  [[no_unique_address]] std::conditional_t<std::is_void_v<T>, std::monostate, T>
      expected;

  template <typename Context>
  void operator()(tag_t<Context>, auto&& awaitable) const {
    auto out = Context::run([&]() -> typename Context::template Coro<T> {
      // Since `result` is not copy-constructible, the cast is needed to force
      // a copy from the reference returned by `co_await`.
      co_return static_cast<T>(co_await must_use_immediately_unsafe_mover(
          static_cast<decltype(awaitable)>(awaitable))());
    });
    ASSERT_TRUE(out.has_value());
    if constexpr (!std::is_void_v<T>) {
      EXPECT_EQ(expected, std::move(out).value_or_throw());
    }
  }
};

// Check that `co_await` returns a reference to the expected address.
template <typename T>
struct CheckExtractRef {
  const T* expected;

  template <typename Context>
  void operator()(tag_t<Context>, auto&& awaitable) const {
    auto out = Context::run([&]() -> typename Context::template Coro<const T*> {
      auto&& ref = co_await must_use_immediately_unsafe_mover(
          static_cast<decltype(awaitable)>(awaitable))();
      co_return &ref;
    });
    EXPECT_EQ(expected, std::move(out).value_or_throw());
  }
};

// Test non-value propagation across all applicable variants.
template <typename MakeNonValue, typename Verify>
void testNonValue(MakeNonValue makeNonValue, Verify verify) {
  forEachOrUnwindVariant<InResult>(
      makeNonValue, [&](tag_t<InResult>, auto&& awaitable) {
        auto out = InResult::run([&]() -> InResult::Coro<void> {
          (void)co_await must_use_immediately_unsafe_mover(
              static_cast<decltype(awaitable)>(awaitable))();
        });
        verify(out);
      });
  forEachOrUnwindVariant<InTask>(
      makeNonValue, [&](tag_t<InTask>, auto&& awaitable) {
        auto out = InTask::run([&]() -> InTask::Coro<void> {
          (void)co_await must_use_immediately_unsafe_mover(
              static_cast<decltype(awaitable)>(awaitable))();
        });
        verify(out);
      });
}

template <typename... Makes>
void testNonValueErrors(Makes... makes) {
  auto verify = [](auto& out) {
    EXPECT_TRUE(!out.has_value() && !out.has_stopped());
    EXPECT_NE(nullptr, get_exception<std::runtime_error>(out));
  };
  (testNonValue(makes, verify), ...);
}

template <typename... Makes>
void testNonValueStopped(Makes... makes) {
  auto verify = [](auto& out) { EXPECT_TRUE(out.has_stopped()); };
  (testNonValue(makes, verify), ...);
}

// Test all `or_unwind...` flavors in both `result` and `now_task` coros.
template <typename ResultT, typename MakeInner, typename CheckValue>
void testOrUnwind(MakeInner makeInner, const CheckValue& checkValue) {
  using T = typename ResultT::value_type;

  // Static assertions on `or_unwind...` return types
  if constexpr (std::is_void_v<T>) {
    checkAllAwaitResumeTypes<ResultT, void, void, void, void>();
  } else if constexpr (std::is_rvalue_reference_v<T>) {
    using U = std::remove_reference_t<T>;
    checkRvalueAwaitResumeTypes<ResultT, U&&, U&&>();
  } else { // Value or lvalue ref
    using U = std::remove_reference_t<T>;
    checkAllAwaitResumeTypes<ResultT, U&, const U&, T&&, T>();
  }

  { // Extract value in result & task coros
    auto makeRes = [&] { return makeResultWithValue<ResultT>(makeInner); };
    forEachOrUnwindVariant<InResult>(makeRes, checkValue);
    forEachOrUnwindVariant<InTask>(makeRes, checkValue);
  }

  // Propagate error/stopped, for result types with `.non_value()`
  if constexpr (requires { FOLLY_DECLVAL(ResultT).non_value(); }) {
    testNonValueErrors([&] {
      return ResultT{non_value_result{std::runtime_error{"err"}}};
    });
    testNonValueStopped([&] { return ResultT{stopped_result}; });
  }
}

// Call `testOrUnwind`, varying the inner value type of `ResultTemplate`.
template <template <typename> class ResultTemplate>
void testOrUnwindVaryingInnerType() {
  // `void`
  testOrUnwind<ResultTemplate<void>>([] {}, CheckExtract<void>{});

  // `int` and `string` -- copyable value
  testOrUnwind<ResultTemplate<int>>([] { return 42; }, CheckExtract<int>{42});
  testOrUnwind<ResultTemplate<std::string>>(
      [] { return std::string{"hello"}; }, CheckExtract<std::string>{"hello"});

  // `unique_ptr` -- move-only value
  testOrUnwind<ResultTemplate<std::unique_ptr<int>>>(
      [] { return std::make_unique<int>(42); },
      []<typename Context>(tag_t<Context>, auto&& awaitable) {
        auto out = Context::run(
            [&]() -> typename Context::template Coro<std::unique_ptr<int>> {
              co_return co_await must_use_immediately_unsafe_mover(
                  static_cast<decltype(awaitable)>(awaitable))();
            });
        auto& ptr = out.value_or_throw();
        EXPECT_EQ(42, *ptr);
      });

  { // `int&` -- ref to copyable
    int val = 42;
    testOrUnwind<ResultTemplate<int&>>(
        [&]() -> int& { return val; }, CheckExtractRef<int>{&val});
    testOrUnwind<ResultTemplate<int&&>>(
        [&]() -> int&& { return std::move(val); }, CheckExtractRef<int>{&val});
  }
  { // `unique_ptr&` and `unique_ptr&&` -- ref to move-only
    auto ptr = std::make_unique<int>(1337);
    CheckExtractRef<std::unique_ptr<int>> check{&ptr};
    testOrUnwind<ResultTemplate<std::unique_ptr<int>&>>(
        [&]() -> std::unique_ptr<int>& { return ptr; }, check);
    testOrUnwind<ResultTemplate<std::unique_ptr<int>&&>>(
        [&]() -> std::unique_ptr<int>&& { return std::move(ptr); }, check);
    EXPECT_EQ(1337, *ptr); // not actually moved-out
  }
}

} // namespace

TEST(OrUnwind, result) {
  testOrUnwindVaryingInnerType<result>();
}

TEST(OrUnwind, valueOnlyResult) {
  testOrUnwindVaryingInnerType<value_only_result>();
}

// Test `or_unwind...` overloads for `non_value_result` & `stopped_result_t`
TEST(OrUnwind, nonValueResult) {
  checkRvalueAwaitResumeTypes<non_value_result, void, void>();
  checkRvalueAwaitResumeTypes<stopped_result_t, void, void>();

  testNonValueErrors([] {
    return non_value_result{std::runtime_error{"err"}};
  });
  testNonValueStopped(
      [] { return non_value_result{stopped_result}; },
      [] { return stopped_result; });
}

} // namespace folly

#endif
