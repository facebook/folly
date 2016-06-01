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

// @author: Andrei Alexandrescu (aalexandre)

// Test bed for folly/Synchronized.h

#include <folly/Portability.h>
#include <folly/Synchronized.h>
#include <folly/RWSpinLock.h>
#include <folly/SharedMutex.h>
#include <folly/test/SynchronizedTestLib.h>
#include <gtest/gtest.h>

namespace {

template <class Mutex>
class SynchronizedTest : public testing::Test {};

using SynchronizedTestTypes = testing::Types
  < folly::SharedMutexReadPriority
  , folly::SharedMutexWritePriority
  , std::mutex
  , std::recursive_mutex
#if FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
  , std::timed_mutex
  , std::recursive_timed_mutex
#endif
  , boost::mutex
  , boost::recursive_mutex
#if FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
  , boost::timed_mutex
  , boost::recursive_timed_mutex
#endif
  , boost::shared_mutex
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
  , folly::RWTicketSpinLock32
  , folly::RWTicketSpinLock64
#endif
  >;
TYPED_TEST_CASE(SynchronizedTest, SynchronizedTestTypes);

TYPED_TEST(SynchronizedTest, Basic) {
  testBasic<TypeParam>();
}

TYPED_TEST(SynchronizedTest, Concurrency) {
  testConcurrency<TypeParam>();
}

TYPED_TEST(SynchronizedTest, DualLocking) {
  testDualLocking<TypeParam>();
}

TYPED_TEST(SynchronizedTest, DualLockingWithConst) {
  testDualLockingWithConst<TypeParam>();
}

TYPED_TEST(SynchronizedTest, ConstCopy) {
  testConstCopy<TypeParam>();
}

template <class Mutex>
class SynchronizedTimedTest : public testing::Test {};

using SynchronizedTimedTestTypes = testing::Types
  < folly::SharedMutexReadPriority
  , folly::SharedMutexWritePriority
#if FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
  , std::timed_mutex
  , std::recursive_timed_mutex
  , boost::timed_mutex
  , boost::recursive_timed_mutex
  , boost::shared_mutex
#endif
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
  , folly::RWTicketSpinLock32
  , folly::RWTicketSpinLock64
#endif
  >;
TYPED_TEST_CASE(SynchronizedTimedTest, SynchronizedTimedTestTypes);

TYPED_TEST(SynchronizedTimedTest, TimedSynchronized) {
  testTimedSynchronized<TypeParam>();
}

template <class Mutex>
class SynchronizedTimedWithConstTest : public testing::Test {};

using SynchronizedTimedWithConstTestTypes = testing::Types
  < folly::SharedMutexReadPriority
  , folly::SharedMutexWritePriority
#if FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
  , boost::shared_mutex
#endif
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
  , folly::RWTicketSpinLock32
  , folly::RWTicketSpinLock64
#endif
  >;
TYPED_TEST_CASE(
    SynchronizedTimedWithConstTest, SynchronizedTimedWithConstTestTypes);

TYPED_TEST(SynchronizedTimedWithConstTest, TimedSynchronizeWithConst) {
  testTimedSynchronizedWithConst<TypeParam>();
}

TYPED_TEST(SynchronizedTest, InPlaceConstruction) {
  testInPlaceConstruction<TypeParam>();
}

using CountPair = std::pair<int, int>;
// This class is specialized only to be uesed in SynchronizedLockTest
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

  // Adapters for Synchronized<>
  friend void acquireReadWrite(FakeMutex& lock) { lock.lock(); }
  friend void releaseReadWrite(FakeMutex& lock) { lock.unlock(); }
};
FOLLY_TLS int FakeMutex::lockCount_{0};
FOLLY_TLS int FakeMutex::unlockCount_{0};

// SynchronizedLockTest is used to verify the correct lock unlock behavior
// happens per design
class SynchronizedLockTest : public testing::Test {
 public:
  void SetUp() override {
    FakeMutex::resetLockUnlockCount();
  }
};

// Single level of SYNCHRONIZED and UNSYNCHRONIZED, although nested test are
// super set of it, it is possible single level test passes while nested tests
// fail
TEST_F(SynchronizedLockTest, SyncUnSync) {
  folly::Synchronized<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  SYNCHRONIZED(obj) {
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    UNSYNCHRONIZED(obj) {
      EXPECT_EQ((CountPair{1, 1}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
  }
  EXPECT_EQ((CountPair{2, 2}), FakeMutex::getLockUnlockCount());
}

// Nested SYNCHRONIZED UNSYNCHRONIZED test, 2 levels for each are used here
TEST_F(SynchronizedLockTest, NestedSyncUnSync) {
  folly::Synchronized<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  SYNCHRONIZED(objCopy, obj) {
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    SYNCHRONIZED(obj) {
      EXPECT_EQ((CountPair{2, 0}), FakeMutex::getLockUnlockCount());
      UNSYNCHRONIZED(obj) {
        EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
        UNSYNCHRONIZED(obj) {
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

// Different nesting behavior, UNSYNCHRONIZED called on differen depth of
// SYNCHRONIZED
TEST_F(SynchronizedLockTest, NestedSyncUnSync2) {
  folly::Synchronized<std::vector<int>, FakeMutex> obj;
  EXPECT_EQ((CountPair{0, 0}), FakeMutex::getLockUnlockCount());
  SYNCHRONIZED(objCopy, obj) {
    EXPECT_EQ((CountPair{1, 0}), FakeMutex::getLockUnlockCount());
    SYNCHRONIZED(obj) {
      EXPECT_EQ((CountPair{2, 0}), FakeMutex::getLockUnlockCount());
      UNSYNCHRONIZED(obj) {
        EXPECT_EQ((CountPair{2, 1}), FakeMutex::getLockUnlockCount());
      }
      EXPECT_EQ((CountPair{3, 1}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{3, 2}), FakeMutex::getLockUnlockCount());
    UNSYNCHRONIZED(obj) {
      EXPECT_EQ((CountPair{3, 3}), FakeMutex::getLockUnlockCount());
    }
    EXPECT_EQ((CountPair{4, 3}), FakeMutex::getLockUnlockCount());
  }
  EXPECT_EQ((CountPair{4, 4}), FakeMutex::getLockUnlockCount());
}
}
