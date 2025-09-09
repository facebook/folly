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
#include <folly/coro/safe/NowTask.h>
#include <folly/result/coro.h>

#if FOLLY_HAS_RESULT

namespace folly::coro {

CO_TEST(ReadyTest, orUnwindOfErrorResult) {
  auto errorTaskFn = [](auto&& res) -> now_task<> {
    try {
      co_await or_unwind{static_cast<decltype(res)>(res)}; // will not throw
    } catch (...) {
    }
    ADD_FAILURE() << "not reached";
  };
  result<> r1 = non_value_result{std::runtime_error{"foo"}};
  { // pass lvalue
    auto r2 = co_await co_await_result(errorTaskFn(r1));
    EXPECT_STREQ("foo", get_exception<std::runtime_error>(r2)->what());
  }
  { // pass const lvalue
    auto r2 = co_await co_await_result(errorTaskFn(std::as_const(r1)));
    EXPECT_STREQ("foo", get_exception<std::runtime_error>(r2)->what());
  }
  { // pass rvalue
    auto r2 = co_await co_await_result(errorTaskFn(std::move(r1)));
    EXPECT_STREQ("foo", get_exception<std::runtime_error>(r2)->what());
  }
}

CO_TEST(ReadyTest, orUnwindOfValueResult) {
  auto valueTaskFn = [](auto&& res) -> now_task<int> {
    co_return 37 + co_await or_unwind{static_cast<decltype(res)>(res)};
  };
  result res = 1300;
  EXPECT_EQ(1337, co_await valueTaskFn(res));
  EXPECT_EQ(1337, co_await valueTaskFn(std::as_const(res)));
  EXPECT_EQ(1337, co_await valueTaskFn(std::move(res)));
}

CO_TEST(ReadyTest, orUnwindOfMoveOnlyValueResult) { // No extraneous copies
  auto moveValueTaskFn = []() -> now_task<std::unique_ptr<int>> {
    result res = std::make_unique<int>(1300);
    auto np = co_await or_unwind{std::move(res)};
    *np += 37;
    co_return std::move(np);
  };
  EXPECT_EQ(1337, *(co_await moveValueTaskFn()));
}

CO_TEST(ReadyTest, orUnwindOfPrvalueResult) { // Prvalue lifetime
  auto moveValueTaskFn = []() -> now_task<std::unique_ptr<int>> {
    auto np = co_await or_unwind{result{std::make_unique<int>(1300)}};
    *np += 37;
    co_return std::move(np);
  };
  EXPECT_EQ(1337, *(co_await moveValueTaskFn()));
}

CO_TEST(ReadyTest, orUnwindOfVoidResult) {
  auto taskFn = [&](auto&& res) -> now_task<int> {
    co_await or_unwind{static_cast<decltype(res)>(res)};
    co_return 42;
  };
  result<> res;
  EXPECT_EQ(42, co_await taskFn(res));
  EXPECT_EQ(42, co_await taskFn(std::as_const(res)));
  EXPECT_EQ(42, co_await taskFn(std::move(res)));
}

CO_TEST(ReadyTest, orUnwindStopped) {
  auto taskFn = [&](auto&& res) -> now_task<int> {
    co_await or_unwind{static_cast<decltype(res)>(res)};
    co_return 42;
  };
  result<> res = stopped_result;
  EXPECT_TRUE((co_await co_await_result(taskFn(res))).has_stopped());
  EXPECT_TRUE(
      (co_await co_await_result(taskFn(std::as_const(res)))).has_stopped());
  EXPECT_TRUE((co_await co_await_result(taskFn(std::move(res)))).has_stopped());
}

} // namespace folly::coro

#endif
