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

#include <folly/coro/AsyncScope.h>
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

    auto r1 = co_withExecutor(&executor, makeReaderTask(b1)).start();
    auto r2 = co_withExecutor(&executor, makeReaderTask(b2)).start();
    auto w1 = co_withExecutor(&executor, makeWriterTask(b3)).start();
    auto w2 = co_withExecutor(&executor, makeWriterTask(b4)).start();
    auto r3 = co_withExecutor(&executor, makeReaderTask(b5)).start();
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

void testAllStateTransitions(
    std::function<folly::coro::Task<void>(coro::SharedMutex&)> lock,
    std::function<folly::coro::Task<void>(coro::SharedMutex&)> lock_upgrade,
    std::function<folly::coro::Task<void>(coro::SharedMutex&)> lock_shared,
    std::function<folly::coro::Task<void>(coro::SharedMutex&)>
        unlock_upgrade_and_lock) {
  // all possible initial state
  auto unlocked = [](coro::SharedMutex&) {};
  auto exclusively_locked = [](coro::SharedMutex& m) { CHECK(m.try_lock()); };
  auto shared_locked_only = [](coro::SharedMutex& m) {
    CHECK(m.try_lock_shared());
  };
  auto upgrade_locked_only = [](coro::SharedMutex& m) {
    CHECK(m.try_lock_upgrade());
  };
  auto upgrade_and_shared_locked = [](coro::SharedMutex& m) {
    CHECK(m.try_lock_shared());
    CHECK(m.try_lock_upgrade());
  };

  // cleanup helpers
  auto unlock = [](coro::SharedMutex& m) { m.unlock(); };
  auto unlock_shared = [](coro::SharedMutex& m) { m.unlock_shared(); };
  auto unlock_upgrade = [](coro::SharedMutex& m) { m.unlock_upgrade(); };
  // unlock in different order changes the waiter list and the mutex state
  auto unlock_upgrade_and_unlock_shared = [](coro::SharedMutex& m) {
    m.unlock_upgrade();
    m.unlock_shared();
  };
  auto unlock_shared_and_unlock_upgrade = [](coro::SharedMutex& m) {
    m.unlock_shared();
    m.unlock_upgrade();
  };

  std::vector<std::tuple<
      std::string /* test name */,
      std::function<void(coro::SharedMutex&)> /* set up initial state*/,
      std::optional<std::function<coro::Task<void>(
          coro::SharedMutex&)>> /* optionally arrange a waiter */,
      std::function<coro::Task<void>(coro::SharedMutex&)> /* action */,
      bool /* expect the task to complete or not */,
      std::optional<std::function<void(coro::SharedMutex&)>> /* optional
                                                                  cleanup */
      >>
      cases;

  // unlocked & no one waiting
  cases.emplace_back(
      "unlocked-no-waiter-lock",
      unlocked,
      std::nullopt,
      lock,
      true,
      std::nullopt);
  cases.emplace_back(
      "unlocked-no-waiter-lock_upgrade",
      unlocked,
      std::nullopt,
      lock_upgrade,
      true,
      std::nullopt);
  cases.emplace_back(
      "unlocked-no-waiter-lock_shared",
      unlocked,
      std::nullopt,
      lock_shared,
      true,
      std::nullopt);

  // locked with no waiters
  cases.emplace_back(
      "locked-no-waiter-lock",
      exclusively_locked,
      std::nullopt,
      lock,
      false,
      unlock);
  cases.emplace_back(
      "locked-no-waiter-lock_upgrade",
      exclusively_locked,
      std::nullopt,
      lock_upgrade,
      false,
      unlock);
  cases.emplace_back(
      "locked-no-waiter-lock_shared",
      exclusively_locked,
      std::nullopt,
      lock_shared,
      false,
      unlock);

  // locked with waiters
  cases.emplace_back(
      "locked-lock-waiter-lock", exclusively_locked, lock, lock, false, unlock);
  cases.emplace_back(
      "locked-lock-waiter-lock_upgrade",
      exclusively_locked,
      lock,
      lock_upgrade,
      false,
      unlock);
  cases.emplace_back(
      "locked-lock-waiter-lock_shared",
      exclusively_locked,
      lock,
      lock_shared,
      false,
      unlock);

  cases.emplace_back(
      "locked-lock_shared-waiter-lock",
      exclusively_locked,
      lock_shared,
      lock,
      false,
      unlock);
  cases.emplace_back(
      "locked-lock_shared-waiter-lock_upgrade",
      exclusively_locked,
      lock_shared,
      lock_upgrade,
      false,
      unlock);
  cases.emplace_back(
      "locked-lock_shared-waiter-lock_shared",
      exclusively_locked,
      lock_shared,
      lock_shared,
      false,
      unlock);

  cases.emplace_back(
      "locked-lock_upgrade-waiter-lock",
      exclusively_locked,
      lock_upgrade,
      lock,
      false,
      unlock);
  cases.emplace_back(
      "locked-lock_upgrade-waiter-lock_upgrade",
      exclusively_locked,
      lock_upgrade,
      lock_upgrade,
      false,
      unlock);
  cases.emplace_back(
      "locked-lock_upgrade-waiter-lock_shared",
      exclusively_locked,
      lock_upgrade,
      lock_shared,
      false,
      unlock);

  // shared locked and no one waiting
  cases.emplace_back(
      "shared_locked-no-waiter-lock",
      shared_locked_only,
      std::nullopt,
      lock,
      false,
      unlock_shared);
  cases.emplace_back(
      "shared_locked-no-waiter-lock_upgrade",
      shared_locked_only,
      std::nullopt,
      lock_upgrade,
      true,
      unlock_shared);
  cases.emplace_back(
      "shared_locked-no-waiter-lock_shared",
      shared_locked_only,
      std::nullopt,
      lock_shared,
      true,
      unlock_shared);

  // shared locked with waiters
  cases.emplace_back(
      "shared_locked-lock-waiter-lock",
      shared_locked_only,
      lock,
      lock,
      false,
      unlock_shared);
  // this is the case where lock_upgrade wait for waiting writers to
  // avoid writer starvation
  cases.emplace_back(
      "shared_locked-lock-waiter-lock_upgrade",
      shared_locked_only,
      lock,
      lock_upgrade,
      false,
      unlock_shared);
  // this is the reader-block-writer-block-reader case, since the mutex
  // prioritizes the writer/upgrader
  cases.emplace_back(
      "shared_locked-lock-waiter-lock_shared",
      shared_locked_only,
      lock,
      lock_shared,
      false,
      unlock_shared);

  // upgrade locked and no one waiting
  cases.emplace_back(
      "upgrade_locked-no-waiter-lock",
      upgrade_locked_only,
      std::nullopt,
      lock,
      false,
      unlock_upgrade);
  cases.emplace_back(
      "upgrade_locked-no-waiter-lock_upgrade",
      upgrade_locked_only,
      std::nullopt,
      lock_upgrade,
      false,
      unlock_upgrade);
  cases.emplace_back(
      "upgrade_locked-no-waiter-lock_shared",
      upgrade_locked_only,
      std::nullopt,
      lock_shared,
      true,
      unlock_upgrade);
  cases.emplace_back(
      "upgrade_locked-no-waiter-unlock_upgrade_and_lock",
      upgrade_locked_only,
      std::nullopt,
      unlock_upgrade_and_lock,
      true,
      std::nullopt);

  // upgrade locked with waiters
  cases.emplace_back(
      "upgrade_locked-lock-waiter-lock",
      upgrade_locked_only,
      lock,
      lock,
      false,
      unlock_upgrade);
  cases.emplace_back(
      "upgrade_locked-lock-waiter-lock_upgrade",
      upgrade_locked_only,
      lock,
      lock_upgrade,
      false,
      unlock_upgrade);
  // since the mutex prioritizes the writer, lock_shared() would be blocked by
  // the lock() even when the mutex is only upgrade locked
  cases.emplace_back(
      "upgrade_locked-lock-waiter-lock_shared",
      upgrade_locked_only,
      lock,
      lock_shared,
      false,
      unlock_upgrade);
  // this is the case where lock transfer skips the waiter line to avoid
  // deadlock
  cases.emplace_back(
      "upgrade_locked-lock-waiter-unlock_upgrade_and_lock",
      upgrade_locked_only,
      lock,
      unlock_upgrade_and_lock,
      true,
      std::nullopt);

  cases.emplace_back(
      "upgrade_locked-lock_upgrade-waiter-lock",
      upgrade_locked_only,
      lock_upgrade,
      lock,
      false,
      unlock_upgrade);
  // the mutex prioritizes writers but read locks can still be granted as long
  // as there is no writers waiting
  // granting the read lock here will not starve the waiting upgrader, as they
  // are not contending anyway
  cases.emplace_back(
      "upgrade_locked-lock_upgrade-waiter-lock_shared",
      upgrade_locked_only,
      lock_upgrade,
      lock_shared,
      true,
      unlock_upgrade);
  cases.emplace_back(
      "upgrade_locked-lock_upgrade-waiter-lock_upgrade",
      upgrade_locked_only,
      lock_upgrade,
      lock_upgrade,
      false,
      unlock_upgrade);
  // this is the case where lock transfer skips the waiter line to avoid
  // deadlock
  cases.emplace_back(
      "upgrade_locked-lock_upgrade-waiter-unlock_upgrade_and_lock",
      upgrade_locked_only,
      lock_upgrade,
      unlock_upgrade_and_lock,
      true,
      std::nullopt);

  // upgrade and shared locked with no one waiting
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock-0",
      upgrade_and_shared_locked,
      std::nullopt,
      lock,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock-1",
      upgrade_and_shared_locked,
      std::nullopt,
      lock,
      false,
      unlock_shared_and_unlock_upgrade);
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock_upgrade-0",
      upgrade_and_shared_locked,
      std::nullopt,
      lock_upgrade,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock_upgrade-1",
      upgrade_and_shared_locked,
      std::nullopt,
      lock_upgrade,
      false,
      unlock_shared_and_unlock_upgrade);
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock_shared-0",
      upgrade_and_shared_locked,
      std::nullopt,
      lock_shared,
      true,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-lock_shared-1",
      upgrade_and_shared_locked,
      std::nullopt,
      lock_shared,
      true,
      unlock_shared_and_unlock_upgrade);

  // the lock transfer needs to wait for the readers to drain
  cases.emplace_back(
      "upgrade_and_shared_locked-no-waiter-unlock_upgrade_and_lock",
      upgrade_and_shared_locked,
      std::nullopt,
      unlock_upgrade_and_lock,
      false,
      unlock_shared);

  // upgrade and shared locked with waiters
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock-0",
      upgrade_and_shared_locked,
      lock,
      lock,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock-1",
      upgrade_and_shared_locked,
      lock,
      lock,
      false,
      unlock_shared_and_unlock_upgrade);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock_upgrade-0",
      upgrade_and_shared_locked,
      lock,
      lock_upgrade,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock_upgrade-1",
      upgrade_and_shared_locked,
      lock,
      lock_upgrade,
      false,
      unlock_shared_and_unlock_upgrade);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock_shared-0",
      upgrade_and_shared_locked,
      lock,
      lock_shared,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-lock_shared-1",
      upgrade_and_shared_locked,
      lock,
      lock_shared,
      false,
      unlock_shared_and_unlock_upgrade);
  // this is the case where lock transfer skips the waiter line to avoid
  // deadlock once the reader is drained, the lock transfer will succeed first
  // before the lock()
  cases.emplace_back(
      "upgrade_and_shared_locked-lock-waiter-unlock_upgrade_and_lock",
      upgrade_and_shared_locked,
      lock,
      unlock_upgrade_and_lock,
      false,
      unlock_shared);

  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock-0",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock-1",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock,
      false,
      unlock_shared_and_unlock_upgrade);
  // the mutex prioritizes writers but read locks can still be granted as long
  // as there is no writers waiting
  // granting the read lock here will not starve the waiting upgrader, as they
  // are not contending anyway
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock_shared-0",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock_shared,
      true,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock_shared-1",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock_shared,
      true,
      unlock_shared_and_unlock_upgrade);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock_upgrade-0",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock_upgrade,
      false,
      unlock_upgrade_and_unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-lock_upgrade-1",
      upgrade_and_shared_locked,
      lock_upgrade,
      lock_upgrade,
      false,
      unlock_shared_and_unlock_upgrade);
  // this is the case where lock transfer skips the waiter line to avoid
  // deadlock once the reader is drained, the lock transfer will succeed
  // first before the lock_upgrade()
  cases.emplace_back(
      "upgrade_and_shared_locked-lock_upgrade-waiter-unlock_upgrade_and_lock",
      upgrade_and_shared_locked,
      lock_upgrade,
      unlock_upgrade_and_lock,
      false,
      unlock_shared);
  // tests when there is a lock transition waiter
  cases.emplace_back(
      "upgrade_and_shared_locked-unlock_upgrade_and_lock-waiter-lock",
      upgrade_and_shared_locked,
      unlock_upgrade_and_lock,
      lock,
      false,
      unlock_shared);
  // this is the case where the mutex prioritizes draining the readers when
  // there is pending lock transfer and not granting new reader locks even when
  // it could
  cases.emplace_back(
      "upgrade_and_shared_locked-unlock_upgrade_and_lock-waiter-lock_shared",
      upgrade_and_shared_locked,
      unlock_upgrade_and_lock,
      lock_shared,
      false,
      unlock_shared);
  cases.emplace_back(
      "upgrade_and_shared_locked-unlock_upgrade_and_lock-waiter-lock_upgrade",
      upgrade_and_shared_locked,
      unlock_upgrade_and_lock,
      lock_upgrade,
      false,
      unlock_shared);

  for (auto& [testName, init, waiterSetup, action, expectDone, cleanup] :
       cases) {
    SCOPED_TRACE(testName);
    coro::SharedMutex m;
    ManualExecutor executor;
    init(m);
    std::optional<folly::SemiFuture<folly::Unit>> waiterSemi;
    if (waiterSetup) {
      waiterSemi = co_withExecutor(&executor, (*waiterSetup)(m)).start();
    }
    executor.drain();
    if (waiterSemi) {
      ASSERT_FALSE(waiterSemi->isReady()); // the waiter is supposed to wait
    }
    auto semi = co_withExecutor(&executor, action(m)).start();
    executor.drain();
    ASSERT_EQ(semi.isReady(), expectDone);
    if (expectDone) {
      ASSERT_TRUE(semi.hasValue());
    }
    if (cleanup) {
      (*cleanup)(m);
    }
    executor.drain();
    ASSERT_TRUE(semi.isReady());
    ASSERT_TRUE(semi.hasValue());
    if (waiterSemi) {
      ASSERT_TRUE(waiterSemi->isReady());
      ASSERT_TRUE(waiterSemi->hasValue());
    }
  }
}

