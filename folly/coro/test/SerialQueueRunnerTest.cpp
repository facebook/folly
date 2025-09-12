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

#include <folly/coro/SerialQueueRunner.h>

#include <folly/coro/AsyncScope.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

struct QueueRunnerTest : testing::Test {};

CO_TEST_F(QueueRunnerTest, example) {
  folly::coro::AsyncScope scope;
  folly::coro::SerialQueueRunner queue;

  // fork the runner
  scope.add(
      co_withExecutor(co_await folly::coro::co_current_executor, queue.run()));

  // launch work items in the runner's queue
  std::vector<size_t> nums;
  for (size_t i = 0; i < 10; ++i) {
    co_await folly::coro::co_reschedule_on_current_executor;
    queue.add(folly::coro::co_invoke([&nums, i]() -> folly::coro::Task<> {
      for (size_t j = 0; j < 10; ++j) {
        co_await folly::coro::co_reschedule_on_current_executor;
        nums.push_back(i);
      }
    }));
  }
  co_await folly::coro::co_reschedule_on_current_executor;
  queue.done();

  // join the runner
  co_await scope.joinAsync();

  // quick expectations
  EXPECT_TRUE(std::is_sorted(nums.begin(), nums.end()));
  EXPECT_EQ(100, nums.size());

  // full expectation
  std::vector<size_t> expected;
  for (size_t i = 0; i < 10; ++i) {
    for (size_t j = 0; j < 10; ++j) {
      expected.push_back(i);
    }
  }
  EXPECT_EQ(expected, nums);
}

#endif
