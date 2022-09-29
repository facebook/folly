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

#include <chrono>
#include <thread>

#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Retry.h>
#include <folly/portability/GTest.h>

using namespace std::literals::chrono_literals;

CO_TEST(CodeExamples, retryTest) {
  int t = 0;

  co_await folly::coro::retryWithExponentialBackoff(
      3, 100ms, 500ms, 0.05, [&t]() -> folly::coro::Task<> {
        if (t++ < 3) {
          co_yield folly::coro::co_error(std::logic_error(""));
        }
      });
  CO_ASSERT_EQ(t, 4);

  CO_ASSERT_THROW(
      co_await folly::coro::retryWithExponentialBackoff(
          3,
          100ms,
          500ms,
          0.05,
          [&t]() -> folly::coro::Task<> {
            t++;
            co_yield folly::coro::co_error(std::logic_error(""));
          }),
      std::logic_error);
  CO_ASSERT_EQ(t, 8);

  co_return;
}
