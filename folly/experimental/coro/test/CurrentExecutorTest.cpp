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
#include <folly/experimental/coro/CurrentExecutor.h>
#include <folly/experimental/coro/Invoke.h>
#include <folly/experimental/coro/Task.h>

#include <folly/portability/GTest.h>

class CoRescheduleOnCurrentExecutorTest : public testing::Test {};

TEST_F(CoRescheduleOnCurrentExecutorTest, example) {
  std::vector<int> results;
  folly::coro::blockingWait(folly::coro::collectAll(
      folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        for (int i = 0; i <= 10; i += 2) {
          if (i == 6) {
            co_await folly::coro::co_reschedule_on_current_executor;
          }
          results.push_back(i);
        }
      }),
      folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        for (int i = 1; i < 10; i += 2) {
          if (i == 7) {
            co_await folly::coro::co_reschedule_on_current_executor;
          }
          results.push_back(i);
        }
      })));

  CHECK_EQ(11, results.size());
  const int expected[11] = {0, 2, 4, 1, 3, 5, 6, 8, 10, 7, 9};
  for (int i = 0; i < 11; ++i) {
    CHECK_EQ(expected[i], results[i]);
  }
}

#endif
