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
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#include <stdio.h>

using namespace folly;

class BatonTest : public testing::Test {};

TEST_F(BatonTest, Ready) {
  coro::Baton b;
  CHECK(!b.ready());
  b.post();
  CHECK(b.ready());
  b.reset();
  CHECK(!b.ready());
}

TEST_F(BatonTest, InitiallyReady) {
  coro::Baton b{true};
  CHECK(b.ready());
  b.reset();
  CHECK(!b.ready());
}

TEST_F(BatonTest, AwaitBaton) {
  coro::Baton baton;
  bool reachedBeforeAwait = false;
  bool reachedAfterAwait = false;

  auto makeTask = [&]() -> coro::Task<void> {
    reachedBeforeAwait = true;
    co_await baton;
    reachedAfterAwait = true;
  };

  coro::Task<void> t = makeTask();

  CHECK(!reachedBeforeAwait);
  CHECK(!reachedAfterAwait);

  ManualExecutor executor;
  auto f = std::move(t).scheduleOn(&executor).start();
  executor.drain();

  CHECK(reachedBeforeAwait);
  CHECK(!reachedAfterAwait);

  baton.post();
  executor.drain();

  CHECK(reachedAfterAwait);
}

TEST_F(BatonTest, MultiAwaitBaton) {
  coro::Baton baton;

  bool reachedBeforeAwait1 = false;
  bool reachedBeforeAwait2 = false;
  bool reachedAfterAwait1 = false;
  bool reachedAfterAwait2 = false;

  auto makeTask1 = [&]() -> coro::Task<void> {
    reachedBeforeAwait1 = true;
    co_await baton;
    reachedAfterAwait1 = true;
  };

  auto makeTask2 = [&]() -> coro::Task<void> {
    reachedBeforeAwait2 = true;
    co_await baton;
    reachedAfterAwait2 = true;
  };

  coro::Task<void> t1 = makeTask1();
  coro::Task<void> t2 = makeTask2();

  ManualExecutor executor;
  auto f1 = std::move(t1).scheduleOn(&executor).start();
  auto f2 = std::move(t2).scheduleOn(&executor).start();
  executor.drain();

  CHECK(reachedBeforeAwait1);
  CHECK(reachedBeforeAwait2);
  CHECK(!reachedAfterAwait1);
  CHECK(!reachedAfterAwait2);

  baton.post();
  executor.drain();

  CHECK(f1.isReady());
  CHECK(f2.isReady());

  CHECK(reachedAfterAwait1);
  CHECK(reachedAfterAwait2);
}

#endif