TEST_F(SharedMutexTest, ManualLockAllStateTransitions) {
  // these are all the async operations that can happen to the mutex
  auto lock = [](coro::SharedMutex& m) -> coro::Task<void> {
    co_await m.co_lock();
    m.unlock();
  };
  auto lock_upgrade = [](coro::SharedMutex& m) -> coro::Task<void> {
    co_await m.co_lock_upgrade();
    m.unlock_upgrade();
  };
  auto lock_shared = [](coro::SharedMutex& m) -> coro::Task<void> {
    co_await m.co_lock_shared();
    m.unlock_shared();
  };
  auto unlock_upgrade_and_lock = [](coro::SharedMutex& m) -> coro::Task<void> {
    co_await m.co_unlock_upgrade_and_lock();
    m.unlock();
  };

  testAllStateTransitions(
      lock, lock_upgrade, lock_shared, unlock_upgrade_and_lock);
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

    auto r1 = co_withExecutor(&executor, makeReaderTask(b1)).start();
    auto r2 = co_withExecutor(&executor, makeReaderTask(b2)).start();
    auto w1 = co_withExecutor(&executor, makeWriterTask(b3)).start();
    auto w2 = co_withExecutor(&executor, makeWriterTask(b4)).start();
    auto r3 = co_withExecutor(&executor, makeReaderTask(b5)).start();

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

TEST_F(SharedMutexTest, MultipleWaiters) {
  coro::SharedMutex mutex;
  auto writer = [&]() -> coro::Task<void> { co_await mutex.co_lock(); };
  auto upgrader = [&]() -> coro::Task<void> {
    co_await mutex.co_lock_upgrade();
  };
  auto reader = [&]() -> coro::Task<void> { co_await mutex.co_lock_shared(); };

  {
    // test that the long reader scan does not go pass a waiting writer
    ManualExecutor executor;
    ASSERT_TRUE(mutex.try_lock());
    // U1 S1 U2 S2 W S3
    // expect U1 S1 S2 to be unblocked on unlock
    auto u1 = co_withExecutor(&executor, upgrader()).start();
    auto s1 = co_withExecutor(&executor, reader()).start();
    auto u2 = co_withExecutor(&executor, upgrader()).start();
    auto s2 = co_withExecutor(&executor, reader()).start();
    auto w = co_withExecutor(&executor, writer()).start();
    auto s3 = co_withExecutor(&executor, reader()).start();
    executor.drain();
    EXPECT_FALSE(u1.isReady());
    EXPECT_FALSE(s1.isReady());
    EXPECT_FALSE(s2.isReady());
    mutex.unlock();
    executor.drain();
    EXPECT_TRUE(u1.hasValue());
    EXPECT_TRUE(s1.hasValue());
    EXPECT_TRUE(s2.hasValue());

    EXPECT_FALSE(u2.isReady());
    EXPECT_FALSE(w.isReady());
    EXPECT_FALSE(s3.isReady());

    mutex.unlock_upgrade();
    mutex.unlock_shared();
    mutex.unlock_shared();
    executor.drain();
    EXPECT_TRUE(u2.hasValue());
    EXPECT_FALSE(w.isReady());
    EXPECT_FALSE(s3.isReady());

    mutex.unlock_upgrade();
    executor.drain();
    EXPECT_TRUE(w.hasValue());
    EXPECT_FALSE(s3.isReady());

    mutex.unlock();
    executor.drain();
    EXPECT_TRUE(s3.hasValue());
    mutex.unlock_shared();
  }

  {
    // test that resuming a tail reader via long scan is handled correct
    // this is to ensure the waiter tail pointer is updated correctly
    ManualExecutor executor;
    ASSERT_TRUE(mutex.try_lock());
    // U1 U2 S
    auto u1 = co_withExecutor(&executor, upgrader()).start();
    auto u2 = co_withExecutor(&executor, upgrader()).start();
    auto s = co_withExecutor(&executor, reader()).start();
    executor.drain();
    EXPECT_FALSE(u1.isReady());
    EXPECT_FALSE(u2.isReady());
    EXPECT_FALSE(s.isReady());
    mutex.unlock();
    executor.drain();
    EXPECT_TRUE(u1.hasValue());
    EXPECT_TRUE(s.hasValue());
    EXPECT_FALSE(u2.isReady());

    // u3 should be enqueued behind u2 which is the new tail
    auto u3 = co_withExecutor(&executor, upgrader()).start();
    executor.drain();
    EXPECT_FALSE(u3.isReady());

    mutex.unlock_upgrade();
    executor.drain();
    EXPECT_TRUE(u2.hasValue());
    EXPECT_FALSE(u3.isReady());

    mutex.unlock_upgrade();
    executor.drain();
    EXPECT_TRUE(u3.hasValue());

    mutex.unlock_upgrade();
    mutex.unlock_shared();
  }
}

TEST_F(SharedMutexTest, ScopedLockAllStateTransitions) {
  // these are all the async operations that can happen to the mutex
  auto lock = [](coro::SharedMutex& m) -> coro::Task<void> {
    auto l = co_await m.co_scoped_lock();
  };
  auto lock_upgrade = [](coro::SharedMutex& m) -> coro::Task<void> {
    auto l = co_await m.co_scoped_lock_upgrade();
  };
  auto lock_shared = [](coro::SharedMutex& m) -> coro::Task<void> {
    auto l = co_await m.co_scoped_lock_shared();
  };
  auto unlock_upgrade_and_lock = [](coro::SharedMutex& m) -> coro::Task<void> {
    auto l = co_await m.co_scoped_unlock_upgrade_and_lock();
  };

  testAllStateTransitions(
      lock, lock_upgrade, lock_shared, unlock_upgrade_and_lock);
}

TEST_F(SharedMutexTest, AsyncLockTransition) {
  coro::SharedMutex mutex;
  int value = 0;
  auto conditionalWrite = [&]() -> coro::Task<void> {
    auto uLock = co_await mutex.co_scoped_lock_upgrade();
    if (value == 0) {
      auto wLock = co_await folly::coro::co_transition_lock(uLock);
      value += 1;
    }
  };

  auto read = [&]() -> coro::Task<int> {
    auto rLock = co_await mutex.co_scoped_lock_shared();
    int copyValue = value;
    co_return copyValue;
  };

  {
    ManualExecutor executor;
    value = 0;
    auto w = co_withExecutor(&executor, conditionalWrite()).start();
    auto r = co_withExecutor(&executor, read()).start();
    executor.drain();
    EXPECT_EQ(std::move(r).get(), 1);
  }

  {
    ManualExecutor executor;
    value = 0;
    auto r1 = co_withExecutor(&executor, read()).start();
    auto w = co_withExecutor(&executor, conditionalWrite()).start();
    auto r2 = co_withExecutor(&executor, read()).start();
    executor.drain();
    EXPECT_EQ(std::move(r1).get(), 0);
    EXPECT_EQ(std::move(r2).get(), 1);
  }

  {
    ManualExecutor executor;
    value = 0;
    auto r1 = co_withExecutor(&executor, read()).start();
    auto w1 = co_withExecutor(&executor, conditionalWrite()).start();
    auto w2 = co_withExecutor(&executor, conditionalWrite()).start();
    auto r2 = co_withExecutor(&executor, read()).start();
    executor.drain();
    EXPECT_EQ(std::move(r1).get(), 0);
    EXPECT_EQ(std::move(r2).get(), 1);
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

  auto w1 = co_withExecutor(&threadPool, makeWriterTask()).start();
  auto w2 = co_withExecutor(&threadPool, makeWriterTask()).start();
  auto r1 = co_withExecutor(&threadPool, makeReaderTask()).start();
  auto r2 = co_withExecutor(&threadPool, makeReaderTask()).start();
  auto r3 = co_withExecutor(&threadPool, makeReaderTask()).start();
  auto r4 = co_withExecutor(&threadPool, makeReaderTask()).start();

  std::move(w1).get();
  std::move(w2).get();
  std::move(r1).get();
  std::move(r2).get();
  std::move(r3).get();
  std::move(r4).get();

  CHECK_EQ(value1, 2 * iterationCount);
  CHECK_EQ(value2, 2 * iterationCount);
}

TEST_F(SharedMutexTest, StressTest) {
  // create a large number of coroutines running on an executor with
  // 10 threads, constantly trying to read and write shared state

  coro::SharedMutex mutex;
  int value1 = 0;
  int value2 = 0;
  std::atomic<bool> reachedTarget{false};
  std::atomic<size_t> earlyExists{0};
  constexpr int target = 100'000;

  auto incrementIfEven = [&]() -> coro::Task<void> {
    {
      auto rLock = co_await mutex.co_scoped_lock_shared();
      if (value1 % 2 != 0) {
        ++earlyExists;
        co_return;
      }
    }
    auto uLock = co_await mutex.co_scoped_lock_upgrade();
    if (value1 % 2 != 0) {
      ++earlyExists;
      co_return;
    }
    auto wLock = co_await folly::coro::co_transition_lock(uLock);
    ++value1;
    ++value2;
  };
  auto incrementIfOdd = [&]() -> coro::Task<void> {
    {
      auto rLock = co_await mutex.co_scoped_lock_shared();
      if (value1 % 2 == 0) {
        ++earlyExists;
        co_return;
      }
    }
    auto uLock = co_await mutex.co_scoped_lock_upgrade();
    if (value1 % 2 == 0) {
      ++earlyExists;
      co_return;
    }
    auto wLock = co_await folly::coro::co_transition_lock(uLock);
    ++value1;
    ++value2;
  };
  auto read = [&]() -> coro::Task<int> {
    auto rLock = co_await mutex.co_scoped_lock_shared();
    EXPECT_EQ(value1, value1);
    co_return value1;
  };
  auto check = [&]() -> coro::Task<void> {
    auto rLock = co_await mutex.co_scoped_lock_shared();
    if (value1 >= target) {
      reachedTarget = true;
    }
    EXPECT_EQ(value1, value1);
  };

  CPUThreadPoolExecutor executor{
      10, std::make_shared<NamedThreadFactory>("TestThreadPool")};

  size_t writeTaskCnt = 0;
  folly::coro::AsyncScope scope;
  while (!reachedTarget.load()) {
    writeTaskCnt += 2;
    scope.add(co_withExecutor(&executor, check()));
    scope.add(co_withExecutor(&executor, incrementIfOdd()));
    scope.add(co_withExecutor(&executor, check()));
    scope.add(co_withExecutor(&executor, incrementIfEven()));
    scope.add(co_withExecutor(&executor, check()));
  }
  folly::coro::blockingWait(co_withExecutor(&executor, scope.joinAsync()));

  // final read
  int finalValue =
      folly::coro::blockingWait(co_withExecutor(&executor, read()));

  EXPECT_GE(finalValue, target);
  EXPECT_EQ(writeTaskCnt, finalValue + earlyExists);
}

#endif
