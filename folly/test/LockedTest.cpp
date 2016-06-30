/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Test bed for folly/Locked.h

#include <folly/Locked.h>

#include <boost/thread.hpp>
#include <folly/Portability.h>
#include <folly/RWSpinLock.h>
#include <folly/SharedMutex.h>
#include <folly/SpinLock.h>
#include <folly/test/LockedTestLib.h>
#include <gtest/gtest.h>
#include <mutex>
#include <shared_mutex>

using namespace folly::locked_tests;

namespace {

template <class Mutex>
class LockedTest : public testing::Test {};

using LockedTestTypes = testing::Types<
    folly::SharedMutexReadPriority,
    folly::SharedMutexWritePriority,
#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
    std::timed_mutex,
    std::shared_timed_mutex,
    std::recursive_timed_mutex,
#endif
    boost::mutex,
    boost::recursive_mutex,
#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
    boost::timed_mutex,
    boost::recursive_timed_mutex,
#endif
    boost::shared_mutex,
    folly::SpinLock,
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
    folly::RWTicketSpinLock32,
    folly::RWTicketSpinLock64,
#endif
    std::mutex,
    std::recursive_mutex>;
TYPED_TEST_CASE(LockedTest, LockedTestTypes);

TYPED_TEST(LockedTest, Basic) {
  testBasic<TypeParam>();
}

TYPED_TEST(LockedTest, Concurrency) {
  testConcurrency<TypeParam>();
}

TYPED_TEST(LockedTest, DualLocking) {
  testDualLocking<TypeParam>();
}

TYPED_TEST(LockedTest, DualLockingShared) {
  testDualLockingShared<TypeParam>();
}

TYPED_TEST(LockedTest, ConstCopy) {
  testConstCopy<TypeParam>();
}

template <class Mutex>
class LockedTimedTest : public testing::Test {};

using LockedTimedTestTypes = testing::Types<
#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
    std::timed_mutex,
    std::shared_timed_mutex,
    std::recursive_timed_mutex,
    boost::timed_mutex,
    boost::recursive_timed_mutex,
    boost::shared_mutex,
#endif
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
    folly::RWTicketSpinLock32,
    folly::RWTicketSpinLock64,
#endif
    folly::SharedMutexReadPriority,
    folly::SharedMutexWritePriority>;
TYPED_TEST_CASE(LockedTimedTest, LockedTimedTestTypes);

TYPED_TEST(LockedTimedTest, Timed) {
  testTimed<TypeParam>();
}

template <class Mutex>
class LockedTimedRWTest : public testing::Test {};

using LockedTimedRWTestTypes = testing::Types<
#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
    boost::shared_mutex,
    std::shared_timed_mutex,
#endif
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
    folly::RWTicketSpinLock32,
    folly::RWTicketSpinLock64,
#endif
    folly::SharedMutexReadPriority,
    folly::SharedMutexWritePriority>;
TYPED_TEST_CASE(LockedTimedRWTest, LockedTimedRWTestTypes);

TYPED_TEST(LockedTimedRWTest, TimedSynchronizeRW) {
  testTimedSynchronizedRW<TypeParam>();
}

TYPED_TEST(LockedTest, InPlaceConstruction) {
  testInPlaceConstruction<TypeParam>();
}

using CountPair = std::pair<int, int>;
// This class is specialized only to be uesed in LockedLockTest
class FakeMutex {
 public:
  bool lock() {
    ++lockCount_;
    return true;
  }

  bool unlock() {
    ++unlockCount_;
    return true;
  }

  static CountPair getLockUnlockCount() {
    return CountPair{lockCount_, unlockCount_};
  }

  static void resetLockUnlockCount() {
    lockCount_ = 0;
    unlockCount_ = 0;
  }
 private:
  // Keep these two static for test access
  // Keep them thread_local in case of tests are run in parallel within one
  // process
  static FOLLY_TLS int lockCount_;
  static FOLLY_TLS int unlockCount_;
};
FOLLY_TLS int FakeMutex::lockCount_{0};
FOLLY_TLS int FakeMutex::unlockCount_{0};

// LockedLockTest is used to verify the correct lock unlock behavior
// happens per design
class LockedLockTest : public testing::Test {
 public:
  void SetUp() override {
    FakeMutex::resetLockUnlockCount();
  }
};

// Single level of lock() and scopedUnlock(), although nested test are
// super set of it, it is possible single level test passes while nested tests
// fail
TEST_F(LockedLockTest, LockUnlock) {
  folly::Locked<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  {
    auto lptr = obj.lock();
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    {
      auto unlocker = lptr.scopedUnlock();
      EXPECT_EQ((CountPair{1, 1}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
  }
  EXPECT_EQ((CountPair{2, 2}), FakeMutex::getLockUnlockCount());
}

// Nested lock() and scopedUnlock() test, 2 levels for each are used here
TEST_F(LockedLockTest, NestedSyncUnSync) {
  folly::Locked<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  {
    auto lptr1 = obj.lock();
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    {
      auto lptr2 = obj.lock();
      EXPECT_EQ((CountPair{2, 0}), FakeMutex::getLockUnlockCount());
      {
        auto unlocker2 = lptr2.scopedUnlock();
        EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
        {
          auto unlocker1 = lptr1.scopedUnlock();
          EXPECT_EQ((CountPair{2, 2}),
                    FakeMutex::getLockUnlockCount());
        }
        EXPECT_EQ((CountPair{3, 2}), FakeMutex::getLockUnlockCount());
      }
      EXPECT_EQ((CountPair{4, 2}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{4, 3}), FakeMutex::getLockUnlockCount());
  }
  EXPECT_EQ((CountPair{4, 4}), FakeMutex::getLockUnlockCount());
}

// Different nesting behavior, scopedUnlock() called on different depth from
// the corresponding lock()
TEST_F(LockedLockTest, NestedSyncUnSync2) {
  folly::Locked<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  {
    auto lptr1 = obj.lock();
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    {
      auto lptr2 = obj.lock();
      EXPECT_EQ((CountPair{2, 0}), FakeMutex::getLockUnlockCount());
      {
        auto unlocker = lptr2.scopedUnlock();
        EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
      }
      EXPECT_EQ((CountPair{3, 1}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{3, 2}), FakeMutex::getLockUnlockCount());
    {
      auto unlocker = lptr1.scopedUnlock();
      EXPECT_EQ((CountPair{3, 3}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{4, 3}), FakeMutex::getLockUnlockCount());
  }
  EXPECT_EQ((CountPair{4, 4}), FakeMutex::getLockUnlockCount());
}
}
