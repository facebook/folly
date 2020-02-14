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
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/FutureUtil.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

static folly::coro::Task<int> makeTask() {
  co_return 42;
}
static folly::coro::AsyncGenerator<int> makeGen() {
  co_yield 42;
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
#endif
