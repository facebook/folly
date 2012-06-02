/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/Synchronized.h"
#include "folly/RWSpinLock.h"
#include "folly/test/SynchronizedTestLib.h"
#include <gtest/gtest.h>


TEST(Synchronized, Basic) {
  testBasic<std::mutex>();
  testBasic<std::recursive_mutex>();
  testBasic<std::timed_mutex>();
  testBasic<std::recursive_timed_mutex>();

  testBasic<folly::RWTicketSpinLock32>();

  testBasic<boost::mutex>();
  testBasic<boost::recursive_mutex>();
  testBasic<boost::shared_mutex>();
  testBasic<boost::timed_mutex>();
  testBasic<boost::recursive_timed_mutex>();
}

TEST(Synchronized, Concurrency) {
  testConcurrency<std::mutex>();
  testConcurrency<std::recursive_mutex>();
  testConcurrency<std::timed_mutex>();
  testConcurrency<std::recursive_timed_mutex>();

  testConcurrency<folly::RWTicketSpinLock32>();

  testConcurrency<boost::mutex>();
  testConcurrency<boost::recursive_mutex>();
  testConcurrency<boost::shared_mutex>();
  testConcurrency<boost::timed_mutex>();
  testConcurrency<boost::recursive_timed_mutex>();
}


TEST(Synchronized, DualLocking) {
  testDualLocking<std::mutex>();
  testDualLocking<std::recursive_mutex>();
  testDualLocking<std::timed_mutex>();
  testDualLocking<std::recursive_timed_mutex>();

  testDualLocking<folly::RWTicketSpinLock32>();

  testDualLocking<boost::mutex>();
  testDualLocking<boost::recursive_mutex>();
  testDualLocking<boost::shared_mutex>();
  testDualLocking<boost::timed_mutex>();
  testDualLocking<boost::recursive_timed_mutex>();
}


TEST(Synchronized, DualLockingWithConst) {
  testDualLockingWithConst<std::mutex>();
  testDualLockingWithConst<std::recursive_mutex>();
  testDualLockingWithConst<std::timed_mutex>();
  testDualLockingWithConst<std::recursive_timed_mutex>();

  testDualLockingWithConst<folly::RWTicketSpinLock32>();

  testDualLockingWithConst<boost::mutex>();
  testDualLockingWithConst<boost::recursive_mutex>();
  testDualLockingWithConst<boost::shared_mutex>();
  testDualLockingWithConst<boost::timed_mutex>();
  testDualLockingWithConst<boost::recursive_timed_mutex>();
}


TEST(Synchronized, TimedSynchronized) {
  testTimedSynchronized<std::timed_mutex>();
  testTimedSynchronized<std::recursive_timed_mutex>();

  testTimedSynchronized<boost::timed_mutex>();
  testTimedSynchronized<boost::recursive_timed_mutex>();
  testTimedSynchronized<boost::shared_mutex>();
}

TEST(Synchronized, ConstCopy) {
  testConstCopy<std::timed_mutex>();
  testConstCopy<std::recursive_timed_mutex>();

  testConstCopy<boost::timed_mutex>();
  testConstCopy<boost::recursive_timed_mutex>();
  testConstCopy<boost::shared_mutex>();
}
