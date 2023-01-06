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
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#include <stdexcept>
#include <type_traits>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

namespace {
class ScopeExitTest : public testing::Test {
 protected:
  int count = 0;
};
} // namespace

TEST_F(ScopeExitTest, OneExitAction) {
  folly::coro::blockingWait([this]() -> Task<> {
    ++count;
    co_await co_scope_exit([this]() -> Task<> {
      count *= 2;
      co_return;
    });
    ++count;
  }());
  EXPECT_EQ(count, 4);
}

TEST_F(ScopeExitTest, TwoExitActions) {
  folly::coro::blockingWait([this]() -> Task<> {
    ++count;
    co_await co_scope_exit([this]() -> Task<> {
      count *= count;
      co_return;
    });
    co_await co_scope_exit([this]() -> Task<> {
      count *= 2;
      co_return;
    });
    ++count;
  }());
  EXPECT_EQ(count, 16);
}

TEST_F(ScopeExitTest, OneExitActionWithException) {
  EXPECT_THROW(
      folly::coro::blockingWait([this]() -> Task<> {
        ++count;
        co_await co_scope_exit([this]() -> Task<> {
          count *= 2;
          co_return;
        });
        throw std::runtime_error("Something bad happened!");
      }()),
      std::runtime_error);
  EXPECT_EQ(count, 2);
}

TEST_F(ScopeExitTest, ExceptionInExitActionCausesTermination) {
  ASSERT_DEATH(
      folly::coro::blockingWait([]() -> Task<> {
        co_await co_scope_exit([]() -> Task<> {
          throw std::runtime_error("Something bad happened!");
        });
      }()),
      "");
}

TEST_F(ScopeExitTest, ExceptionInExitActionDuringExceptionCausesTermination) {
  ASSERT_DEATH(
      folly::coro::blockingWait([]() -> Task<> {
        co_await co_scope_exit([]() -> Task<> {
          throw std::runtime_error("Something bad happened!");
        });
        throw std::runtime_error("Throwing from parent");
      }()),
      "Something bad happened!");
}

TEST_F(ScopeExitTest, StatefulExitAction) {
  folly::coro::blockingWait([this]() -> Task<> {
    auto&& [i] = co_await co_scope_exit(
        [this](int&& ii) -> Task<void> {
          count += ii;
          co_return;
        },
        3);
    ++count;
    i *= i;
  }());
  EXPECT_EQ(count, 10);
}

TEST_F(ScopeExitTest, NonMoveableState) {
  folly::coro::blockingWait([this]() -> Task<> {
    auto&& [asyncScope] = co_await co_scope_exit(
        [this](auto&& scope) -> Task<void> {
          co_await scope->joinAsync();
          count *= 2;
        },
        std::make_unique<AsyncScope>());

    auto ex = co_await co_current_executor;
    asyncScope->add(co_invoke([this]() -> Task<> {
                      ++count;
                      co_return;
                    }).scheduleOn(ex));
  }());
  EXPECT_EQ(count, 2);
}

TEST_F(ScopeExitTest, OneExitActionThroughNoThrow) {
  EXPECT_THROW(
      folly::coro::blockingWait([this]() -> Task<> {
        co_await co_scope_exit([this]() -> Task<> {
          ++count;
          co_return;
        });
        co_await co_nothrow([]() -> Task<> {
          throw std::runtime_error("Something bad happened!");
          co_return;
        }());
      }()),
      std::runtime_error);
  EXPECT_EQ(count, 1);
}

#endif // FOLLY_HAS_COROUTINES
