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

#include <folly/experimental/coro/AsyncScope.h>

#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

struct AsyncScopeTest : public testing::Test {};

TEST_F(AsyncScopeTest, ConstructDestruct) {
  // Safe to construct/destruct an AsyncScope without calling any methods.
  folly::coro::AsyncScope scope;
}

CO_TEST_F(AsyncScopeTest, AddAndJoin) {
  std::atomic<int> count = 0;
  auto makeTask = [&]() -> folly::coro::Task<> {
    ++count;
    co_return;
  };

  folly::coro::AsyncScope scope;
  for (int i = 0; i < 100; ++i) {
    scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }

  co_await scope.joinAsync();

  EXPECT_EQ(count, 100);
}

CO_TEST_F(AsyncScopeTest, StartChildTasksAfterCleanupStarted) {
  folly::coro::AsyncScope scope;
  folly::coro::Baton baton;
  bool childFinished = false;
  auto executor = co_await folly::coro::co_current_executor;

  auto childTask = [&]() -> folly::coro::Task<> {
    co_await folly::coro::co_reschedule_on_current_executor;
    childFinished = true;
  };

  auto parentTask = [&]() -> folly::coro::Task<> {
    co_await baton;
    scope.add(childTask().scheduleOn(executor));
  };

  scope.add(parentTask().scheduleOn(executor));

  co_await folly::coro::collectAll(
      scope.joinAsync(), [&]() -> folly::coro::Task<> {
        baton.post();
        co_return;
      }());

  EXPECT_TRUE(childFinished);
}

CO_TEST_F(AsyncScopeTest, QueryRemainingCount) {
  folly::coro::Baton baton;

  auto makeTask = [&]() -> folly::coro::Task<> { co_await baton; };
  auto executor = co_await folly::coro::co_current_executor;

  folly::coro::AsyncScope scope;

  CO_ASSERT_EQ(0, scope.remaining());
  for (int i = 0; i < 10; ++i) {
    scope.add(makeTask().scheduleOn(executor));
  }
  CO_ASSERT_EQ(10, scope.remaining());

  baton.post();

  co_await scope.joinAsync();
  CO_ASSERT_EQ(0, scope.remaining());
}

struct CancellableAsyncScopeTest : public testing::Test {};

TEST_F(CancellableAsyncScopeTest, ConstructDestruct) {
  // Safe to construct/destruct an AsyncScope without calling any methods.
  folly::coro::CancellableAsyncScope scope;
}

CO_TEST_F(CancellableAsyncScopeTest, AddAndJoin) {
  std::atomic<int> count = 0;
  auto makeTask = [&]() -> folly::coro::Task<> {
    ++count;
    co_return;
  };

  folly::coro::CancellableAsyncScope scope;
  for (int i = 0; i < 100; ++i) {
    scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }

  co_await scope.joinAsync();

  EXPECT_EQ(count, 100);
}

CO_TEST_F(CancellableAsyncScopeTest, StartChildTasksAfterCleanupStarted) {
  folly::coro::CancellableAsyncScope scope;
  folly::coro::Baton baton;
  bool childFinished = false;
  auto executor = co_await folly::coro::co_current_executor;

  auto childTask = [&]() -> folly::coro::Task<> {
    co_await folly::coro::co_reschedule_on_current_executor;
    childFinished = true;
  };

  auto parentTask = [&]() -> folly::coro::Task<> {
    co_await baton;
    scope.add(childTask().scheduleOn(executor));
  };

  scope.add(parentTask().scheduleOn(executor));

  co_await folly::coro::collectAll(
      scope.joinAsync(), [&]() -> folly::coro::Task<> {
        baton.post();
        co_return;
      }());

  EXPECT_TRUE(childFinished);
}

CO_TEST_F(CancellableAsyncScopeTest, QueryRemainingCount) {
  folly::coro::Baton baton;

  auto makeTask = [&]() -> folly::coro::Task<> { co_await baton; };
  auto executor = co_await folly::coro::co_current_executor;

  folly::coro::CancellableAsyncScope scope;

  CO_ASSERT_EQ(0, scope.remaining());
  for (int i = 0; i < 10; ++i) {
    scope.add(makeTask().scheduleOn(executor));
  }
  CO_ASSERT_EQ(10, scope.remaining());

  baton.post();

  co_await scope.joinAsync();
  CO_ASSERT_EQ(0, scope.remaining());
}

CO_TEST_F(CancellableAsyncScopeTest, CancelSuspendedWork) {
  using namespace std::chrono_literals;

  auto makeTask = [&]() -> folly::coro::Task<> {
    co_await folly::coro::sleep(300s);
  };

  folly::coro::CancellableAsyncScope scope;

  CO_ASSERT_EQ(0, scope.remaining());
  for (int i = 0; i < 10; ++i) {
    scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }
  CO_ASSERT_EQ(10, scope.remaining());

  // Although we are suspended while sleeping, cancelAndJoinAsync will handle
  // this correctly.
  co_await scope.cancelAndJoinAsync();
  CO_ASSERT_EQ(0, scope.remaining());
}

#endif // FOLLY_HAS_COROUTINES
