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

//
// @author xliu (xliux@fb.com)
//

#include <stdlib.h>
#include <unistd.h>
#include <vector>

#include <boost/thread.hpp>

#include "gtest/gtest.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "folly/RWSpinLock.h"

DEFINE_int32(num_threads, 8, "num threads");

namespace {

static const int kMaxReaders = 50;
static std::atomic<bool> stopThread;
using namespace folly;

template<typename RWSpinLockT> struct RWSpinLockTest: public testing::Test {
  typedef RWSpinLockT RWSpinLockType;
};

typedef testing::Types<RWSpinLock
#if defined(__GNUC__) && (defined(__i386) || defined(__x86_64__) || \
    defined(ARCH_K8))
        , RWTicketSpinLockT<32, true>,
        RWTicketSpinLockT<32, false>,
        RWTicketSpinLockT<64, true>,
        RWTicketSpinLockT<64, false>
#endif
> Implementations;

TYPED_TEST_CASE(RWSpinLockTest, Implementations);

template<typename RWSpinLockType>
static void run(RWSpinLockType* lock) {
  int64_t reads = 0;
  int64_t writes = 0;
  while (!stopThread.load(std::memory_order_acquire)) {
    if (rand() % 10 == 0) { // write
      typename RWSpinLockType::WriteHolder guard(lock);
      ++writes;
    } else { // read
      typename RWSpinLockType::ReadHolder guard(lock);
      ++reads;
    }
  }
  VLOG(0) << "total reads: " << reads << "; total writes: " << writes;
}


TYPED_TEST(RWSpinLockTest, Writer_Wait_Readers) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;

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

TYPED_TEST(RWSpinLockTest, Readers_Wait_Writer) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;

  EXPECT_TRUE(l.try_lock());

  for (int i = 0; i < kMaxReaders; ++i) {
    EXPECT_FALSE(l.try_lock_shared());
  }

  l.unlock_and_lock_shared();
  for (int i = 0; i < kMaxReaders - 1; ++i) {
    EXPECT_TRUE(l.try_lock_shared());
  }
}

TYPED_TEST(RWSpinLockTest, Writer_Wait_Writer) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;

  EXPECT_TRUE(l.try_lock());
  EXPECT_FALSE(l.try_lock());
  l.unlock();

  EXPECT_TRUE(l.try_lock());
  EXPECT_FALSE(l.try_lock());
}

TYPED_TEST(RWSpinLockTest, Read_Holders) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;

  {
    typename RWSpinLockType::ReadHolder guard(&l);
    EXPECT_FALSE(l.try_lock());
    EXPECT_TRUE(l.try_lock_shared());
    l.unlock_shared();

    EXPECT_FALSE(l.try_lock());
  }

  EXPECT_TRUE(l.try_lock());
  l.unlock();
}

TYPED_TEST(RWSpinLockTest, Write_Holders) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;
  {
    typename RWSpinLockType::WriteHolder guard(&l);
    EXPECT_FALSE(l.try_lock());
    EXPECT_FALSE(l.try_lock_shared());
  }

  EXPECT_TRUE(l.try_lock_shared());
  EXPECT_FALSE(l.try_lock());
  l.unlock_shared();
  EXPECT_TRUE(l.try_lock());
}

TYPED_TEST(RWSpinLockTest, ConcurrentTests) {
  typedef typename TestFixture::RWSpinLockType RWSpinLockType;
  RWSpinLockType l;
  srand(time(NULL));

  std::vector<boost::thread> threads;
  for (int i = 0; i < FLAGS_num_threads; ++i) {
    threads.push_back(boost::thread(&run<RWSpinLockType>, &l));
  }

  sleep(1);
  stopThread.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }
}

// RWSpinLock specific tests

TEST(RWSpinLock, lock_unlock_tests) {
  folly::RWSpinLock lock;
  EXPECT_TRUE(lock.try_lock_upgrade());
  EXPECT_TRUE(lock.try_lock_shared());
  EXPECT_FALSE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock_upgrade());
  lock.unlock_upgrade();
  EXPECT_FALSE(lock.try_lock());
  EXPECT_TRUE(lock.try_lock_upgrade());
  lock.unlock_upgrade();
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock_upgrade());
  lock.unlock_and_lock_upgrade();
  EXPECT_TRUE(lock.try_lock_shared());
  lock.unlock_shared();
  lock.unlock_upgrade_and_lock_shared();
  lock.unlock_shared();
  EXPECT_EQ(0, lock.bits());
}

TEST(RWSpinLock, concurrent_holder_test) {
  srand(time(NULL));

  folly::RWSpinLock lock;
  std::atomic<int64_t> reads(0);
  std::atomic<int64_t> writes(0);
  std::atomic<int64_t> upgrades(0);
  std::atomic<bool> stop(false);

  auto go = [&] {
    while (!stop.load(std::memory_order_acquire)) {
      auto r = (uint32_t)(rand()) % 10;
      if (r < 3) {          // starts from write lock
        RWSpinLock::ReadHolder rg(
            RWSpinLock::UpgradedHolder ug(
              RWSpinLock::WriteHolder(&lock)));
        writes.fetch_add(1, std::memory_order_acq_rel);;

      } else if (r < 6) {   // starts from upgrade lock
        RWSpinLock::UpgradedHolder ug(&lock);
        if (r < 4) {
          RWSpinLock::WriteHolder wg(std::move(ug));
        } else {
          RWSpinLock::ReadHolder rg(std::move(ug));
        }
        upgrades.fetch_add(1, std::memory_order_acq_rel);;
      } else {
        RWSpinLock::UpgradedHolder ug(
            RWSpinLock::WriteHolder(
              RWSpinLock::ReadHolder(&lock)));
        reads.fetch_add(1, std::memory_order_acq_rel);
      }
    }
  };

  std::vector<boost::thread> threads;
  for (int i = 0; i < FLAGS_num_threads; ++i) {
    threads.push_back(boost::thread(go));
  }

  sleep(5);
  stop.store(true, std::memory_order_release);

  for (auto& t : threads) t.join();

  LOG(INFO) << "reads: " << reads.load(std::memory_order_acquire)
    << "; writes: " << writes.load(std::memory_order_acquire)
    << "; upgrades: " << upgrades.load(std::memory_order_acquire);
}

}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
