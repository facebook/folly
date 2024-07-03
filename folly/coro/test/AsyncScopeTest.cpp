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

CO_TEST_F(AsyncScopeTest, QueryRemainingCountAfterJoined) {
  folly::coro::AsyncScope scope;
  folly::coro::Baton baton;

  auto makeTask = [&]() -> folly::coro::Task<> { co_await baton; };
  auto executor = co_await folly::coro::co_current_executor;
  scope.add(makeTask().scheduleOn(executor));

  EXPECT_EQ(scope.remaining(), 1);

  folly::coro::Baton validateBaton;
  auto validateTask = [&]() -> folly::coro::Task<> {
    EXPECT_EQ(scope.remaining(), 1);
    validateBaton.post();
    // sleep for scope.joinAsync() to get called.
    co_await folly::coro::sleep(std::chrono::milliseconds(10));
    EXPECT_EQ(scope.remaining(), 1);
    baton.post();
  };
  auto validateFut = validateTask().scheduleOn(executor).start();
  co_await validateBaton;
  co_await scope.joinAsync();
  co_await std::move(validateFut);
}

namespace {
folly::coro::Task<> crash() {
  folly::coro::AsyncScope scope{false};
  auto makeTask = [&]() -> folly::coro::Task<> {
    // sleep to force yielding
    co_await folly::coro::sleep(std::chrono::milliseconds(100));
    throw std::runtime_error("Computer says no");
  };
  scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  co_return;
}
} // namespace

CO_TEST_F(AsyncScopeTest, DontThrowOnJoin) {
  EXPECT_DEATH(folly::coro::blockingWait(crash()), "not yet complete");
  co_return;
}

CO_TEST_F(AsyncScopeTest, ThrowOnJoin) {
  folly::coro::AsyncScope scope{true};
  auto makeTask = [&]() -> folly::coro::Task<> {
    // sleep to force yielding
    co_await folly::coro::sleep(std::chrono::milliseconds(100));
    throw std::runtime_error("Computer says no");
  };
  scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));

  EXPECT_THROW(co_await scope.joinAsync(), std::runtime_error);
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
  for (int i = 0; i < 99; ++i) {
    scope.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }
  scope.addWithSourceLoc(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));

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

CO_TEST_F(CancellableAsyncScopeTest, QueryIsCancellationRequested) {
  using namespace std::chrono_literals;

  auto makeTask = [&]() -> folly::coro::Task<> {
    while (true) {
      co_await folly::coro::sleep(500s);
    }
  };
  auto executor = co_await folly::coro::co_current_executor;

  // default constructed scope
  folly::coro::CancellableAsyncScope scope;
  CO_ASSERT_EQ(false, scope.isScopeCancellationRequested());
  for (int i = 0; i < 10; ++i) {
    scope.add(makeTask().scheduleOn(executor));
  }
  CO_ASSERT_EQ(10, scope.remaining());

  co_await scope.cancelAndJoinAsync();
  CO_ASSERT_EQ(true, scope.isScopeCancellationRequested());
  CO_ASSERT_EQ(0, scope.remaining());

  // construct scope using external CancellationSource and cancel using the
  // external cancellationSource
  folly::CancellationSource source;
  folly::coro::CancellableAsyncScope scope2(source.getToken());

  CO_ASSERT_EQ(0, scope2.remaining());
  for (int i = 0; i < 10; ++i) {
    scope2.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }
  CO_ASSERT_EQ(10, scope2.remaining());
  CO_ASSERT_EQ(false, scope2.isScopeCancellationRequested());

  source.requestCancellation();
  CO_ASSERT_EQ(true, scope2.isScopeCancellationRequested());
  co_await scope2.joinAsync();
  CO_ASSERT_EQ(0, scope2.remaining());

  source = {};
  // construct scope using external CancellationSource and cancel using the
  // class's cancellation source
  folly::coro::CancellableAsyncScope scope3(source.getToken());

  CO_ASSERT_EQ(0, scope3.remaining());
  for (int i = 0; i < 10; ++i) {
    scope3.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }
  CO_ASSERT_EQ(10, scope3.remaining());
  CO_ASSERT_EQ(false, scope3.isScopeCancellationRequested());
  co_await scope3.cancelAndJoinAsync();
  CO_ASSERT_EQ(true, scope3.isScopeCancellationRequested());
  CO_ASSERT_EQ(0, scope3.remaining());

  source = {};
  // default scope construction; each task is added with custom cancellation
  // token
  folly::coro::CancellableAsyncScope scope4;
  CO_ASSERT_EQ(0, scope4.remaining());
  for (int i = 0; i < 10; ++i) {
    scope4.add(
        makeTask().scheduleOn(folly::getGlobalCPUExecutor()),
        source.getToken());
  }
  CO_ASSERT_EQ(10, scope4.remaining());
  source.requestCancellation();
  CO_ASSERT_EQ(source.isCancellationRequested(), true);
  CO_ASSERT_EQ(false, scope4.isScopeCancellationRequested());
  co_await scope4.joinAsync();
  // this is false since we the token that is used is not part of the AsyncScope
  // state
  CO_ASSERT_EQ(false, scope4.isScopeCancellationRequested());
  CO_ASSERT_EQ(0, scope4.remaining());
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

  folly::CancellationSource source;
  folly::coro::CancellableAsyncScope scope2(source.getToken());

  CO_ASSERT_EQ(0, scope2.remaining());
  for (int i = 0; i < 10; ++i) {
    scope2.add(makeTask().scheduleOn(folly::getGlobalCPUExecutor()));
  }
  CO_ASSERT_EQ(10, scope2.remaining());

  source.requestCancellation();
  co_await scope2.joinAsync();
  CO_ASSERT_EQ(0, scope2.remaining());

  source = {};
  folly::coro::CancellableAsyncScope scope3;

  CO_ASSERT_EQ(0, scope3.remaining());
  for (int i = 0; i < 10; ++i) {
    scope3.add(
        makeTask().scheduleOn(folly::getGlobalCPUExecutor()),
        source.getToken());
  }
  CO_ASSERT_EQ(10, scope3.remaining());

  source.requestCancellation();
  co_await scope3.joinAsync();
  CO_ASSERT_EQ(0, scope3.remaining());
}

CO_TEST_F(CancellableAsyncScopeTest, CancelSuspendedWorkCoSchedule) {
  using namespace std::chrono_literals;

  auto makeTask = [&]() -> folly::coro::Task<> {
    co_await folly::coro::sleep(300s);
  };

  folly::coro::CancellableAsyncScope scope;

  CO_ASSERT_EQ(0, scope.remaining());
  for (int i = 0; i < 10; ++i) {
    co_await scope.co_schedule(makeTask());
  }
  CO_ASSERT_EQ(10, scope.remaining());

  // Although we are suspended while sleeping, cancelAndJoinAsync will handle
  // this correctly.
  co_await scope.cancelAndJoinAsync();
  CO_ASSERT_EQ(0, scope.remaining());
}

#endif // FOLLY_HAS_COROUTINES
