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

#include <folly/synchronization/Utility.h>
#include <folly/Utility.h>
#include <folly/portability/GTest.h>

#include <type_traits>

class UtilityTest : public testing::Test {};

namespace {
class MockMutex {
 public:
  void lock() {
    EXPECT_FALSE(locked_);
    locked_ = true;
  }
  void unlock() {
    EXPECT_TRUE(locked_);
    locked_ = false;
  }
  bool try_lock() {
    if (!locked_) {
      locked_ = true;
      return true;
    }

    return false;
  }
  template <typename Duration>
  bool try_lock_for(const Duration&) {
    return try_lock();
  }
  template <typename TimePoint>
  bool try_lock_until(const TimePoint&) {
    return try_lock();
  }

  void lock_shared() {
    lock();
  }
  void unlock_shared() {
    unlock();
  }

  bool locked_{false};
};
} // namespace

TEST_F(UtilityTest, TestMakeUniqueLock) {
  auto&& mutex = MockMutex{};

  {
    auto lck = folly::make_unique_lock(mutex);
    EXPECT_TRUE(mutex.locked_);
    EXPECT_TRUE(lck);
  }

  EXPECT_FALSE(mutex.locked_);
}

TEST_F(UtilityTest, MakeUniqueLockDeferLock) {
  auto&& mutex = MockMutex{};

  {
    auto lck = folly::make_unique_lock(mutex, std::defer_lock);
    EXPECT_FALSE(mutex.locked_);
    EXPECT_FALSE(lck);
  }

  EXPECT_FALSE(mutex.locked_);
}

TEST_F(UtilityTest, MakeUniqueLockAdoptLock) {
  auto&& mutex = MockMutex{};
  mutex.lock();

  {
    auto lck = folly::make_unique_lock(mutex, std::adopt_lock);
    EXPECT_TRUE(mutex.locked_);
    EXPECT_TRUE(lck);
  }

  EXPECT_FALSE(mutex.locked_);
}

TEST_F(UtilityTest, MakeUniqueLockTryToLock) {
  auto&& mutex = MockMutex{};

  {
    mutex.lock();
    auto lck = folly::make_unique_lock(mutex, std::try_to_lock);
    EXPECT_TRUE(mutex.locked_);
    EXPECT_FALSE(lck);
  }

  EXPECT_TRUE(mutex.locked_);
  mutex.unlock();

  {
    auto lck = folly::make_unique_lock(mutex, std::try_to_lock);
    EXPECT_TRUE(mutex.locked_);
    EXPECT_TRUE(lck);
  }

  EXPECT_FALSE(mutex.locked_);
}

TEST_F(UtilityTest, MakeUniqueLockTimedLock) {
  auto&& mutex = MockMutex{};

  {
    auto lck = folly::make_unique_lock(mutex, std::chrono::seconds{1});
    EXPECT_TRUE(lck);
  }

  EXPECT_FALSE(mutex.locked_);

  {
    auto lck = folly::make_unique_lock(
        mutex, std::chrono::steady_clock::now() + std::chrono::seconds{1});
    EXPECT_TRUE(lck);
  }

  EXPECT_FALSE(mutex.locked_);
}
