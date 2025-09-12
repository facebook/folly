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

#include <folly/coro/Synchronized.h>

#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/portability/GTest.h>

using folly::coro::Baton;
using folly::coro::Synchronized;
using folly::coro::Task;

namespace {

// Silences clang-tidy's warning about use-after-move
template <typename T>
T& assumeInitialized(T& value) {
  return value;
}

} // namespace

class SynchronizedTest : public testing::Test {};

TEST_F(SynchronizedTest, MoveLockingPtr) {
  Synchronized<int64_t> counter{5};

  auto readPtr = counter.tryRLock();
  ASSERT_TRUE(readPtr);
  EXPECT_EQ(*readPtr, 5);

  auto readPtr2 = std::move(readPtr);
  assumeInitialized(readPtr);
  EXPECT_FALSE(readPtr);
  EXPECT_DEATH({ (void)*readPtr; }, "");
  ASSERT_TRUE(readPtr2);
  EXPECT_EQ(*readPtr2, 5);

  auto readPtr3 = counter.tryRLock();
  EXPECT_TRUE(readPtr3);
  readPtr3 = std::move(readPtr2);
  assumeInitialized(readPtr2);
  EXPECT_FALSE(readPtr2);
  EXPECT_DEATH({ (void)*readPtr2; }, "");
  ASSERT_TRUE(readPtr3);
  EXPECT_EQ(*readPtr3, 5);
}

TEST_F(SynchronizedTest, ConcurrentReads) {
  Synchronized<int64_t> counter;

  folly::ManualExecutor executor;
  Baton b;

  static auto kNumReaders = 10;
  int64_t activeReaders = 0;
  for (auto i = 0; i < kNumReaders; i++) {
    auto readTask = counter.withRLock(
        [&b, &activeReaders](auto /* unused */) mutable -> Task<folly::Unit> {
          activeReaders++;
          co_await b;
          activeReaders--;
          co_return folly::unit;
        });
    co_withExecutor(&executor, std::move(readTask)).start();
  }
  executor.drain();
  EXPECT_EQ(activeReaders, kNumReaders);

  bool writeComplete = false;
  auto writeTask = counter.withWLock(
      [&writeComplete,
       &activeReaders](auto /* unused */) mutable -> Task<folly::Unit> {
        EXPECT_EQ(activeReaders, 0);
        writeComplete = true;
        co_return folly::unit;
      });
  co_withExecutor(&executor, std::move(writeTask)).start();

  // Readers have the lock. Writing doesn't run.
  executor.drain();
  EXPECT_EQ(activeReaders, kNumReaders);

  // Post the baton. Writing will succeed.
  b.post();
  executor.drain();
  EXPECT_TRUE(writeComplete);
}

TEST_F(SynchronizedTest, ConcurrentReadsWithUnlock) {
  Synchronized<int64_t> counter;

  folly::ManualExecutor executor;
  Baton b1, b2;

  static auto kNumReaders = 10;
  int64_t activeReaders = 0;
  for (auto i = 0; i < kNumReaders; i++) {
    auto readTask = counter.withRLock(
        [&b1, &b2, &activeReaders](
            auto counterReadPtr) mutable -> folly::coro::Task<folly::Unit> {
          activeReaders++;
          co_await b1;

          counterReadPtr.unlock();
          co_await b2;

          activeReaders--;
          co_return folly::unit;
        });
    co_withExecutor(&executor, std::move(readTask)).start();
  }
  executor.drain();
  EXPECT_EQ(activeReaders, kNumReaders);

  bool writeComplete = false;
  auto writeTask = counter.withWLock(
      [&writeComplete](
          auto /* unused */) mutable -> folly::coro::Task<folly::Unit> {
        writeComplete = true;
        co_return folly::unit;
      });
  co_withExecutor(&executor, std::move(writeTask)).start();

  // Readers have the lock. Writing doesn't run.
  executor.drain();
  EXPECT_EQ(activeReaders, kNumReaders);

  // Post the first baton. Lock released, writing will succeed,
  // activeReaders won't change.
  b1.post();
  executor.drain();
  EXPECT_TRUE(writeComplete);
  EXPECT_EQ(activeReaders, kNumReaders);

  // Post the second baton. Lock already released, activeReaders will change.
  b2.post();
  executor.drain();
  EXPECT_EQ(activeReaders, 0);
}

TEST_F(SynchronizedTest, ThreadSafety) {
  folly::CPUThreadPoolExecutor threadPool{
      2, std::make_shared<folly::NamedThreadFactory>("CPUThreadPool")};

  static auto kBumpCount = 10000;
  Synchronized<int64_t> counter = {};

  auto makeTask = [&]() -> Task<void> {
    for (int i = 0; i < kBumpCount; ++i) {
      co_await counter.withWLock([](auto cnt) -> void { (*cnt)++; });
    }
  };

  auto f1 = co_withExecutor(&threadPool, makeTask()).start();
  auto f2 = co_withExecutor(&threadPool, makeTask()).start();
  auto f3 = co_withExecutor(&threadPool, makeTask()).start();

  std::move(f1).get();
  std::move(f2).get();
  std::move(f3).get();

  auto finalValue = folly::coro::blockingWait(counter.copy());
  EXPECT_EQ(3 * kBumpCount, finalValue);
}

CO_TEST_F(SynchronizedTest, SwapAndCopy) {
  Synchronized<std::vector<int64_t>> protectedList({1});
  std::vector<int64_t> newValues = {2};

  co_await protectedList.swap(newValues);
  auto updatedValues = co_await protectedList.copy();

  EXPECT_EQ(newValues[0], 1);
  EXPECT_EQ(updatedValues[0], 2);
}

TEST_F(SynchronizedTest, TryLock) {
  Synchronized<int64_t> sync(1);

  {
    const Synchronized<int64_t>& constSynch = sync;
    auto lock = constSynch.tryRLock();
    EXPECT_TRUE(lock);
  }

  {
    auto lock = sync.tryRLock();
    EXPECT_TRUE(lock);

    // Multiple read locks can be acquired
    EXPECT_TRUE(sync.tryRLock());

    // Exclusive lock cannot be acquired
    auto writeLock = sync.tryWLock();
    EXPECT_FALSE(writeLock);
    EXPECT_DEATH({ (void)*writeLock; }, "");

    lock.unlock();

    // After unlock another lock can be acquired
    EXPECT_TRUE(sync.tryRLock());
  }

  {
    auto lock = sync.tryWLock();
    EXPECT_TRUE(lock);

    // Another exclusive lock cannot be acquired
    auto writeLock = sync.tryWLock();
    EXPECT_FALSE(writeLock);
    EXPECT_DEATH({ (void)*writeLock; }, "");

    // A read lock cannot be acquired
    auto readLock = sync.tryRLock();
    EXPECT_FALSE(readLock);
    EXPECT_DEATH({ (void)*readLock; }, "");

    lock.unlock();

    // After unlock another lock can be acquired
    EXPECT_TRUE(sync.tryRLock());
  }
}
