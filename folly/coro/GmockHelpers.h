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

#pragma once

#include <atomic>
#include <type_traits>

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Result.h>
#include <folly/coro/Task.h>
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

// Member function overload
template <class Class, typename MethodPtr>
auto CoInvoke(Class* obj_ptr, MethodPtr method_ptr) {
  return ::testing::Invoke([=](auto&&... a) {
    return co_invoke(method_ptr, obj_ptr, static_cast<decltype(a)>(a)...);
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
  return ::testing::InvokeWithoutArgs([f = static_cast<F&&>(f)]() {
    return co_invoke(f);
  });
}

// Member function overload
template <class Class, typename MethodPtr>
auto CoInvokeWithoutArgs(Class* obj_ptr, MethodPtr method_ptr) {
  return ::testing::InvokeWithoutArgs([=]() {
    return co_invoke(method_ptr, obj_ptr);
  });
}

namespace detail {

// Matcher implementation that wraps a Task and delegates to an inner matcher
// (typically ::testing::Throws or ::testing::ThrowsMessage).
// This allows CoThrows/CoThrowsMessage to reuse GMock's exception matchers.
template <typename InnerMatcher>
class CoroExceptionMatcherImpl {
 public:
  explicit CoroExceptionMatcherImpl(InnerMatcher inner_matcher)
      : inner_matcher_(std::move(inner_matcher)),
        // Convert the inner matcher to a concrete Matcher type so we can
        // use its DescribeTo/DescribeNegationTo methods.
        callable_matcher_(inner_matcher_) {}

  void DescribeTo(std::ostream* os) const { callable_matcher_.DescribeTo(os); }

  void DescribeNegationTo(std::ostream* os) const {
    callable_matcher_.DescribeNegationTo(os);
  }

  template <typename T>
  bool MatchAndExplain(T&& x, ::testing::MatchResultListener* listener) const {
    // Task is move-only. GoogleTest passes values as const T&, but we need
    // to move the Task into blockingWait. We use const_cast to enable the
    // move. This is safe because each matcher instance is used only once.
    using MutableT = std::remove_const_t<std::remove_reference_t<T>>;
    auto& mutableX = const_cast<MutableT&>(x);

    // Wrap the task in a shared_ptr so the callable can be const-invoked
    // (GMock's Throws matcher requires the callable to be const-invocable).
    auto taskHolder =
        std::make_shared<std::optional<MutableT>>(std::move(mutableX));

    std::function<void()> callable = [taskHolder]() {
      if (!taskHolder->has_value()) {
        throw std::logic_error("Task already consumed");
      }
      auto task = std::move(taskHolder->value());
      taskHolder->reset();
      blockingWait(std::move(task));
    };

    // Delegate to the inner matcher (Throws/ThrowsMessage).
    // GMock's Throws matchers expect a callable, which they invoke to check
    // for exceptions.
    return callable_matcher_.MatchAndExplain(callable, listener);
  }

 private:
  InnerMatcher inner_matcher_;
  ::testing::Matcher<std::function<void()>> callable_matcher_;
};

template <typename Fn>
auto makeCoAction(Fn&& fn) {
  static_assert(
      std::is_copy_constructible_v<remove_cvref_t<Fn>>,
      "Fn should be copyable to allow calling mocked call multiple times.");

  using Ret = invoke_result_t<remove_cvref_t<Fn>&&>;
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
    return [](T value) -> Result { co_return value; }(T(value_));
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
  return detail::makeCoAction([ex = std::forward<Ex>(e)]() -> Task<T> {
    co_yield co_error(ex);
  });
}

// CoThrows()
// CoThrows(exceptionMatcher)
// CoThrowsMessage(messageMatcher)
//
// These matchers accept a folly::coro::Task and verify that when awaited,
// it throws an exception with the given type and properties.
//
// Examples:
//
//   EXPECT_THAT(
//       []() -> folly::coro::Task<void> {
//         throw std::runtime_error("message");
//         co_return;
//       }(),
//       CoThrows<std::runtime_error>());
//
//   EXPECT_THAT(
//       []() -> folly::coro::Task<void> {
//         throw std::runtime_error("message");
//         co_return;
//       }(),
//       CoThrowsMessage<std::runtime_error>(HasSubstr("message")));
//
//   EXPECT_THAT(
//       []() -> folly::coro::Task<void> {
//         throw std::runtime_error("message");
//         co_return;
//       }(),
//       CoThrows<std::runtime_error>(
//           Property(&std::runtime_error::what, HasSubstr("message"))));

template <typename Err>
auto CoThrows() {
  return ::testing::MakePolymorphicMatcher(
      detail::CoroExceptionMatcherImpl<decltype(::testing::Throws<Err>())>(
          ::testing::Throws<Err>()));
}

template <typename Err, typename ExceptionMatcher>
auto CoThrows(const ExceptionMatcher& exception_matcher) {
  return ::testing::MakePolymorphicMatcher(
      detail::CoroExceptionMatcherImpl<decltype(::testing::Throws<Err>(
          exception_matcher))>(::testing::Throws<Err>(exception_matcher)));
}

template <typename Err, typename MessageMatcher>
auto CoThrowsMessage(MessageMatcher&& message_matcher) {
  static_assert(
      std::is_base_of_v<std::exception, Err>,
      "expected an std::exception-derived type");
  return ::testing::MakePolymorphicMatcher(
      detail::CoroExceptionMatcherImpl<decltype(::testing::ThrowsMessage<Err>(
          std::forward<MessageMatcher>(message_matcher)))>(
          ::testing::ThrowsMessage<Err>(
              std::forward<MessageMatcher>(message_matcher))));
}

} // namespace gmock_helpers
} // namespace coro
} // namespace folly

#define CO_ASSERT_THAT(value, matcher) \
  CO_ASSERT_PRED_FORMAT1(              \
      ::testing::internal::MakePredicateFormatterFromMatcher(matcher), value)

#endif // FOLLY_HAS_COROUTINES
