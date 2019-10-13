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

#include <folly/synchronization/detail/ProxyLockable.h>
#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/DistributedMutex.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

using namespace std::literals;

namespace folly {
namespace detail {

namespace {
DEFINE_int64(stress_test_seconds, 2, "Duration for stress tests");

class MockMutex {
 public:
  int lock() {
    ++locked_;
    return 1;
  }
  void unlock(int integer) {
    --locked_;
    EXPECT_EQ(integer, 1);
  }
  int try_lock() {
    if (!locked_) {
      return lock();
    }

    return 0;
  }

  template <typename Duration>
  int try_lock_for(const Duration&) {
    return try_lock();
  }
  template <typename TimePoint>
  int try_lock_until(const TimePoint&) {
    return try_lock();
  }

  // counts the number of times the mutex has been locked
  int locked_{0};
};
} // namespace

class ProxyLockableTest : public ::testing::Test {};

TEST_F(ProxyLockableTest, UniqueLockBasic) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex};
  std::ignore = lck;

  EXPECT_EQ(mutex.locked_, 1);
}

TEST_F(ProxyLockableTest, UniqueLockDefaultConstruct) {
  auto lck = ProxyLockableUniqueLock<MockMutex>{};

  EXPECT_FALSE(lck.mutex());
  EXPECT_FALSE(lck.proxy());
  EXPECT_FALSE(lck.owns_lock());
  EXPECT_FALSE(lck.operator bool());
}

TEST_F(ProxyLockableTest, UniqueLockLockOnConstruct) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex};

  EXPECT_TRUE(lck.mutex());
  EXPECT_TRUE(lck.proxy());

  EXPECT_EQ(mutex.locked_, 1);
}

TEST_F(ProxyLockableTest, UniqueLockConstructMoveConstructAssign) {
  auto mutex = MockMutex{};

  auto one = ProxyLockableUniqueLock<MockMutex>{mutex};
  EXPECT_TRUE(one.mutex());
  EXPECT_TRUE(one.proxy());

  auto two = std::move(one);
  EXPECT_FALSE(one.mutex());
  EXPECT_FALSE(one.proxy());
  EXPECT_FALSE(one.owns_lock());
  EXPECT_FALSE(one.operator bool());
  EXPECT_TRUE(two.mutex());
  EXPECT_TRUE(two.proxy());

  auto three = std::move(one);
  EXPECT_FALSE(one.mutex());
  EXPECT_FALSE(one.mutex());
  EXPECT_FALSE(three.mutex());
  EXPECT_FALSE(three.mutex());

  auto four = std::move(two);
  EXPECT_TRUE(four.mutex());
  EXPECT_TRUE(four.proxy());
  EXPECT_FALSE(one.proxy());
  EXPECT_FALSE(one.proxy());

  EXPECT_EQ(mutex.locked_, 1);
}

TEST_F(ProxyLockableTest, UniqueLockDeferLock) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex, std::defer_lock};

  EXPECT_EQ(mutex.locked_, 0);
  lck.lock();
  EXPECT_EQ(mutex.locked_, 1);
}

namespace {
template <typename Make>
void testTryToLock(Make make) {
  auto mutex = MockMutex{};

  {
    auto lck = make(mutex);

    EXPECT_TRUE(lck.mutex());
    EXPECT_TRUE(lck.proxy());
    EXPECT_EQ(mutex.locked_, 1);
  }

  EXPECT_EQ(mutex.locked_, 0);
  mutex.lock();

  auto lck = make(mutex);
  EXPECT_EQ(mutex.locked_, 1);
  EXPECT_TRUE(lck.mutex());
  EXPECT_FALSE(lck.proxy());
}
} // namespace

TEST_F(ProxyLockableTest, UniqueLockTryToLock) {
  testTryToLock([](auto& mutex) {
    using Mutex = std::decay_t<decltype(mutex)>;
    return ProxyLockableUniqueLock<Mutex>{mutex, std::try_to_lock};
  });
}

TEST_F(ProxyLockableTest, UniqueLockTimedLockDuration) {
  testTryToLock([](auto& mutex) {
    using Mutex = std::decay_t<decltype(mutex)>;
    return ProxyLockableUniqueLock<Mutex>{mutex, 1s};
  });
}

TEST_F(ProxyLockableTest, UniqueLockTimedLockWithTime) {
  testTryToLock([](auto& mutex) {
    using Mutex = std::decay_t<decltype(mutex)>;
    return ProxyLockableUniqueLock<Mutex>{
        mutex, std::chrono::steady_clock::now() + 1s};
  });
}

TEST_F(ProxyLockableTest, UniqueLockLockExplicitLockAfterDefer) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex, std::defer_lock};
  EXPECT_TRUE(lck.mutex());
  EXPECT_FALSE(lck.proxy());

  lck.lock();

  EXPECT_TRUE(lck.mutex());
  EXPECT_TRUE(lck.proxy());
  EXPECT_EQ(mutex.locked_, 1);
}

