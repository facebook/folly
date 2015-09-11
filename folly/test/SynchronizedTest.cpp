/*
 * Copyright 2015 Facebook, Inc.
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
#ifdef FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
  , std::timed_mutex
  , std::recursive_timed_mutex
#endif
  , boost::mutex
  , boost::recursive_mutex
#ifdef FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
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
#ifdef FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
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
#ifdef FOLLY_SYNCHRONIZED_HAVE_TIMED_MUTEXES
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

}
