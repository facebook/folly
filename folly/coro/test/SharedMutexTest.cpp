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

#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/SharedMutex.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/portability/GTest.h>

#include <mutex>

#if FOLLY_HAS_COROUTINES

using namespace folly;

class SharedMutexTest : public testing::Test {};

TEST_F(SharedMutexTest, TryLock) {
  // the mutex can only be one of the following five states
  struct MutexStateBase : private folly::NonCopyableNonMovable {
    virtual ~MutexStateBase() {}
    coro::SharedMutex m{};
    bool upgraded{false};
  };
  struct Unlocked : public MutexStateBase {};
  struct ExclusivelyLocked : public MutexStateBase {
    ExclusivelyLocked() { CHECK(m.try_lock()); }
    ~ExclusivelyLocked() override { m.unlock(); }
  };
  struct SharedLockedOnly : public MutexStateBase {
    SharedLockedOnly() { CHECK(m.try_lock_shared()); }
    ~SharedLockedOnly() override { m.unlock_shared(); }
  };
  struct UpgradeLockedOnly : public MutexStateBase {
    UpgradeLockedOnly() { CHECK(m.try_lock_upgrade()); }
    ~UpgradeLockedOnly() override {
      if (upgraded) {
        m.unlock();
      } else {
        m.unlock_upgrade();
      }
    }
  };
  struct UpgradeAndSharedLocked : public MutexStateBase {
    UpgradeAndSharedLocked() {
      CHECK(m.try_lock_upgrade());
      CHECK(m.try_lock_shared());
    }
    ~UpgradeAndSharedLocked() override {
      if (upgraded) {
        m.unlock();
      } else {
        m.unlock_upgrade();
      }
      m.unlock_shared();
    }
  };
  // all possible actions
  auto lock_success = [](MutexStateBase& state) {
    CHECK(state.m.try_lock());
    state.m.unlock();
  };
  auto lock_fail = [](MutexStateBase& state) { CHECK(!state.m.try_lock()); };
  auto lock_upgrade_success = [](MutexStateBase& state) {
    CHECK(state.m.try_lock_upgrade());
    state.m.unlock_upgrade();
  };
  auto lock_upgrade_fail = [](MutexStateBase& state) {
    CHECK(!state.m.try_lock_upgrade());
  };
  auto lock_shared_success = [](MutexStateBase& state) {
    CHECK(state.m.try_lock_shared());
    state.m.unlock_shared();
  };
  auto lock_shared_fail = [](MutexStateBase& state) {
    CHECK(!state.m.try_lock_shared());
  };
  auto unlock_upgrade_and_lock_success = [](MutexStateBase& state) {
    CHECK(state.m.try_unlock_upgrade_and_lock());
    state.upgraded = true;
  };
  auto unlock_upgrade_and_lock_fail = [](MutexStateBase& state) {
    CHECK(!state.m.try_unlock_upgrade_and_lock());
  };

  std::vector<std::tuple<
      std::unique_ptr<MutexStateBase> /* initial state */,
      std::function<void(MutexStateBase&)> /* action */
      >>
      cases;

  // all state transitions and expected outcome
  cases.emplace_back(std::make_unique<Unlocked>(), lock_success);
  cases.emplace_back(std::make_unique<Unlocked>(), lock_upgrade_success);
  cases.emplace_back(std::make_unique<Unlocked>(), lock_shared_success);

  cases.emplace_back(std::make_unique<ExclusivelyLocked>(), lock_fail);
  cases.emplace_back(std::make_unique<ExclusivelyLocked>(), lock_upgrade_fail);
  cases.emplace_back(std::make_unique<ExclusivelyLocked>(), lock_shared_fail);

  cases.emplace_back(std::make_unique<SharedLockedOnly>(), lock_fail);
  cases.emplace_back(
      std::make_unique<SharedLockedOnly>(), lock_upgrade_success);
  cases.emplace_back(std::make_unique<SharedLockedOnly>(), lock_shared_success);

  cases.emplace_back(std::make_unique<UpgradeLockedOnly>(), lock_fail);
  cases.emplace_back(
      std::make_unique<UpgradeLockedOnly>(), lock_shared_success);
  cases.emplace_back(std::make_unique<UpgradeLockedOnly>(), lock_upgrade_fail);
  cases.emplace_back(
      std::make_unique<UpgradeLockedOnly>(), unlock_upgrade_and_lock_success);

  cases.emplace_back(std::make_unique<UpgradeAndSharedLocked>(), lock_fail);
  cases.emplace_back(
      std::make_unique<UpgradeAndSharedLocked>(), lock_shared_success);
  cases.emplace_back(
      std::make_unique<UpgradeAndSharedLocked>(), lock_upgrade_fail);
  cases.emplace_back(
      std::make_unique<UpgradeAndSharedLocked>(), unlock_upgrade_and_lock_fail);

  for (auto& [state, action] : cases) {
    action(*state);
  }
}

