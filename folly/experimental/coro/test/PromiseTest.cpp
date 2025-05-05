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

#include <folly/experimental/coro/Promise.h>

#include <tuple>

#include <folly/Portability.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

using namespace folly;
using namespace ::testing;

static_assert(
    std::is_move_assignable<folly::coro::Promise<void>>::value,
    "promise should be move assignable");
static_assert(
    std::is_move_assignable<folly::coro::Future<void>>::value,
    "future should be move assignable");

CO_TEST(PromiseTest, ImmediateValue) {
  auto [promise, future] = coro::makePromiseContract<int>();
  promise.setValue(42);
  EXPECT_EQ(co_await std::move(future), 42);
}

CO_TEST(PromiseTest, ImmediateTry) {
  auto [promise, future] = coro::makePromiseContract<int>();
  promise.setResult(folly::Try(42));
  auto res = co_await co_awaitTry(std::move(future));
  EXPECT_EQ(res.value(), 42);
}

CO_TEST(PromiseTest, ImmediateException) {
  auto [promise, future] = coro::makePromiseContract<int>();
  promise.setException(std::runtime_error(""));
  auto res = co_await co_awaitTry(std::move(future));
  EXPECT_TRUE(res.hasException<std::runtime_error>());
}

CO_TEST(PromiseTest, ImmediateExceptionVoid) {
  auto [promise, future] = coro::makePromiseContract<void>();
  promise.setException(std::runtime_error(""));
  EXPECT_THROW(co_await std::move(future), std::runtime_error);
}

CO_TEST(PromiseTest, SuspendValue) {
  auto [promise, future] = coro::makePromiseContract<int>();
  auto waiter = [](auto future) -> coro::Task<int> {
    co_return co_await std::move(future);
  }(std::move(future));
  auto fulfiller = [](auto promise) -> coro::Task<> {
    promise.setValue(42);
    co_return;
  }(std::move(promise));

  auto [res, _] = co_await coro::collectAll(
      co_awaitTry(std::move(waiter)), std::move(fulfiller));

  EXPECT_EQ(res.value(), 42);
}

CO_TEST(PromiseTest, SuspendException) {
  auto [promise, future] = coro::makePromiseContract<int>();
  auto waiter = [](auto future) -> coro::Task<int> {
    co_return co_await std::move(future);
  }(std::move(future));
  auto fulfiller = [](auto promise) -> coro::Task<> {
    promise.setException(std::logic_error(""));
    co_return;
  }(std::move(promise));

  auto [res, _] = co_await coro::collectAll(
      co_awaitTry(std::move(waiter)), std::move(fulfiller));

  EXPECT_TRUE(res.hasException<std::logic_error>());
}

CO_TEST(PromiseTest, ImmediateCancel) {
  auto [promise, future] = coro::makePromiseContract<int>();
  CancellationSource cs;
  cs.requestCancellation();
  bool cancelled = false;
  CancellationCallback cb{
      promise.getCancellationToken(), [&] { cancelled = true; }};
  EXPECT_FALSE(cancelled);
  auto res = co_await co_awaitTry(
      co_withCancellation(cs.getToken(), std::move(future)));
  EXPECT_TRUE(cancelled);
  EXPECT_TRUE(res.hasException<OperationCancelled>());
  promise.setValue(42);
}

CO_TEST(PromiseTest, CancelFulfilled) {
  auto [promise, future] = coro::makePromiseContract<int>();
  promise.setValue(42);
  CancellationSource cs;
  cs.requestCancellation();
  bool cancelled = false;
  CancellationCallback cb{
      promise.getCancellationToken(), [&] { cancelled = true; }};
  auto res = co_await co_awaitTry(
      co_withCancellation(cs.getToken(), std::move(future)));
  EXPECT_FALSE(cancelled); // not signalled if already fulfilled
  EXPECT_EQ(res.value(), 42);
}

