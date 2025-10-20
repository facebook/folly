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

#include <folly/coro/AwaitResult.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Result.h>
#include <folly/coro/safe/NowTask.h>

#include <type_traits>

#include <folly/ExceptionWrapper.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace folly;
using namespace folly::coro;

TEST(CoErrorTest, constructible) {
  EXPECT_TRUE((std::is_constructible_v<co_error, exception_wrapper>));
  EXPECT_TRUE((std::is_constructible_v<co_error, std::runtime_error>));
  EXPECT_TRUE(
      (std::is_constructible_v<
          co_error,
          std::in_place_type_t<std::runtime_error>,
          std::string>));
  EXPECT_FALSE((std::is_constructible_v<co_error, int>));
}

// NB: Cancellation is not an error (https://wg21.link/p1677), but in current
// coro, the handling is so intertwined that we test it here.

CO_TEST(CoCancellationTest, propagateOperationCancelled) {
  auto cancelledTask = []() -> now_task<> { co_yield co_cancelled; };

  // `co_await_result` & `co_awaitTry` interrupt cancellation
  EXPECT_TRUE((co_await co_await_result(cancelledTask())).has_stopped());
  EXPECT_TRUE(
      // Prefer `has_stopped()` in coro code.  Outside of coro code, catching
      // `OperationCancelled` is OK.
      (co_await co_awaitTry(cancelledTask()))
          .hasException<OperationCancelled>());

  // Throws if awaited directly in coro or non-coro code
  EXPECT_THROW((co_await cancelledTask()), OperationCancelled);
  EXPECT_THROW(blocking_wait(cancelledTask()), OperationCancelled);
}

#endif
