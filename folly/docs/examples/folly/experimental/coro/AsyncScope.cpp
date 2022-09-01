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

#include <folly/experimental/coro/AsyncScope.h>

#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/coro/GtestHelpers.h>

CO_TEST(AsyncScope, demo) {
  std::atomic<int> count = 0;
  auto incrementBy5 = [&]() -> folly::coro::Task<> {
    count += 5;
    co_return;
  };
  auto incrementBy10 = [&]() -> folly::coro::Task<> {
    count += 10;
    co_return;
  };
  auto incrementBy100 = [&]() -> folly::coro::Task<> {
    count += 100;
    co_return;
  };

  folly::coro::AsyncScope scope;
  scope.add(incrementBy5().scheduleOn(folly::getGlobalCPUExecutor()));
  scope.add(incrementBy10().scheduleOn(folly::getGlobalCPUExecutor()));
  scope.add(incrementBy100().scheduleOn(folly::getGlobalCPUExecutor()));

  co_await scope.joinAsync();
  EXPECT_EQ(count, 115);
}
