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

#include <folly/Portability.h>

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Timeout.h>
#include <folly/futures/Future.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>

#include <chrono>
#include <stdexcept>

#if FOLLY_HAS_COROUTINES

using namespace std::chrono_literals;
using namespace folly;

struct Timeout {
  template <typename... Arg>
  auto operator()(Arg&&... args) {
    return coro::timeout(std::forward<Arg>(args)...);
  }
  using ExType = FutureTimeout;
};

struct TimeoutNoDiscard {
  template <typename... Arg>
  auto operator()(Arg&&... args) {
    return coro::timeoutNoDiscard(std::forward<Arg>(args)...);
  }
  using ExType = OperationCancelled;
};

template <typename T>
struct TimeoutFixture : public ::testing::Test {
  T fn;
};

using TimeoutTestTypes = ::testing::Types<Timeout, TimeoutNoDiscard>;
TYPED_TEST_SUITE(TimeoutFixture, TimeoutTestTypes);

TYPED_TEST(TimeoutFixture, CompletesSynchronously) {
  coro::blockingWait([&fn = this->fn]() -> coro::Task<> {
    // Completing synchronously with void
    co_await fn([]() -> coro::Task<void> { co_return; }(), 1s);

    // Completing synchronously with a value.

    auto result = co_await fn([]() -> coro::Task<int> { co_return 42; }(), 1s);
    EXPECT_EQ(42, result);

    // Test that it handles failing synchronously
    auto tryResult = co_await coro::co_awaitTry(fn(
        [&]() -> coro::Task<int> {
          if (true) {
            throw std::runtime_error{"bad value"};
          }
          co_return result;
        }(),
        1s));
    EXPECT_TRUE(tryResult.template hasException<std::runtime_error>());
  }());
}

TYPED_TEST(TimeoutFixture, CompletesWithinTimeout) {
  coro::blockingWait([&fn = this->fn]() -> coro::Task<> {
    // Completing synchronously with void
    co_await fn(
        []() -> coro::Task<void> {
          co_await coro::sleep(1ms);
          co_return;
        }(),
        1s);

    // Completing synchronously with a value.
    auto result = co_await fn(
        []() -> coro::Task<int> {
          co_await coro::sleep(1ms);
          co_return 42;
        }(),
        1s);
    EXPECT_EQ(42, result);

    // Test that it handles failing synchronously
    auto tryResult = co_await coro::co_awaitTry(fn(
        [&]() -> coro::Task<int> {
          co_await coro::sleep(1ms);
          if (true) {
            throw std::runtime_error{"bad value"};
          }
          co_return result;
        }(),
        1s));
    EXPECT_TRUE(tryResult.template hasException<std::runtime_error>());
  }());
}

TEST(TimeoutNoDiscard, ResultOnTimeout) {
  coro::blockingWait([]() -> coro::Task<> {
    co_await coro::timeoutNoDiscard(
        []() -> coro::Task<void> {
          co_await coro::sleepReturnEarlyOnCancel(10s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return;
        }(),
        1ms);

    auto result = co_await coro::timeoutNoDiscard(
        []() -> coro::Task<int> {
          co_await coro::sleepReturnEarlyOnCancel(10s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return 42;
        }(),
        1ms);
    EXPECT_EQ(42, result);

    struct sentinel : public std::exception {};
    auto tryResult = co_await coro::co_awaitTry(coro::timeoutNoDiscard(
        [&]() -> coro::Task<int> {
          co_await coro::sleepReturnEarlyOnCancel(10s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          throw sentinel{};
        }(),
        1ms));
    EXPECT_TRUE(tryResult.template hasException<sentinel>());
  }());
}

TYPED_TEST(TimeoutFixture, TimeoutElapsed) {
  using ExType = typename decltype(this->fn)::ExType;
  coro::blockingWait([&fn = this->fn]() -> coro::Task<> {
    // Completing synchronously with void
    auto start = std::chrono::steady_clock::now();
    folly::Try<void> voidResult = co_await coro::co_awaitTry(fn(
        []() -> coro::Task<void> {
          co_await coro::sleep(1s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return;
        }(),
        5ms));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(voidResult.hasException<ExType>());

    // Completing synchronously with a value.
    start = std::chrono::steady_clock::now();
    auto result = co_await coro::co_awaitTry(fn(
        []() -> coro::Task<int> {
          co_await coro::sleep(1s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return 42;
        }(),
        5ms));
    elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(result.template hasException<ExType>());

    // Test that it handles failing synchronously
    start = std::chrono::steady_clock::now();
    auto failResult = co_await coro::co_awaitTry(fn(
        [&]() -> coro::Task<int> {
          co_await coro::sleep(1s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          if (true) {
            throw std::runtime_error{"bad value"};
          }
          co_return result;
        }(),
        5ms));
    elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(result.template hasException<ExType>());
  }());
}

TYPED_TEST(TimeoutFixture, CancelParent) {
  coro::blockingWait([&fn = this->fn]() -> coro::Task<> {
    CancellationSource cancelSource;

    auto start = std::chrono::steady_clock::now();

    auto [cancelled, _] = co_await coro::collectAll(
        coro::co_withCancellation(
            cancelSource.getToken(),
            fn(
                []() -> coro::Task<bool> {
                  auto result = co_await coro::co_awaitTry(coro::sleep(5s));
                  co_return result.template hasException<OperationCancelled>();
                }(),
                10s)),
        [&]() -> coro::Task<void> {
          cancelSource.requestCancellation();
          co_return;
        }());

    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 1s);

    EXPECT_TRUE(cancelled);
  }());
}

TYPED_TEST(TimeoutFixture, AsyncGenerator) {
  coro::blockingWait([&fn = this->fn]() -> coro::Task<> {
    // Completing synchronously with a value.
    auto result = co_await fn(
        []() -> coro::AsyncGenerator<int> { co_yield 42; }().next(), 1s);
    EXPECT_EQ(42, *result);

    // Test that it handles failing synchronously
    auto tryResult = co_await coro::co_awaitTry(fn(
        []() -> coro::AsyncGenerator<int> {
          co_yield coro::co_error(std::runtime_error{"bad value"});
        }()
                    .next(),
        1s));
    EXPECT_TRUE(tryResult.template hasException<std::runtime_error>());

    // Generator completing normally.
    result = co_await fn(
        []() -> coro::AsyncGenerator<int> { co_return; }().next(), 1s);
    EXPECT_FALSE(result);
  }());
}

TYPED_TEST(TimeoutFixture, RequestContextInCancellationCallback) {
  RequestContextScopeGuard guard;
  auto* ctx = folly::RequestContext::try_get();
  ASSERT_TRUE(ctx);

  bool cancelled = false;
  coro::blockingWait([&, &fn = this->fn]() -> coro::Task<> {
    co_await coro::co_awaitTry(fn(
        [&]() -> coro::Task<void> {
          CancellationCallback cb{
              co_await coro::co_current_cancellation_token, [&] {
                EXPECT_EQ(folly::RequestContext::try_get(), ctx);
                cancelled = true;
              }};
          co_await coro::sleep(1s);
        }(),
        5ms));
  }());
  ASSERT_TRUE(cancelled);
}

#endif // FOLLY_HAS_COROUTINES
