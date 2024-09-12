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

#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Collect.h>
#include <folly/experimental/coro/FutureUtil.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

static folly::coro::Task<int> makeTask() {
  co_return 42;
}
static folly::coro::AsyncGenerator<int> makeGen() {
  co_yield 42;
}
static folly::coro::Task<void> makeVoidTask() {
  co_return;
}

TEST(FutureUtilTest, ToTask) {
  EXPECT_EQ(folly::coro::blockingWait(folly::coro::toTask(makeTask())), 42);

  auto gen = makeGen();
  EXPECT_EQ(*folly::coro::blockingWait(folly::coro::toTask(gen.next())), 42);

  folly::coro::Baton baton;
  auto task = folly::coro::toTask(std::ref(baton));
  baton.post();
  folly::coro::blockingWait(std::move(task));
}

TEST(FutureUtilTest, ToSemiFuture) {
  folly::ManualExecutor ex;

  auto semi = folly::coro::toSemiFuture(makeTask());
  EXPECT_FALSE(semi.isReady());
  semi = std::move(semi).via(&ex);
  EXPECT_FALSE(semi.isReady());
  ex.drain();
  EXPECT_TRUE(semi.isReady());
  EXPECT_EQ(std::move(semi).get(), 42);

  auto gen = makeGen();
  auto semi2 = folly::coro::toSemiFuture(gen.next());
  EXPECT_FALSE(semi2.isReady());
  semi2 = std::move(semi2).via(&ex);
  EXPECT_FALSE(semi2.isReady());
  ex.drain();
  EXPECT_TRUE(semi2.isReady());
  EXPECT_EQ(*std::move(semi2).get(), 42);

  folly::coro::Baton baton;
  auto semi3 = folly::coro::toSemiFuture(std::ref(baton));
  EXPECT_FALSE(semi3.isReady());
  semi3 = std::move(semi3).via(&ex);
  EXPECT_FALSE(semi3.isReady());
  ex.drain();
  EXPECT_FALSE(semi3.isReady());
  baton.post();
  ex.drain();
  EXPECT_TRUE(semi3.isReady());
}

TEST(FutureUtilTest, ToFuture) {
  folly::ManualExecutor ex;

  auto fut = folly::coro::toFuture(makeTask(), &ex);
  EXPECT_FALSE(fut.isReady());
  ex.drain();
  EXPECT_TRUE(fut.isReady());
  EXPECT_EQ(std::move(fut).get(), 42);

  auto gen = makeGen();
  auto fut2 = folly::coro::toFuture(gen.next(), &ex);
  EXPECT_FALSE(fut2.isReady());
  ex.drain();
  EXPECT_TRUE(fut2.isReady());
  EXPECT_EQ(*std::move(fut2).get(), 42);

  folly::coro::Baton baton;
  auto fut3 = folly::coro::toFuture(std::ref(baton), &ex);
  EXPECT_FALSE(fut3.isReady());
  ex.drain();
  EXPECT_FALSE(fut3.isReady());
  baton.post();
  ex.drain();
  EXPECT_TRUE(fut3.isReady());
}

TEST(FutureUtilTest, VoidRoundtrip) {
  folly::coro::Task<void> task = makeVoidTask();
  folly::SemiFuture<folly::Unit> semi =
      folly::coro::toSemiFuture(std::move(task));
  task = folly::coro::toTask(std::move(semi));
  folly::coro::blockingWait(std::move(task));
}

TEST(FutureUtilTest, ToTaskInterruptOnCancelFutureWithCancellation) {
  auto [p, f] = folly::makePromiseContract<folly::Unit>();

  // to verify that cancellation propagates into the future interrupt-handler
  folly::exception_wrapper interrupt;
  p.setInterruptHandler([&, p_ = &p](auto&& ew) {
    interrupt = ew;
    p_->setException(std::move(ew));
  });

  // to verify that deferred work runs
  folly::Try<folly::Unit> touched;
  auto f1 = std::move(f).defer([&](folly::Try<folly::Unit> t) { touched = t; });
  ASSERT_FALSE(touched.tryGetExceptionObject()); // sanity check

  // run the scenario within blocking-wait
  auto result = folly::coro::blocking_wait(
      std::invoke([&, f_ = &f1]() -> folly::coro::Task<folly::Try<void>> {
        folly::CancellationSource csource;

        co_return std::get<0>(co_await folly::coro::collectAllTry(

            folly::coro::co_withCancellation(
                csource.getToken(),
                // a task that will be cancelled, wrapping a future
                std::invoke([&]() -> folly::coro::Task<> {
                  EXPECT_FALSE(touched.tryGetExceptionObject()); // sanity check
                  co_await folly::coro::toTaskInterruptOnCancel(std::move(*f_));
                })),

            // a task that will do the cancelling, after waiting a bit
            std::invoke([&]() -> folly::coro::Task<> {
              EXPECT_FALSE(touched.tryGetExceptionObject()); // sanity check
              co_await folly::coro::co_reschedule_on_current_executor;
              EXPECT_FALSE(touched.tryGetExceptionObject()); // sanity check
              csource.requestCancellation();
              EXPECT_FALSE(touched.tryGetExceptionObject()); // sanity check
              co_await folly::coro::co_reschedule_on_current_executor;
              EXPECT_TRUE( // sanity check: events happen here
                  touched.tryGetExceptionObject<folly::FutureCancellation>());
            })));
      }));

  EXPECT_TRUE(touched.tryGetExceptionObject<folly::FutureCancellation>());
  EXPECT_TRUE(result.tryGetExceptionObject<folly::OperationCancelled>());
  EXPECT_TRUE(interrupt.get_exception<folly::FutureCancellation>());
}

#endif