CO_TEST(PromiseTest, SuspendCancel) {
  auto [promise, future] = coro::makePromiseContract<int>();
  CancellationSource cs;
  bool cancelled = false;
  CancellationCallback cb{
      promise.getCancellationToken(), [&] { cancelled = true; }};
  auto waiter = [](auto future) -> coro::Task<int> {
    co_return co_await std::move(future);
  }(co_withCancellation(cs.getToken(), std::move(future)));
  auto fulfiller = [](auto cs) -> coro::Task<> {
    cs.requestCancellation();
    co_return;
  }(cs);

  auto [res, _] = co_await coro::collectAll(
      co_awaitTry(std::move(waiter)), std::move(fulfiller));

  EXPECT_TRUE(cancelled);
  EXPECT_TRUE(res.hasException<OperationCancelled>());
}

CO_TEST(PromiseTest, ImmediateBreakPromise) {
  auto [promise, future] = coro::makePromiseContract<int>();
  { auto p2 = std::move(promise); }
  auto res = co_await co_awaitTry(std::move(future));
  EXPECT_TRUE(res.hasException<BrokenPromise>());
}

CO_TEST(PromiseTest, SuspendBreakPromise) {
  auto [promise, future] = coro::makePromiseContract<int>();
  auto waiter = [](auto future) -> coro::Task<int> {
    co_return co_await std::move(future);
  }(std::move(future));
  auto fulfiller = [](auto promise) -> coro::Task<> {
    (void)promise;
    co_return;
  }(std::move(promise));

  auto [res, _] = co_await coro::collectAll(
      co_awaitTry(std::move(waiter)), std::move(fulfiller));

  EXPECT_TRUE(res.hasException<BrokenPromise>());
}

CO_TEST(PromiseTest, Lifetime) {
  struct Guard {
    int& destroyed;
    explicit Guard(int& d) : destroyed(d) {}
    Guard(Guard&&) = default;
    ~Guard() { destroyed++; }
  };

  int destroyed = 0;
  {
    auto [promise, future] = coro::makePromiseContract<Guard>();
    promise.setValue(Guard(destroyed));
    EXPECT_EQ(destroyed, 1); // the temporary
    co_await std::move(future);
    EXPECT_EQ(destroyed, 2); // the return value
  }
  EXPECT_EQ(destroyed, 3); // the slot in shared state
}

TEST(PromiseTest, DropFuture) {
  struct Guard {
    int& destroyed;
    explicit Guard(int& d) : destroyed(d) {}
    Guard(Guard&&) = default;
    ~Guard() { destroyed++; }
  };

  int destroyed = 0;
  {
    auto [promise, future] = coro::makePromiseContract<Guard>();
    promise.setValue(Guard(destroyed));
    EXPECT_EQ(destroyed, 1); // the temporary
  }
  EXPECT_EQ(destroyed, 2); // the slot in shared state
}

CO_TEST(PromiseTest, MoveOnly) {
  auto [promise, future] = coro::makePromiseContract<std::unique_ptr<int>>();
  promise.setValue(std::make_unique<int>(42));
  auto val = co_await std::move(future);
  EXPECT_EQ(*val, 42);
}

CO_TEST(PromiseTest, Void) {
  auto [promise, future] = coro::makePromiseContract<void>();
  promise.setValue();
  co_await std::move(future);
}

TEST(PromiseTest, IsReady) {
  auto [promise, future] = coro::makePromiseContract<int>();
  EXPECT_FALSE(future.isReady());
  promise.setValue(42);
  EXPECT_TRUE(future.isReady());
}

CO_TEST(PromiseTest, MakeFuture) {
  auto future = coro::makeFuture(42);
  EXPECT_TRUE(future.isReady());
  auto val = co_await std::move(future);
  EXPECT_EQ(val, 42);

  auto future2 = coro::makeFuture<int>(std::runtime_error(""));
  EXPECT_TRUE(future2.isReady());
  auto res = co_await co_awaitTry(std::move(future2));
  EXPECT_TRUE(res.hasException<std::runtime_error>());

  auto future3 = coro::makeFuture();
  EXPECT_TRUE(future3.isReady());
  auto res3 = co_await co_awaitTry(std::move(future3));
  EXPECT_TRUE(res3.hasValue());
}

CO_TEST(PromiseTest, MoveAssign) {
  coro::Promise<void> promise;
  coro::Future<void> future;
  std::tie(promise, future) = coro::makePromiseContract<void>();
  promise.setValue();
  co_await std::move(future);
}
#endif
