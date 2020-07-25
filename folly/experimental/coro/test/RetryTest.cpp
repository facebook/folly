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
#include <folly/experimental/coro/Retry.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#include <chrono>
#include <exception>

using namespace std::chrono_literals;

namespace {

struct SomeError : std::exception {
  explicit SomeError(int value) : value(value) {}
  int value;
};

} // namespace

TEST(RetryN, Success) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    co_await folly::coro::retryN(3, [&]() -> folly::coro::Task<void> {
      ++runCount;
      co_return;
    });
    EXPECT_EQ(1, runCount);
  }());
}

TEST(RetryN, Failure) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    folly::Try<void> result = co_await folly::coro::co_awaitTry(
        folly::coro::retryN(3, [&]() -> folly::coro::Task<void> {
          ++runCount;
          co_yield folly::coro::co_error(SomeError{runCount});
        }));
    EXPECT_EQ(4, runCount);
    EXPECT_TRUE(result.hasException<SomeError>());
    EXPECT_EQ(4, result.tryGetExceptionObject<SomeError>()->value);
  }());
}

TEST(RetryN, EventualSuccess) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    folly::Try<void> result = co_await folly::coro::co_awaitTry(
        folly::coro::retryN(3, [&]() -> folly::coro::Task<void> {
          ++runCount;
          if (runCount <= 2) {
            co_yield folly::coro::co_error(SomeError{runCount});
          }
        }));
    EXPECT_EQ(3, runCount);
    EXPECT_TRUE(result.hasValue());
  }());
}

TEST(RetryWithJitter, Success) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    auto start = std::chrono::steady_clock::now();
    folly::Try<void> result = co_await folly::coro::co_awaitTry(
        folly::coro::retryWithExponentialBackoff(
            10, 10ms, 500ms, 0.25, [&]() -> folly::coro::Task<void> {
              ++runCount;
              co_return;
            }));
    auto end = std::chrono::steady_clock::now();
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(1, runCount);
    EXPECT_TRUE((end - start) < 10ms);
  }());
}

TEST(RetryWithJitter, Failure) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    auto prev = std::chrono::steady_clock::now();
    folly::Try<void> result = co_await folly::coro::co_awaitTry(
        folly::coro::retryWithExponentialBackoff(
            5, 10ms, 100ms, 0.25, [&]() -> folly::coro::Task<void> {
              ++runCount;
              auto now = std::chrono::steady_clock::now();
              auto elapsedMs =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - prev)
                      .count();
              LOG(INFO) << "Attempt " << runCount << " after " << elapsedMs
                        << "ms";
              prev = now;
              co_yield folly::coro::co_error(SomeError{runCount});
            }));
    EXPECT_TRUE(result.hasException<SomeError>());
    EXPECT_EQ(6, runCount);
  }());
}

TEST(RetryWithJitter, EventualSuccess) {
  folly::coro::blockingWait([]() -> folly::coro::Task<void> {
    int runCount = 0;
    auto start = std::chrono::steady_clock::now();
    folly::Try<void> result = co_await folly::coro::co_awaitTry(
        folly::coro::retryWithExponentialBackoff(
            10, 10ms, 500ms, 0.25, [&]() -> folly::coro::Task<void> {
              ++runCount;
              auto now = std::chrono::steady_clock::now();
              if (runCount == 1) {
                EXPECT_TRUE((now - start) < 5ms);
                start = now;
                co_yield folly::coro::co_error(SomeError{1});
              } else if (runCount == 2) {
                // Really should be at least 10ms but allowing for some
                // potential measurement error between the timer and
                // steady_clock.
                EXPECT_TRUE((now - start) >= 8ms);
                start = now;
                co_yield folly::coro::co_error(SomeError{2});
              }
              co_return;
            }));
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(3, runCount);
  }());
}

#endif // FOLLY_HAS_COROUTINES
