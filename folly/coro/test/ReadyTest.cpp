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

#include <folly/coro/AwaitResult.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Ready.h>
#include <folly/coro/safe/NowTask.h>

#if FOLLY_HAS_RESULT

namespace folly::coro {

CO_TEST(ReadyTest, co_ready_of_error_result) {
  auto errorTaskFn = []() -> now_task<> {
    result<> res = non_value_result{std::runtime_error{"foo"}};
    try {
      co_await co_ready{std::move(res)}; // will not throw
    } catch (...) {
    }
    LOG(FATAL) << "not reached";
  };
  auto res = co_await co_await_result(errorTaskFn());
  EXPECT_STREQ("foo", get_exception<std::runtime_error>(res)->what());
}

CO_TEST(ReadyTest, co_ready_of_value_result) {
  auto valueTaskFn = []() -> now_task<int> {
    result res = 1300;
    co_return 37 + co_await co_ready{std::move(res)};
  };
  EXPECT_EQ(1337, co_await valueTaskFn());

  // Use a move-only value to check we don't have extraneous copies
  auto moveValueTaskFn = []() -> now_task<std::unique_ptr<int>> {
    result res = std::make_unique<int>(1300);
    auto np = co_await co_ready{std::move(res)};
    *np += 37;
    co_return std::move(np);
  };
  EXPECT_EQ(1337, *(co_await moveValueTaskFn()));
}

CO_TEST(ReadyTest, co_ready_of_void_result) {
  auto taskFn = [&]() -> now_task<int> {
    result<> res;
    co_await co_ready{std::move(res)};
    co_return 42;
  };
  EXPECT_EQ(42, co_await taskFn());
}

CO_TEST(ReadyTest, co_ready_stopped) {
  auto taskFn = [&]() -> now_task<int> {
    result<> res = stopped_result;
    co_await co_ready{std::move(res)};
    co_return 42;
  };
  EXPECT_TRUE((co_await co_await_result(taskFn())).has_stopped());
}

} // namespace folly::coro

#endif