TEST_F(SharedMutexTest, ManualLockAsync) {
  coro::SharedMutex mutex;
  int value = 0;

  auto makeReaderTask = [&](coro::Baton& b) -> coro::Task<int> {
    co_await mutex.co_lock_shared();
    int valueCopy = value;
    co_await b;
    mutex.unlock_shared();
    co_return valueCopy;
  };

  auto makeWriterTask = [&](coro::Baton& b) -> coro::Task<void> {
    co_await mutex.co_lock();
    co_await b;
    value += 1;
    mutex.unlock();
  };

  ManualExecutor executor;

  {
    coro::Baton b1;
    coro::Baton b2;
    coro::Baton b3;
    coro::Baton b4;
    coro::Baton b5;

    auto r1 = makeReaderTask(b1).scheduleOn(&executor).start();
    auto r2 = makeReaderTask(b2).scheduleOn(&executor).start();
    auto w1 = makeWriterTask(b3).scheduleOn(&executor).start();
    auto w2 = makeWriterTask(b4).scheduleOn(&executor).start();
    auto r3 = makeReaderTask(b5).scheduleOn(&executor).start();
    executor.drain();

    b1.post();
    executor.drain();
    CHECK_EQ(0, std::move(r1).get());

    b2.post();
    executor.drain();
    CHECK_EQ(0, std::move(r2).get());

    b3.post();
    executor.drain();
    CHECK_EQ(1, value);

    b4.post();
    executor.drain();
    CHECK_EQ(2, value);

    // This reader should have had to wait for the prior two write locks
    // to complete before it acquired the read-lock.
    b5.post();
    executor.drain();
    CHECK_EQ(2, std::move(r3).get());
  }
}

TEST_F(SharedMutexTest, ScopedLockAsync) {
  coro::SharedMutex mutex;
  int value = 0;

  auto makeReaderTask = [&](coro::Baton& b) -> coro::Task<int> {
    auto lock = co_await mutex.co_scoped_lock_shared();
    co_await b;
    co_return value;
  };

  auto makeWriterTask = [&](coro::Baton& b) -> coro::Task<void> {
    auto lock = co_await mutex.co_scoped_lock();
    co_await b;
    value += 1;
  };

  ManualExecutor executor;

  {
    coro::Baton b1;
    coro::Baton b2;
    coro::Baton b3;
    coro::Baton b4;
    coro::Baton b5;

    auto r1 = makeReaderTask(b1).scheduleOn(&executor).start();
    auto r2 = makeReaderTask(b2).scheduleOn(&executor).start();
    auto w1 = makeWriterTask(b3).scheduleOn(&executor).start();
    auto w2 = makeWriterTask(b4).scheduleOn(&executor).start();
    auto r3 = makeReaderTask(b5).scheduleOn(&executor).start();

    b1.post();
    executor.drain();
    CHECK_EQ(0, std::move(r1).get());

    b2.post();
    executor.drain();
    CHECK_EQ(0, std::move(r2).get());

    b3.post();
    executor.drain();
    CHECK_EQ(1, value);

    b4.post();
    executor.drain();
    CHECK_EQ(2, value);

    // This reader should have had to wait for the prior two write locks
    // to complete before it acquired the read-lock.
    b5.post();
    executor.drain();
    CHECK_EQ(2, std::move(r3).get());
  }
}

TEST_F(SharedMutexTest, ThreadSafety) {
  // Spin up a thread-pool with 3 threads and 6 coroutines
  // (2 writers, 4 readers) that are constantly spinning in a loop reading
  // and modifying some shared state.

  CPUThreadPoolExecutor threadPool{
      3, std::make_shared<NamedThreadFactory>("TestThreadPool")};

  static constexpr int iterationCount = 100'000;

  coro::SharedMutex mutex;
  int value1 = 0;
  int value2 = 0;

  auto makeWriterTask = [&]() -> coro::Task<void> {
    for (int i = 0; i < iterationCount; ++i) {
      auto lock = co_await mutex.co_scoped_lock();
      ++value1;
      ++value2;
    }
  };

  auto makeReaderTask = [&]() -> coro::Task<void> {
    for (int i = 0; i < iterationCount; ++i) {
      auto lock = co_await mutex.co_scoped_lock_shared();
      CHECK_EQ(value1, value2);
    }
  };

  auto w1 = makeWriterTask().scheduleOn(&threadPool).start();
  auto w2 = makeWriterTask().scheduleOn(&threadPool).start();
  auto r1 = makeReaderTask().scheduleOn(&threadPool).start();
  auto r2 = makeReaderTask().scheduleOn(&threadPool).start();
  auto r3 = makeReaderTask().scheduleOn(&threadPool).start();
  auto r4 = makeReaderTask().scheduleOn(&threadPool).start();

  std::move(w1).get();
  std::move(w2).get();
  std::move(r1).get();
  std::move(r2).get();
  std::move(r3).get();
  std::move(r4).get();

  CHECK_EQ(value1, 2 * iterationCount);
  CHECK_EQ(value2, 2 * iterationCount);
}

#endif
