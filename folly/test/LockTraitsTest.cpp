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
#include <folly/LockTraits.h>
#include <folly/LockTraitsBoost.h>

#include <gtest/gtest.h>
#include <mutex>

#include <folly/RWSpinLock.h>
#include <folly/SharedMutex.h>
#include <folly/SpinLock.h>

using namespace folly;

TEST(LockTraits, std_mutex) {
  static_assert(
      !LockTraits<std::mutex>::has_timed_lock,
      "std:mutex does not have timed acquire");
  static_assert(
      !LockTraits<std::mutex>::is_shared, "std:mutex is not a read-write lock");

  std::mutex mutex;
  LockTraits<std::mutex>::lock(mutex);
  LockTraits<std::mutex>::unlock(mutex);
}

TEST(LockTraits, SharedMutex) {
  static_assert(
      LockTraits<SharedMutex>::has_timed_lock, "SharedMutex has timed acquire");
  static_assert(
      LockTraits<SharedMutex>::is_shared, "SharedMutex is a read-write lock");

  SharedMutex mutex;
  LockTraits<SharedMutex>::lock(mutex);
  LockTraits<SharedMutex>::unlock(mutex);

  LockTraits<SharedMutex>::lock_shared(mutex);
  LockTraits<SharedMutex>::lock_shared(mutex);
  LockTraits<SharedMutex>::unlock_shared(mutex);
  LockTraits<SharedMutex>::unlock_shared(mutex);
}

TEST(LockTraits, SpinLock) {
  static_assert(
      !LockTraits<SpinLock>::has_timed_lock,
      "folly::SpinLock does not have timed acquire");
  static_assert(
      !LockTraits<SpinLock>::is_shared,
      "folly::SpinLock is not a read-write lock");

  SpinLock mutex;
  LockTraits<SpinLock>::lock(mutex);
  LockTraits<SpinLock>::unlock(mutex);
}

TEST(LockTraits, RWSpinLock) {
  static_assert(
      !LockTraits<RWSpinLock>::has_timed_lock,
      "folly::RWSpinLock does not have timed acquire");
  static_assert(
      LockTraits<RWSpinLock>::is_shared,
      "folly::RWSpinLock is a read-write lock");

  RWSpinLock mutex;
  LockTraits<RWSpinLock>::lock(mutex);
  LockTraits<RWSpinLock>::unlock(mutex);

  LockTraits<RWSpinLock>::lock_shared(mutex);
  LockTraits<RWSpinLock>::lock_shared(mutex);
  LockTraits<RWSpinLock>::unlock_shared(mutex);
  LockTraits<RWSpinLock>::unlock_shared(mutex);
}

TEST(LockTraits, boost_mutex) {
  static_assert(
      !LockTraits<boost::mutex>::has_timed_lock,
      "boost::mutex does not have timed acquire");
  static_assert(
      !LockTraits<boost::mutex>::is_shared,
      "boost::mutex is not a read-write lock");

  boost::mutex mutex;
  LockTraits<boost::mutex>::lock(mutex);
  LockTraits<boost::mutex>::unlock(mutex);
}

TEST(LockTraits, boost_recursive_mutex) {
  static_assert(
      !LockTraits<boost::mutex>::has_timed_lock,
      "boost::recursive_mutex does not have timed acquire");
  static_assert(
      !LockTraits<boost::recursive_mutex>::is_shared,
      "boost::recursive_mutex is not a read-write lock");

  boost::recursive_mutex mutex;
  LockTraits<boost::recursive_mutex>::lock(mutex);
  LockTraits<boost::recursive_mutex>::unlock(mutex);
}

#if FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
TEST(LockTraits, timed_mutex) {
  static_assert(
      LockTraits<std::timed_mutex>::has_timed_lock,
      "std::timed_mutex supports timed acquire");
  static_assert(
      !LockTraits<std::timed_mutex>::is_shared,
      "std::timed_mutex is not a read-write lock");

  std::timed_mutex mutex;
  LockTraits<std::timed_mutex>::lock(mutex);
  bool gotLock = LockTraits<std::timed_mutex>::try_lock_for(
      mutex, std::chrono::milliseconds(1));
  EXPECT_FALSE(gotLock) << "should not have been able to acquire the "
                        << "timed_mutex a second time";
  LockTraits<std::timed_mutex>::unlock(mutex);
}

TEST(LockTraits, recursive_timed_mutex) {
  static_assert(
      LockTraits<std::recursive_timed_mutex>::has_timed_lock,
      "std::recursive_timed_mutex supports timed acquire");
  static_assert(
      !LockTraits<std::recursive_timed_mutex>::is_shared,
      "std::recursive_timed_mutex is not a read-write lock");

  std::recursive_timed_mutex mutex;
  LockTraits<std::recursive_timed_mutex>::lock(mutex);
  auto gotLock = LockTraits<std::recursive_timed_mutex>::try_lock_for(
      mutex, std::chrono::milliseconds(10));
  EXPECT_TRUE(gotLock) << "should have been able to acquire the "
                       << "recursive_timed_mutex a second time";
  LockTraits<std::recursive_timed_mutex>::unlock(mutex);
  LockTraits<std::recursive_timed_mutex>::unlock(mutex);
}

TEST(LockTraits, boost_shared_mutex) {
  static_assert(
      LockTraits<boost::shared_mutex>::has_timed_lock,
      "boost::shared_mutex supports timed acquire");
  static_assert(
      LockTraits<boost::shared_mutex>::is_shared,
      "boost::shared_mutex is a read-write lock");

  boost::shared_mutex mutex;
  LockTraits<boost::shared_mutex>::lock(mutex);
  auto gotLock = LockTraits<boost::shared_mutex>::try_lock_for(
      mutex, std::chrono::milliseconds(1));
  EXPECT_FALSE(gotLock) << "should not have been able to acquire the "
                        << "shared_mutex a second time";
  gotLock = LockTraits<boost::shared_mutex>::try_lock_shared_for(
      mutex, std::chrono::milliseconds(1));
  EXPECT_FALSE(gotLock) << "should not have been able to acquire the "
                        << "shared_mutex a second time";
  LockTraits<boost::shared_mutex>::unlock(mutex);

  LockTraits<boost::shared_mutex>::lock_shared(mutex);
  gotLock = LockTraits<boost::shared_mutex>::try_lock_for(
      mutex, std::chrono::milliseconds(1));
  EXPECT_FALSE(gotLock) << "should not have been able to acquire the "
                        << "shared_mutex a second time";
  gotLock = LockTraits<boost::shared_mutex>::try_lock_shared_for(
      mutex, std::chrono::milliseconds(10));
  EXPECT_TRUE(gotLock) << "should have been able to acquire the "
                       << "shared_mutex a second time in shared mode";
  LockTraits<boost::shared_mutex>::unlock_shared(mutex);
  LockTraits<boost::shared_mutex>::unlock_shared(mutex);
}
#endif // FOLLY_LOCK_TRAITS_HAVE_TIMED_MUTEXES
