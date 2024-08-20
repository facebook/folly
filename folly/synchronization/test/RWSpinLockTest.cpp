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

#include <folly/synchronization/RWSpinLock.h>

#include <stdlib.h>

#include <thread>
#include <vector>

#include <glog/logging.h>

#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Unistd.h>

DEFINE_int32(num_threads, 8, "num threads");

namespace {

static const int kMaxReaders = 50;
static std::atomic<bool> stopThread;
using namespace folly;

template <typename RWSpinLockT>
struct RWSpinLockTest : public testing::Test {
  typedef RWSpinLockT RWSpinLockType;
};

typedef testing::Types<
    RWSpinLock
#ifdef RW_SPINLOCK_USE_X86_INTRINSIC_
    ,
    RWTicketSpinLockT<32, true>,
    RWTicketSpinLockT<32, false>,
    RWTicketSpinLockT<64, true>,
    RWTicketSpinLockT<64, false>
#endif
    >
    Implementations;

TYPED_TEST_SUITE(RWSpinLockTest, Implementations);

template <typename RWSpinLockType>
static void run(RWSpinLockType& lock) {
  while (!stopThread.load(std::memory_order_acquire)) {
    if (rand() % 10 == 0) { // write
      auto guard = std::unique_lock(lock);
    } else { // read
      auto guard = std::shared_lock(lock);
    }
  }
}

TYPED_TEST(RWSpinLockTest, WriterWaitReaders) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;

  for (int i = 0; i < kMaxReaders; ++i) {
    EXPECT_TRUE(l.try_lock_shared());
    EXPECT_FALSE(l.try_lock());
  }

  for (int i = 0; i < kMaxReaders; ++i) {
    EXPECT_FALSE(l.try_lock());
    l.unlock_shared();
  }

  EXPECT_TRUE(l.try_lock());
}

TYPED_TEST(RWSpinLockTest, ReadersWaitWriter) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;

  EXPECT_TRUE(l.try_lock());

  for (int i = 0; i < kMaxReaders; ++i) {
    EXPECT_FALSE(l.try_lock_shared());
  }

  l.unlock_and_lock_shared();
  for (int i = 0; i < kMaxReaders - 1; ++i) {
    EXPECT_TRUE(l.try_lock_shared());
  }
}

TYPED_TEST(RWSpinLockTest, WriterWaitWriter) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;

  EXPECT_TRUE(l.try_lock());
  EXPECT_FALSE(l.try_lock());
  l.unlock();

  EXPECT_TRUE(l.try_lock());
  EXPECT_FALSE(l.try_lock());
}

TYPED_TEST(RWSpinLockTest, ReadHolders) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;

  {
    std::shared_lock guard(l);
    EXPECT_FALSE(l.try_lock());
    EXPECT_TRUE(l.try_lock_shared());
    l.unlock_shared();

    EXPECT_FALSE(l.try_lock());
  }

  EXPECT_TRUE(l.try_lock());
  l.unlock();
}

TYPED_TEST(RWSpinLockTest, WriteHolders) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;
  {
    std::unique_lock guard(l);
    EXPECT_FALSE(l.try_lock());
    EXPECT_FALSE(l.try_lock_shared());
  }

  EXPECT_TRUE(l.try_lock_shared());
  EXPECT_FALSE(l.try_lock());
  l.unlock_shared();
  EXPECT_TRUE(l.try_lock());
}

TYPED_TEST(RWSpinLockTest, ConcurrentTests) {
  typedef typename TestFixture::RWSpinLockType LockType;
  LockType l;
  srand(time(nullptr));

  std::vector<std::thread> threads;
  for (int i = 0; i < FLAGS_num_threads; ++i) {
    threads.push_back(std::thread(&run<LockType>, std::ref(l)));
  }

  sleep(1);
  stopThread.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }
}

// RWSpinLock specific tests

TEST(RWSpinLock, lockUnlockTests) {
  folly::RWSpinLock lock;
  EXPECT_TRUE(lock.try_lock_upgrade());
  EXPECT_FALSE(lock.try_lock_shared());
  EXPECT_FALSE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock_upgrade());
  lock.unlock_upgrade();
  lock.lock_shared();
  EXPECT_FALSE(lock.try_lock());
  EXPECT_TRUE(lock.try_lock_upgrade());
  lock.unlock_upgrade();
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock_upgrade());
  lock.unlock_and_lock_upgrade();
  EXPECT_FALSE(lock.try_lock_shared());
  lock.unlock_upgrade_and_lock_shared();
  lock.unlock_shared();
  EXPECT_EQ(0, lock.bits());
}

TEST(RWSpinLock, concurrentHolderTest) {
  srand(time(nullptr));

  folly::RWSpinLock lock;
  std::atomic<int64_t> reads(0);
  std::atomic<int64_t> writes(0);
  std::atomic<int64_t> upgrades(0);
  std::atomic<bool> stop(false);

  auto go = [&] {
    while (!stop.load(std::memory_order_acquire)) {
      auto r = (uint32_t)(rand()) % 10;
      if (r < 3) { // starts from write lock
        auto wg = std::unique_lock(lock);
        auto ug = folly::transition_lock<folly::upgrade_lock>(wg);
        auto rg = folly::transition_lock<std::shared_lock>(ug);
        writes.fetch_add(1, std::memory_order_acq_rel);
      } else if (r < 6) { // starts from upgrade lock
        auto ug = folly::upgrade_lock(lock);
        if (r < 4) {
          auto wg = folly::transition_lock<std::unique_lock>(ug);
        } else {
          auto rg = folly::transition_lock<std::shared_lock>(ug);
        }
        upgrades.fetch_add(1, std::memory_order_acq_rel);
      } else {
        auto rg = std::shared_lock(lock);
        reads.fetch_add(1, std::memory_order_acq_rel);
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < FLAGS_num_threads; ++i) {
    threads.push_back(std::thread(go));
  }

  sleep(5);
  stop.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }

  LOG(INFO) << "reads: " << reads.load(std::memory_order_acquire)
            << "; writes: " << writes.load(std::memory_order_acquire)
            << "; upgrades: " << upgrades.load(std::memory_order_acquire);
}

} // namespace
