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

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/detail/InlineTask.h>
#include <folly/portability/GTest.h>

#include <mutex>

using namespace folly;

class MutexTest : public testing::Test {};

TEST_F(MutexTest, TryLock) {
  coro::Mutex m;
  CHECK(m.try_lock());
  CHECK(!m.try_lock());
  m.unlock();
  CHECK(m.try_lock());
}

TEST_F(MutexTest, ScopedLock) {
  coro::Mutex m;
  {
    std::unique_lock<coro::Mutex> lock{m, std::try_to_lock};
    CHECK(lock.owns_lock());

    {
      std::unique_lock<coro::Mutex> lock2{m, std::try_to_lock};
      CHECK(!lock2.owns_lock());
    }
  }

  CHECK(m.try_lock());
  m.unlock();
}

TEST_F(MutexTest, LockAsync) {
  coro::Mutex m;
  coro::Baton b1;
  coro::Baton b2;

  int value = 0;

  auto makeTask = [&](coro::Baton& b) -> coro::Task<void> {
    co_await m.co_lock();
    ++value;
    co_await b;
    ++value;
    m.unlock();
  };

  ManualExecutor executor;

  auto f1 = makeTask(b1).scheduleOn(&executor).start();
  executor.drain();
  CHECK_EQ(1, value);
  CHECK(!m.try_lock());

  auto f2 = makeTask(b2).scheduleOn(&executor).start();
  executor.drain();
  CHECK_EQ(1, value);

  // This will resume f1 coroutine and let it release the
  // lock. This will in turn resume f2 which was suspended
  // at co_await m.lockAsync() which will then increment the value
  // before becoming blocked on
  b1.post();
  executor.drain();

  CHECK_EQ(3, value);
  CHECK(!m.try_lock());

  b2.post();
  executor.drain();
  CHECK_EQ(4, value);
  CHECK(m.try_lock());
}

TEST_F(MutexTest, ScopedLockAsync) {
  coro::Mutex m;
  coro::Baton b1;
  coro::Baton b2;

  int value = 0;

  auto makeTask = [&](coro::Baton& b) -> coro::Task<void> {
    auto lock = co_await m.co_scoped_lock();
    ++value;
    co_await b;
    ++value;
  };

  ManualExecutor executor;

  auto f1 = makeTask(b1).scheduleOn(&executor).start();
  executor.drain();
  CHECK_EQ(1, value);
  CHECK(!m.try_lock());

  auto f2 = makeTask(b2).scheduleOn(&executor).start();
  executor.drain();
  CHECK_EQ(1, value);

  // This will resume f1 coroutine and let it release the
  // lock. This will in turn resume f2 which was suspended
  // at co_await m.lockAsync() which will then increment the value
  // before becoming blocked on b2.
  b1.post();
  executor.drain();

  CHECK_EQ(3, value);
  CHECK(!m.try_lock());

  b2.post();
  executor.drain();
  CHECK_EQ(4, value);
  CHECK(m.try_lock());
}

TEST_F(MutexTest, ThreadSafety) {
  CPUThreadPoolExecutor threadPool{
      2, std::make_shared<NamedThreadFactory>("CPUThreadPool")};

  int value = 0;
  coro::Mutex mutex;

  auto makeTask = [&]() -> coro::Task<void> {
    for (int i = 0; i < 10'000; ++i) {
      auto lock = co_await mutex.co_scoped_lock();
      ++value;
    }
  };

  auto f1 = makeTask().scheduleOn(&threadPool).start();
  auto f2 = makeTask().scheduleOn(&threadPool).start();
  auto f3 = makeTask().scheduleOn(&threadPool).start();

  std::move(f1).get();
  std::move(f2).get();
  std::move(f3).get();

  CHECK_EQ(30'000, value);
}

#endif