TEST_F(ProxyLockableTest, UniqueLockLockExplicitUnlockAfterDefer) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex, std::defer_lock};
  EXPECT_TRUE(lck.mutex());
  EXPECT_FALSE(lck.proxy());

  lck.lock();

  EXPECT_TRUE(lck.mutex());
  EXPECT_TRUE(lck.proxy());
  EXPECT_EQ(mutex.locked_, 1);

  lck.unlock();
  EXPECT_EQ(mutex.locked_, 0);
}

TEST_F(ProxyLockableTest, UniqueLockLockExplicitTryLockAfterDefer) {
  auto mutex = MockMutex{};
  auto lck = ProxyLockableUniqueLock<MockMutex>{mutex, std::defer_lock};
  EXPECT_TRUE(lck.mutex());
  EXPECT_FALSE(lck.proxy());

  EXPECT_TRUE(lck.try_lock());

  EXPECT_TRUE(lck.mutex());
  EXPECT_TRUE(lck.proxy());
  EXPECT_EQ(mutex.locked_, 1);

  lck.unlock();
  EXPECT_EQ(mutex.locked_, 0);
}

TEST_F(ProxyLockableTest, UniqueLockExceptionOnLock) {
  {
    auto lck = ProxyLockableUniqueLock<MockMutex>{};
    if (kIsDebug) {
      EXPECT_THROW(lck.lock(), std::system_error);
    }
  }

  {
    auto mutex = MockMutex{};
    auto lck = ProxyLockableUniqueLock<MockMutex>{mutex};
    if (kIsDebug) {
      EXPECT_THROW(lck.lock(), std::system_error);
    }
  }
}

TEST_F(ProxyLockableTest, UniqueLockExceptionOnUnlock) {
  {
    auto lck = ProxyLockableUniqueLock<MockMutex>{};
    if (kIsDebug) {
      EXPECT_THROW(lck.unlock(), std::system_error);
    }
  }

  {
    auto mutex = MockMutex{};
    auto lck = ProxyLockableUniqueLock<MockMutex>{mutex};
    lck.unlock();
    if (kIsDebug) {
      EXPECT_THROW(lck.unlock(), std::system_error);
    }
  }
}

TEST_F(ProxyLockableTest, UniqueLockExceptionOnTryLock) {
  {
    auto lck = ProxyLockableUniqueLock<MockMutex>{};
    if (kIsDebug) {
      EXPECT_THROW(lck.try_lock(), std::system_error);
    }
  }

  {
    auto mutex = MockMutex{};
    auto lck = ProxyLockableUniqueLock<MockMutex>{mutex};
    if (kIsDebug) {
      EXPECT_THROW(lck.try_lock(), std::system_error);
    }
  }
}

namespace {
class StdMutexWrapper {
 public:
  int lock() {
    mutex_.lock();
    return 1;
  }
  void unlock(int value) {
    EXPECT_EQ(value, 1);
    mutex_.unlock();
  }

  std::mutex mutex_{};
};

template <typename Mutex>
void stressTest() {
  const auto&& kNumThreads = std::thread::hardware_concurrency();
  auto&& mutex = Mutex{};
  auto&& threads = std::vector<std::thread>{};
  auto&& atomic = std::atomic<std::uint64_t>{0};
  auto&& stop = std::atomic<bool>{false};

  // try and randomize thread scheduling
  auto&& randomize = [] {
    if (folly::Random::oneIn(100)) {
      /* sleep override */
      std::this_thread::sleep_for(500us);
    }
  };

  for (auto i = std::size_t{0}; i < kNumThreads; ++i) {
    threads.emplace_back([&] {
      while (!stop.load()) {
        auto lck = ProxyLockableUniqueLock<Mutex>{mutex};
        EXPECT_EQ(atomic.fetch_add(1, std::memory_order_relaxed), 0);
        randomize();
        EXPECT_EQ(atomic.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    });
  }

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::seconds{FLAGS_stress_test_seconds});
  stop.store(true);

  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace

TEST_F(ProxyLockableTest, StressLockOnConstructionStdMutex) {
  stressTest<StdMutexWrapper>();
}

TEST_F(ProxyLockableTest, StressLockOnConstructionFollyDistributedMutex) {
  stressTest<folly::DistributedMutex>();
}

TEST_F(ProxyLockableTest, LockGuardBasic) {
  auto mutex = MockMutex{};
  auto&& lck = ProxyLockableLockGuard<MockMutex>{mutex};
  std::ignore = lck;

  EXPECT_TRUE(mutex.locked_);
}

} // namespace detail
} // namespace folly
