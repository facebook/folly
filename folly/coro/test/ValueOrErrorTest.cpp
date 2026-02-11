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

#include <folly/Try.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Result.h>
#include <folly/coro/ValueOrError.h>
#include <folly/coro/ValueOrFatal.h>
#include <folly/coro/ViaIfAsync.h>
#include <folly/coro/safe/NowTask.h>

/// Besides `value_or_error_or_stopped`, this test also covers the
/// `folly::result<T>` integration with `coro::co_result` from `coro/Result.h`.

#if FOLLY_HAS_RESULT

namespace folly::coro {

CO_TEST(ValueOrErrorTest, valueOrErrorOrStoppedOfError) {
  auto voidErrorTask = []() -> now_task<void> {
    co_yield co_error(std::runtime_error("foo"));
  };
  { // Capture the error
    auto res = co_await value_or_error_or_stopped(voidErrorTask());
    EXPECT_STREQ("foo", get_exception<std::runtime_error>(res)->what());
  }
  { // Also test `coro::co_result` integration
    auto res = co_await value_or_error_or_stopped([&]() -> now_task<void> {
      co_yield co_result(co_await value_or_error_or_stopped(voidErrorTask()));
    }());
    EXPECT_STREQ("foo", get_exception<std::runtime_error>(res)->what());
  }
}

CO_TEST(ValueOrErrorTest, valueOrErrorOrStoppedOfValue) {
  // Return a move-only thing to make sure we don't copy
  auto valueTask = []() -> now_task<std::unique_ptr<int>> {
    co_return std::make_unique<int>(1337);
  };
  { // Capture the value
    auto res = co_await value_or_error_or_stopped(valueTask());
    // Real code should use `co_await co_ready(res)` to unpack!
    EXPECT_EQ(1337, *res.value_or_throw());
  }
  { // Also test `coro::co_result` integration
    auto res = co_await value_or_error_or_stopped(
        [&]() -> now_task<std::unique_ptr<int>> {
          co_yield co_result(co_await value_or_error_or_stopped(valueTask()));
        }());
    EXPECT_EQ(1337, *res.value_or_throw());
  }
  { // Also test `co_result` with `value_only_result`
    auto res = co_await value_or_error_or_stopped([&]() -> now_task<int> {
      co_yield co_result(value_only_result<int>{42});
    }());
    EXPECT_EQ(42, res.value_or_throw());
  }
}

CO_TEST(ValueOrErrorTest, valueOrErrorOrStoppedOfVoid) {
  int numAwaited = 0;
  auto voidTask = [&]() -> now_task<void> {
    ++numAwaited;
    co_return;
  };
  { // Capturing a "value" completion
    auto res = co_await value_or_error_or_stopped(voidTask());
    static_assert(std::is_same_v<decltype(res), result<>>);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(1, numAwaited);
  }
  { // Also test `coro::co_result` integration
    auto res = co_await value_or_error_or_stopped([&]() -> now_task<void> {
      co_yield co_result(co_await value_or_error_or_stopped(voidTask()));
    }());
    static_assert(std::is_same_v<decltype(res), result<>>);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(2, numAwaited);
  }
}

CO_TEST(ValueOrErrorTest, valueOrErrorOrStoppedStopped) {
  auto stoppedTask = [&]() -> now_task<void> {
    co_yield co_stopped_may_throw;
    LOG(FATAL) << "not reached";
  };
  { // Capturing a "stopped" completion
    auto res = co_await value_or_error_or_stopped(stoppedTask());
    EXPECT_TRUE(res.has_stopped());
  }
  { // Also test `coro::co_result` integration
    auto res = co_await value_or_error_or_stopped([&]() -> now_task<void> {
      co_yield co_result(co_await value_or_error_or_stopped(stoppedTask()));
      LOG(FATAL) << "not reached";
    }());
    EXPECT_TRUE(res.has_stopped());
  }
}

TEST(ValueOrErrorTest, coNothrowValueOrErrorOrStopped) {
  bool ran = []<typename T>(T) { // `requires` does SFINAE inside templates
    auto errorTask = [&]() -> now_task<> {
      co_yield co_error(std::runtime_error("foo"));
    };
    (void)co_nothrow(errorTask()); // compile
    (void)value_or_error_or_stopped(errorTask()); // compiles
    static_assert(!requires {
      co_nothrow(value_or_error_or_stopped(errorTask()));
    });
#if 0 // manual test equivalent of above `static_assert`
    (void)co_nothrow(value_or_error_or_stopped(errorTask())); // constraint failure
#endif
    return true;
  }(5);
  EXPECT_TRUE(ran); // it's easy to forget to call the lambda
}

struct ThrowingMove {
  ThrowingMove(const ThrowingMove&) = default;
  ThrowingMove& operator=(const ThrowingMove&) = default;
  // NOLINTNEXTLINE(performance-noexcept-move-constructor)
  ThrowingMove(ThrowingMove&&) {}
  // NOLINTNEXTLINE(performance-noexcept-move-constructor)
  ThrowingMove& operator=(ThrowingMove&&) { return *this; }
};
struct NothrowMove {};

template <typename T>
using TestAwaiter = detail::ResultAwaiter<TaskWithExecutor<T>>;

// While it's unclear if anything cares about `await_resume()` `noexcept`
// discipline, it's easy enough to check.
static_assert(noexcept(FOLLY_DECLVAL(TestAwaiter<int>).await_resume()));
static_assert(noexcept(FOLLY_DECLVAL(TestAwaiter<NothrowMove>).await_resume()));
static_assert(
    !noexcept(FOLLY_DECLVAL(TestAwaiter<ThrowingMove>).await_resume()));

// Awaiter lacking noexcept on `await_suspend`, for manual test below.
struct ThrowingAwaitSuspendAwaitable {
  bool await_ready() noexcept { return false; }
  void await_suspend(coro::coroutine_handle<>) {} // not noexcept
  int await_resume() noexcept { return 0; }
  Try<int> await_resume_try() noexcept { return Try<int>{0}; }
  friend ThrowingAwaitSuspendAwaitable&& co_viaIfAsync(
      const folly::Executor::KeepAlive<>&,
      ThrowingAwaitSuspendAwaitable&& a) noexcept {
    return std::move(a);
  }
};

CO_TEST(ValueOrErrorTest, requiresNoexceptAwait) {
#if 0 // Manual test: "value-only await requires noexcept await_suspend()"
  (void)co_await value_or_error(ThrowingAwaitSuspendAwaitable{});
#endif
  co_return;
}

CO_TEST(ValueOrErrorTest, coAwaitTryRequiresNoexceptAwait) {
#if 0 // Manual test: "value-only await requires noexcept await_suspend()"
  (void)co_await co_awaitTry(ThrowingAwaitSuspendAwaitable{});
#endif
  co_return;
}

// Deliberately redundant with `ValueOrFatalTest.ValueOrErrorComposition`.
// Do NOT remove.
//
// `value_or_error` must not activate the "bypass" mechanism when wrapping a
// value-only awaitable like `value_or_fatal`. Otherwise, `OperationCancelled`
// would be intercepted at the promise level, before the inner awaitable's
// `on_stopped` policy could substitute the default value.
CO_TEST(ValueOrErrorTest, valueOrErrorAroundValueOnlyAwaitable) {
  auto coStopped = []() -> value_or_fatal<Task<int>, on_stopped<99>> {
    co_yield co_stopped_may_throw;
    co_return -1;
  };
  EXPECT_EQ(99, (co_await value_or_error(coStopped())).value_only());
  EXPECT_EQ(
      99, (co_await value_or_error_or_stopped(coStopped())).value_or_throw());
}

} // namespace folly::coro

#endif
