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

#pragma once

#include <atomic>
#include <type_traits>

#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Result.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GMock.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace gmock_helpers {

// This helper function is intended for use in GMock implementations where the
// implementation of the method is a coroutine lambda.
//
// The GMock framework internally always takes a copy of an action/lambda
// before invoking it to prevent cases where invoking the method might end
// up destroying itself.
//
// However, this is problematic for coroutine-lambdas-with-captures as the
// return-value from invoking a coroutine lambda will typically capture a
// reference to the copy of the lambda which will immediately become a dangling
// reference as soon as the mocking framework returns that value to the caller.
//
// Use this action-factory instead of Invoke() when passing coroutine-lambdas
// to mock definitions to ensure that a copy of the lambda is kept alive until
// the coroutine completes. It does this by invoking the lambda using the
// folly::coro::co_invoke() helper instead of directly invoking the lambda.
//
//
// Example:
//   using namespace ::testing
//   using namespace folly::coro::gmock_helpers;
//
//   MockFoo mock;
//   int fooCallCount = 0;
//
//   EXPECT_CALL(mock, foo(_))
//     .WillRepeatedly(CoInvoke(
//         [&](int x) -> folly::coro::Task<int> {
//           ++fooCallCount;
//           co_return x + 1;
//         }));
//
template <typename F>
auto CoInvoke(F&& f) {
  return ::testing::Invoke([f = static_cast<F&&>(f)](auto&&... a) {
    return co_invoke(f, static_cast<decltype(a)>(a)...);
  });
}

// CoInvoke variant that does not pass arguments to callback function.
//
// Example:
//   using namespace ::testing
//   using namespace folly::coro::gmock_helpers;
//
//   MockFoo mock;
//   int fooCallCount = 0;
//
//   EXPECT_CALL(mock, foo(_))
//     .WillRepeatedly(CoInvokeWithoutArgs(
//         [&]() -> folly::coro::Task<int> {
//           ++fooCallCount;
//           co_return 42;
//         }));
template <typename F>
auto CoInvokeWithoutArgs(F&& f) {
  return ::testing::InvokeWithoutArgs(
      [f = static_cast<F&&>(f)]() { return co_invoke(f); });
}

namespace detail {
template <typename Fn>
auto makeCoAction(Fn&& fn) {
  static_assert(
      std::is_copy_constructible_v<remove_cvref_t<Fn>>,
      "Fn should be copyable to allow calling mocked call multiple times.");

  using Ret = std::invoke_result_t<remove_cvref_t<Fn>&&>;
  return ::testing::InvokeWithoutArgs(
      [fn = std::forward<Fn>(fn)]() mutable -> Ret { return co_invoke(fn); });
}

// Helper class to capture a ByMove return value for mocked coroutine function.
// Adds a test failure if it is moved twice like:
//    .WillRepeatedly(CoReturnByMove...)
template <typename R>
struct OnceForwarder {
  static_assert(std::is_reference_v<R>);
  using V = remove_cvref_t<R>;

  explicit OnceForwarder(R r) noexcept(std::is_nothrow_constructible_v<V>)
      : val_(static_cast<R>(r)) {}

  R operator()() noexcept {
    auto performedPreviously =
        performed_.exchange(true, std::memory_order_relaxed);
    if (performedPreviously) {
      terminate_with<std::runtime_error>(
          "a CoReturnByMove action must be performed only once");
    }
    return static_cast<R>(val_);
  }

 private:
  V val_;
  std::atomic<bool> performed_ = false;
};

// Allow to return a value by providing a convertible value.
// This works similarly to Return(x):
// MOCK_METHOD1(Method, T(U));
// EXPECT_CALL(mock, Method(_)).WillOnce(Return(F()));
// should work as long as F is convertible to T.
template <typename T>
class CoReturnImpl {
 public:
  explicit CoReturnImpl(T&& value) : value_(std::move(value)) {}

  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple& /* unused */) const {
    return [](T value) -> Result { co_return value; }(value_);
  }

 private:
  T value_;
};

template <typename T>
class CoReturnByMoveImpl {
 public:
  explicit CoReturnByMoveImpl(std::shared_ptr<OnceForwarder<T&&>> forwarder)
      : forwarder_(std::move(forwarder)) {}

  template <typename Result, typename ArgumentTuple>
  Result Perform(const ArgumentTuple& /* unused */) const {
    return [](std::shared_ptr<OnceForwarder<T&&>> forwarder) -> Result {
      co_return (*forwarder)();
    }(forwarder_);
  }

 private:
  std::shared_ptr<OnceForwarder<T&&>> forwarder_;
};

} // namespace detail

// Helper functions to adapt CoRoutines enabled functions to be mocked using
// gMock. CoReturn and CoThrows are gMock Action types that mirror the Return
// and Throws Action types used in EXPECT_CALL|ON_CALL invocations.
//
// Example:
//   using namespace ::testing
//   using namespace folly::coro::gmock_helpers;
//
//   MockFoo mock;
//   std::string result = "abc";
//
//   EXPECT_CALL(mock, co_foo(_))
//     .WillRepeatedly(CoReturn(result));
//
//   // For Task<void> return types.
//   EXPECT_CALL(mock, co_bar(_))
//     .WillRepeatedly(CoReturn());
//
//   // For returning by move.
//   EXPECT_CALL(mock, co_bar(_))
//     .WillRepeatedly(CoReturnByMove(std::move(result)));
//
//   // For returning by move.
//   EXPECT_CALL(mock, co_bar(_))
//     .WillRepeatedly(CoReturnByMove(std::make_unique(result)));
//
//
//  EXPECT_CALL(mock, co_foo(_))
//     .WillRepeatedly(CoThrow<std::string>(std::runtime_error("error")));
template <typename T>
auto CoReturn(T ret) {
  return ::testing::MakePolymorphicAction(
      detail::CoReturnImpl<T>(std::move(ret)));
}

inline auto CoReturn() {
  return ::testing::InvokeWithoutArgs([]() -> Task<> { co_return; });
}

template <typename T>
auto CoReturnByMove(T&& ret) {
  static_assert(
      !std::is_lvalue_reference_v<decltype(ret)>,
      "the argument must be passed as non-const rvalue-ref");
  static_assert(
      !std::is_const_v<T>,
      "the argument must be passed as non-const rvalue-ref");

  auto ptr = std::make_shared<detail::OnceForwarder<T&&>>(std::move(ret));

  return ::testing::MakePolymorphicAction(
      detail::CoReturnByMoveImpl<T>(std::move(ptr)));
}

template <typename T, typename Ex>
auto CoThrow(Ex&& e) {
  return detail::makeCoAction(
      [ex = std::forward<Ex>(e)]() -> Task<T> { co_yield co_error(ex); });
}

} // namespace gmock_helpers
} // namespace coro
} // namespace folly

#define CO_ASSERT_THAT(value, matcher) \
  CO_ASSERT_PRED_FORMAT1(              \
      ::testing::internal::MakePredicateFormatterFromMatcher(matcher), value)

#endif // FOLLY_HAS_COROUTINES
