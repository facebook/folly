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

#include <folly/coro/AsyncScope.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/Task.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/futures/ManualTimekeeper.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

struct SleepTest : testing::Test {};

CO_TEST_F(SleepTest, Basic) {
  constexpr auto kSleepDuration{std::chrono::seconds(1)};

  folly::coro::CancellableAsyncScope asyncScope;
  folly::ManualTimekeeper manualTimekeeper;

  size_t sleepCompletedCount = 0;

  auto func = [&]() -> folly::coro::Task<> {
    while (true) {
      co_await folly::coro::sleep(kSleepDuration, &manualTimekeeper);
      ++sleepCompletedCount;
    }
  };

  asyncScope.add(func().scheduleOn(co_await folly::coro::co_current_executor));
  co_await folly::coro::co_reschedule_on_current_executor;
  EXPECT_EQ(sleepCompletedCount, 0);

  manualTimekeeper.advance(2 * kSleepDuration);
  co_await folly::coro::co_reschedule_on_current_executor;
  EXPECT_EQ(sleepCompletedCount, 1);

  manualTimekeeper.advance(2 * kSleepDuration);
  co_await folly::coro::co_reschedule_on_current_executor;
  EXPECT_EQ(sleepCompletedCount, 2);

  manualTimekeeper.advance(2 * kSleepDuration);
  co_await asyncScope.cancelAndJoinAsync();
  EXPECT_EQ(sleepCompletedCount, 2);
}

#endif
