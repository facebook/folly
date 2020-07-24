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

#include <folly/Portability.h>

#if FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Timeout.h>
#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>

#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;
using namespace folly;

TEST(Timeout, CompletesSynchronously) {
  coro::blockingWait([]() -> coro::Task<> {
    // Completing synchronously with void
    co_await coro::timeout([]() -> coro::Task<void> { co_return; }(), 1s);

    // Completing synchronously with a value.
    auto result =
        co_await coro::timeout([]() -> coro::Task<int> { co_return 42; }(), 1s);
    EXPECT_EQ(42, result);

    // Test that it handles failing synchronously
    auto tryResult = co_await coro::co_awaitTry(coro::timeout(
        [&]() -> coro::Task<int> {
          if (true) {
            throw std::runtime_error{"bad value"};
          }
          co_return result;
        }(),
        1s));
    EXPECT_TRUE(tryResult.hasException<std::runtime_error>());
  }());
}

TEST(Timeout, CompletesWithinTimeout) {
  coro::blockingWait([]() -> coro::Task<> {
    // Completing synchronously with void
    co_await coro::timeout(
        []() -> coro::Task<void> {
          co_await coro::sleep(1ms);
          co_return;
        }(),
        1s);

    // Completing synchronously with a value.
    auto result = co_await coro::timeout(
        []() -> coro::Task<int> {
          co_await coro::sleep(1ms);
          co_return 42;
        }(),
        1s);
    EXPECT_EQ(42, result);

    // Test that it handles failing synchronously
    auto tryResult = co_await coro::co_awaitTry(coro::timeout(
        [&]() -> coro::Task<int> {
          co_await coro::sleep(1ms);
          if (true) {
            throw std::runtime_error{"bad value"};
          }
          co_return result;
        }(),
        1s));
    EXPECT_TRUE(tryResult.hasException<std::runtime_error>());
  }());
}

TEST(Timeout, TimeoutElapsed) {
  coro::blockingWait([]() -> coro::Task<> {
    // Completing synchronously with void
    auto start = std::chrono::steady_clock::now();
    folly::Try<void> voidResult = co_await coro::co_awaitTry(coro::timeout(
        []() -> coro::Task<void> {
          co_await coro::sleep(1s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return;
        }(),
        5ms));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(voidResult.hasException<folly::FutureTimeout>());

    // Completing synchronously with a value.
    start = std::chrono::steady_clock::now();
    auto result = co_await coro::co_awaitTry(coro::timeout(
        []() -> coro::Task<int> {
          co_await coro::sleep(1s);
          EXPECT_TRUE((co_await coro::co_current_cancellation_token)
                          .isCancellationRequested());
          co_return 42;
        }(),
        5ms));
    elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_TRUE(result.hasException<folly::FutureTimeout>());

    // Test that it handles failing synchronously
    start = std::chrono::steady_clock::now();
    auto failResult = co_await coro::co_awaitTry(coro::timeout(
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
    EXPECT_TRUE(result.hasException<folly::FutureTimeout>());
  }());
}

TEST(Timeout, CancelParent) {
  coro::blockingWait([]() -> coro::Task<> {
    CancellationSource cancelSource;

    auto start = std::chrono::steady_clock::now();

    auto [cancelled, _] = co_await coro::collectAll(
        coro::co_withCancellation(
            cancelSource.getToken(),
            coro::timeout(
                []() -> coro::Task<bool> {
                  co_await coro::sleep(5s);
                  co_return(co_await coro::co_current_cancellation_token)
                      .isCancellationRequested();
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

#endif // FOLLY_HAS_COROUTINES
