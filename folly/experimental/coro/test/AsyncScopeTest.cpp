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

#include <folly/experimental/coro/AsyncScope.h>

#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

struct AsyncScopeTest : public testing::Test {};

TEST_F(AsyncScopeTest, ConstructDestruct) {
  // Safe to construct/destruct an AsyncScope without calling any methods.
  folly::coro::AsyncScope scope;
}

TEST_F(AsyncScopeTest, AddAndJoin) {
  folly::coro::blockingWait([]() -> folly::coro::Task<> {
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

    CHECK(count == 100);
  }());
}

TEST_F(AsyncScopeTest, StartChildTasksAfterCleanupStarted) {
  folly::coro::blockingWait([]() -> folly::coro::Task<> {
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

    CHECK(childFinished);
  }());
}

#endif // FOLLY_HAS_COROUTINES
