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

#include <folly/SharedMutex.h>

#include <stdlib.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <folly/Benchmark.h>
#include <folly/MPMCQueue.h>
#include <folly/RWSpinLock.h>
#include <folly/Random.h>
#include <folly/portability/GFlags.h>
#include <folly/test/DeterministicSchedule.h>

using namespace folly;
using namespace folly::test;
using namespace std;
using namespace chrono;

typedef DeterministicSchedule DSched;
typedef SharedMutexImpl<true, void, DeterministicAtomic, true>
    DSharedMutexReadPriority;
typedef SharedMutexImpl<false, void, DeterministicAtomic, true>
    DSharedMutexWritePriority;

COMMON_CONCURRENCY_SHARED_MUTEX_DECLARE_STATIC_STORAGE(
    DSharedMutexReadPriority);
COMMON_CONCURRENCY_SHARED_MUTEX_DECLARE_STATIC_STORAGE(
    DSharedMutexWritePriority);

template <typename Lock>
void runBasicTest() {
  Lock lock;
  SharedMutexToken token1;
  SharedMutexToken token2;
  SharedMutexToken token3;

  EXPECT_TRUE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock_shared(token1));
  lock.unlock();

  EXPECT_TRUE(lock.try_lock_shared(token1));
  EXPECT_FALSE(lock.try_lock());
  EXPECT_TRUE(lock.try_lock_shared(token2));
  lock.lock_shared(token3);
  lock.unlock_shared(token3);
  lock.unlock_shared(token2);
  lock.unlock_shared(token1);

  lock.lock();
  lock.unlock();

  lock.lock_shared(token1);
  lock.lock_shared(token2);
  lock.unlock_shared(token1);
  lock.unlock_shared(token2);

  lock.lock();
  lock.unlock_and_lock_shared(token1);
  lock.lock_shared(token2);
  lock.unlock_shared(token2);
  lock.unlock_shared(token1);
}

TEST(SharedMutex, basic) {
  runBasicTest<SharedMutexReadPriority>();
  runBasicTest<SharedMutexWritePriority>();
}

template <typename Lock>
void runBasicHoldersTest() {
  Lock lock;
  SharedMutexToken token;

  {
    // create an exclusive write lock via holder
    typename Lock::WriteHolder holder(lock);
    EXPECT_FALSE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock_shared(token));

    // move ownership to another write holder via move constructor
    typename Lock::WriteHolder holder2(std::move(holder));
    EXPECT_FALSE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock_shared(token));

    // move ownership to another write holder via assign operator
    typename Lock::WriteHolder holder3;
    holder3 = std::move(holder2);
    EXPECT_FALSE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock_shared(token));

    // downgrade from exclusive to upgrade lock via move constructor
    typename Lock::UpgradeHolder holder4(std::move(holder3));

    // ensure we can lock from a shared source
    EXPECT_FALSE(lock.try_lock());
    EXPECT_TRUE(lock.try_lock_shared(token));
    lock.unlock_shared(token);

    // promote from upgrade to exclusive lock via move constructor
    typename Lock::WriteHolder holder5(std::move(holder4));
    EXPECT_FALSE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock_shared(token));

    // downgrade exclusive to shared lock via move constructor
    typename Lock::ReadHolder holder6(std::move(holder5));

    // ensure we can lock from another shared source
    EXPECT_FALSE(lock.try_lock());
    EXPECT_TRUE(lock.try_lock_shared(token));
    lock.unlock_shared(token);
  }

  {
    typename Lock::WriteHolder holder(lock);
    EXPECT_FALSE(lock.try_lock());
  }

  {
    typename Lock::ReadHolder holder(lock);
    typename Lock::ReadHolder holder2(lock);
    typename Lock::UpgradeHolder holder3(lock);
  }

  {
    typename Lock::UpgradeHolder holder(lock);
    typename Lock::ReadHolder holder2(lock);
    typename Lock::ReadHolder holder3(std::move(holder));
  }
}

TEST(SharedMutex, basic_holders) {
  runBasicHoldersTest<SharedMutexReadPriority>();
  runBasicHoldersTest<SharedMutexWritePriority>();
}

template <typename Lock>
void runManyReadLocksTestWithTokens() {
  Lock lock;

  vector<SharedMutexToken> tokens;
  for (int i = 0; i < 1000; ++i) {
    tokens.emplace_back();
    EXPECT_TRUE(lock.try_lock_shared(tokens.back()));
  }
  for (auto& token : tokens) {
    lock.unlock_shared(token);
  }
  EXPECT_TRUE(lock.try_lock());
  lock.unlock();
}

TEST(SharedMutex, many_read_locks_with_tokens) {
  runManyReadLocksTestWithTokens<SharedMutexReadPriority>();
  runManyReadLocksTestWithTokens<SharedMutexWritePriority>();
}

template <typename Lock>
void runManyReadLocksTestWithoutTokens() {
  Lock lock;

  for (int i = 0; i < 1000; ++i) {
    EXPECT_TRUE(lock.try_lock_shared());
  }
  for (int i = 0; i < 1000; ++i) {
    lock.unlock_shared();
  }
  EXPECT_TRUE(lock.try_lock());
  lock.unlock();
}

TEST(SharedMutex, many_read_locks_without_tokens) {
  runManyReadLocksTestWithoutTokens<SharedMutexReadPriority>();
  runManyReadLocksTestWithoutTokens<SharedMutexWritePriority>();
}

template <typename Lock>
void runTimeoutInPastTest() {
  Lock lock;

  EXPECT_TRUE(lock.try_lock_for(milliseconds(0)));
  lock.unlock();
  EXPECT_TRUE(lock.try_lock_for(milliseconds(-1)));
  lock.unlock();
  EXPECT_TRUE(lock.try_lock_shared_for(milliseconds(0)));
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock_shared_for(milliseconds(-1)));
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock_until(system_clock::now() - milliseconds(1)));
  lock.unlock();
  EXPECT_TRUE(
      lock.try_lock_shared_until(system_clock::now() - milliseconds(1)));
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock_until(steady_clock::now() - milliseconds(1)));
  lock.unlock();
  EXPECT_TRUE(
      lock.try_lock_shared_until(steady_clock::now() - milliseconds(1)));
  lock.unlock_shared();
}

TEST(SharedMutex, timeout_in_past) {
  runTimeoutInPastTest<SharedMutexReadPriority>();
  runTimeoutInPastTest<SharedMutexWritePriority>();
}

template <class Func>
bool funcHasDuration(milliseconds expectedDuration, Func func) {
  // elapsed time should eventually fall within expectedDuration +- 25%
  for (int tries = 0; tries < 100; ++tries) {
    auto start = steady_clock::now();
    func();
    auto elapsed = steady_clock::now() - start;
    if (elapsed > expectedDuration - expectedDuration / 4 &&
        elapsed < expectedDuration + expectedDuration / 4) {
      return true;
    }
  }
  return false;
}

template <typename Lock>
void runFailingTryTimeoutTest() {
  Lock lock;
  lock.lock();
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_for(milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    typename Lock::Token token;
    EXPECT_FALSE(lock.try_lock_shared_for(milliseconds(10), token));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_upgrade_for(milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_until(steady_clock::now() + milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    typename Lock::Token token;
    EXPECT_FALSE(lock.try_lock_shared_until(
        steady_clock::now() + milliseconds(10), token));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(
        lock.try_lock_upgrade_until(steady_clock::now() + milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_until(system_clock::now() + milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    typename Lock::Token token;
    EXPECT_FALSE(lock.try_lock_shared_until(
        system_clock::now() + milliseconds(10), token));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(
        lock.try_lock_upgrade_until(system_clock::now() + milliseconds(10)));
  }));
  lock.unlock();

  lock.lock_shared();
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_for(milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_until(steady_clock::now() + milliseconds(10)));
  }));
  EXPECT_TRUE(funcHasDuration(milliseconds(10), [&] {
    EXPECT_FALSE(lock.try_lock_until(system_clock::now() + milliseconds(10)));
  }));
  lock.unlock_shared();

  lock.lock();
  for (int p = 0; p < 8; ++p) {
    EXPECT_FALSE(lock.try_lock_for(nanoseconds(1 << p)));
  }
  lock.unlock();

  for (int p = 0; p < 8; ++p) {
    typename Lock::ReadHolder holder1(lock);
    typename Lock::ReadHolder holder2(lock);
    typename Lock::ReadHolder holder3(lock);
    EXPECT_FALSE(lock.try_lock_for(nanoseconds(1 << p)));
  }
}

TEST(SharedMutex, failing_try_timeout) {
  runFailingTryTimeoutTest<SharedMutexReadPriority>();
  runFailingTryTimeoutTest<SharedMutexWritePriority>();
}

template <typename Lock>
void runBasicUpgradeTest() {
  Lock lock;
  typename Lock::Token token1;
  typename Lock::Token token2;

  lock.lock_upgrade();
  EXPECT_FALSE(lock.try_lock());
  EXPECT_TRUE(lock.try_lock_shared(token1));
  lock.unlock_shared(token1);
  lock.unlock_upgrade();

  lock.lock_upgrade();
  lock.unlock_upgrade_and_lock();
  EXPECT_FALSE(lock.try_lock_shared(token1));
  lock.unlock();

  lock.lock_upgrade();
  lock.unlock_upgrade_and_lock_shared(token1);
  lock.lock_upgrade();
  lock.unlock_upgrade_and_lock_shared(token2);
  lock.unlock_shared(token1);
  lock.unlock_shared(token2);

  lock.lock();
  lock.unlock_and_lock_upgrade();
  EXPECT_TRUE(lock.try_lock_shared(token1));
  lock.unlock_upgrade();
  lock.unlock_shared(token1);
}

TEST(SharedMutex, basic_upgrade_tests) {
  runBasicUpgradeTest<SharedMutexReadPriority>();
  runBasicUpgradeTest<SharedMutexWritePriority>();
}

TEST(SharedMutex, read_has_prio) {
  SharedMutexReadPriority lock;
  SharedMutexToken token1;
  SharedMutexToken token2;
  lock.lock_shared(token1);
  bool exclusiveAcquired = false;
  auto writer = thread([&] {
    lock.lock();
    exclusiveAcquired = true;
    lock.unlock();
  });

  // lock() can't complete until we unlock token1, but it should stake
  // its claim with regards to other exclusive or upgrade locks.  We can
  // use try_lock_upgrade to poll for that eventuality.
  while (lock.try_lock_upgrade()) {
    lock.unlock_upgrade();
    this_thread::yield();
  }
  EXPECT_FALSE(exclusiveAcquired);

  // Even though lock() is stuck we should be able to get token2
  EXPECT_TRUE(lock.try_lock_shared(token2));
  lock.unlock_shared(token1);
  lock.unlock_shared(token2);
  writer.join();
  EXPECT_TRUE(exclusiveAcquired);
}

TEST(SharedMutex, write_has_prio) {
  SharedMutexWritePriority lock;
  SharedMutexToken token1;
  SharedMutexToken token2;
  lock.lock_shared(token1);
  auto writer = thread([&] {
    lock.lock();
    lock.unlock();
  });

  // eventually lock() should block readers
  while (lock.try_lock_shared(token2)) {
    lock.unlock_shared(token2);
    this_thread::yield();
  }

  lock.unlock_shared(token1);
  writer.join();
}

struct TokenLocker {
  SharedMutexToken token;

  template <typename T>
  void lock(T* lock) {
    lock->lock();
  }

  template <typename T>
  void unlock(T* lock) {
    lock->unlock();
  }

  template <typename T>
  void lock_shared(T* lock) {
    lock->lock_shared(token);
  }

  template <typename T>
  void unlock_shared(T* lock) {
    lock->unlock_shared(token);
  }
};

struct Locker {
  template <typename T>
  void lock(T* lock) {
    lock->lock();
  }

  template <typename T>
  void unlock(T* lock) {
    lock->unlock();
  }

  template <typename T>
  void lock_shared(T* lock) {
    lock->lock_shared();
  }

  template <typename T>
  void unlock_shared(T* lock) {
    lock->unlock_shared();
  }
};

struct EnterLocker {
  template <typename T>
  void lock(T* lock) {
    lock->lock(0);
  }

  template <typename T>
  void unlock(T* lock) {
    lock->unlock();
  }

  template <typename T>
  void lock_shared(T* lock) {
    lock->enter(0);
  }

  template <typename T>
  void unlock_shared(T* lock) {
    lock->leave();
  }
};

struct PosixRWLock {
  pthread_rwlock_t lock_;

  PosixRWLock() { pthread_rwlock_init(&lock_, nullptr); }

  ~PosixRWLock() { pthread_rwlock_destroy(&lock_); }

  void lock() { pthread_rwlock_wrlock(&lock_); }

  void unlock() { pthread_rwlock_unlock(&lock_); }

  void lock_shared() { pthread_rwlock_rdlock(&lock_); }

  void unlock_shared() { pthread_rwlock_unlock(&lock_); }
};

struct PosixMutex {
  pthread_mutex_t lock_;

  PosixMutex() { pthread_mutex_init(&lock_, nullptr); }

  ~PosixMutex() { pthread_mutex_destroy(&lock_); }

  void lock() { pthread_mutex_lock(&lock_); }

  void unlock() { pthread_mutex_unlock(&lock_); }

  void lock_shared() { pthread_mutex_lock(&lock_); }

  void unlock_shared() { pthread_mutex_unlock(&lock_); }
};

template <template <typename> class Atom, typename Lock, typename Locker>
static void runContendedReaders(size_t numOps,
                                size_t numThreads,
                                bool useSeparateLocks) {
  char padding1[64];
  (void)padding1;
  Lock globalLock;
  int valueProtectedByLock = 10;
  char padding2[64];
  (void)padding2;
  Atom<bool> go(false);
  Atom<bool>* goPtr = &go; // workaround for clang bug
  vector<thread> threads(numThreads);

  BENCHMARK_SUSPEND {
    for (size_t t = 0; t < numThreads; ++t) {
      threads[t] = DSched::thread([&, t, numThreads] {
        Lock privateLock;
        Lock* lock = useSeparateLocks ? &privateLock : &globalLock;
        Locker locker;
        while (!goPtr->load()) {
          this_thread::yield();
        }
        for (size_t op = t; op < numOps; op += numThreads) {
          locker.lock_shared(lock);
          // note: folly::doNotOptimizeAway reads and writes to its arg,
          // so the following two lines are very different than a call
          // to folly::doNotOptimizeAway(valueProtectedByLock);
          auto copy = valueProtectedByLock;
          folly::doNotOptimizeAway(copy);
          locker.unlock_shared(lock);
        }
      });
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    DSched::join(thr);
  }
}

static void folly_rwspin_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, RWSpinLock, Locker>(
      numOps, numThreads, useSeparateLocks);
}

static void shmtx_wr_pri_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, SharedMutexWritePriority, TokenLocker>(
      numOps, numThreads, useSeparateLocks);
}

static void shmtx_w_bare_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, SharedMutexWritePriority, Locker>(
      numOps, numThreads, useSeparateLocks);
}

static void shmtx_rd_pri_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, SharedMutexReadPriority, TokenLocker>(
      numOps, numThreads, useSeparateLocks);
}

static void shmtx_r_bare_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, SharedMutexReadPriority, Locker>(
      numOps, numThreads, useSeparateLocks);
}

static void folly_ticket_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, RWTicketSpinLock64, Locker>(
      numOps, numThreads, useSeparateLocks);
}

static void boost_shared_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, boost::shared_mutex, Locker>(
      numOps, numThreads, useSeparateLocks);
}

static void pthrd_rwlock_reads(uint numOps,
                               size_t numThreads,
                               bool useSeparateLocks) {
  runContendedReaders<atomic, PosixRWLock, Locker>(
      numOps, numThreads, useSeparateLocks);
}

template <template <typename> class Atom, typename Lock, typename Locker>
static void runMixed(size_t numOps,
                     size_t numThreads,
                     double writeFraction,
                     bool useSeparateLocks) {
  char padding1[64];
  (void)padding1;
  Lock globalLock;
  int valueProtectedByLock = 0;
  char padding2[64];
  (void)padding2;
  Atom<bool> go(false);
  Atom<bool>* goPtr = &go; // workaround for clang bug
  vector<thread> threads(numThreads);

  BENCHMARK_SUSPEND {
    for (size_t t = 0; t < numThreads; ++t) {
      threads[t] = DSched::thread([&, t, numThreads] {
        struct drand48_data buffer;
        srand48_r(t, &buffer);
        long writeThreshold = writeFraction * 0x7fffffff;
        Lock privateLock;
        Lock* lock = useSeparateLocks ? &privateLock : &globalLock;
        Locker locker;
        while (!goPtr->load()) {
          this_thread::yield();
        }
        for (size_t op = t; op < numOps; op += numThreads) {
          long randVal;
          lrand48_r(&buffer, &randVal);
          bool writeOp = randVal < writeThreshold;
          if (writeOp) {
            locker.lock(lock);
            if (!useSeparateLocks) {
              ++valueProtectedByLock;
            }
            locker.unlock(lock);
          } else {
            locker.lock_shared(lock);
            auto v = valueProtectedByLock;
            folly::doNotOptimizeAway(v);
            locker.unlock_shared(lock);
          }
        }
      });
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    DSched::join(thr);
  }
}

static void folly_rwspin(size_t numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, RWSpinLock, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void shmtx_wr_pri(uint numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, SharedMutexWritePriority, TokenLocker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void shmtx_w_bare(uint numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, SharedMutexWritePriority, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void shmtx_rd_pri(uint numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, SharedMutexReadPriority, TokenLocker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void shmtx_r_bare(uint numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, SharedMutexReadPriority, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void folly_ticket(size_t numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, RWTicketSpinLock64, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void boost_shared(size_t numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, boost::shared_mutex, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void pthrd_rwlock(size_t numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, PosixRWLock, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

static void pthrd_mutex_(size_t numOps,
                         size_t numThreads,
                         double writeFraction,
                         bool useSeparateLocks) {
  runMixed<atomic, PosixMutex, Locker>(
      numOps, numThreads, writeFraction, useSeparateLocks);
}

template <typename Lock, template <typename> class Atom>
static void runAllAndValidate(size_t numOps, size_t numThreads) {
  Lock globalLock;
  Atom<int> globalExclusiveCount(0);
  Atom<int> globalUpgradeCount(0);
  Atom<int> globalSharedCount(0);

  Atom<bool> go(false);

  // clang crashes on access to Atom<> captured by ref in closure
  Atom<int>* globalExclusiveCountPtr = &globalExclusiveCount;
  Atom<int>* globalUpgradeCountPtr = &globalUpgradeCount;
  Atom<int>* globalSharedCountPtr = &globalSharedCount;
  Atom<bool>* goPtr = &go;

  vector<thread> threads(numThreads);

  BENCHMARK_SUSPEND {
    for (size_t t = 0; t < numThreads; ++t) {
      threads[t] = DSched::thread([&, t, numThreads] {
        struct drand48_data buffer;
        srand48_r(t, &buffer);

        bool exclusive = false;
        bool upgrade = false;
        bool shared = false;
        bool ourGlobalTokenUsed = false;
        SharedMutexToken ourGlobalToken;

        Lock privateLock;
        vector<SharedMutexToken> privateTokens;

        while (!goPtr->load()) {
          this_thread::yield();
        }
        for (size_t op = t; op < numOps; op += numThreads) {
          // randVal in [0,1000)
          long randVal;
          lrand48_r(&buffer, &randVal);
          randVal = (long)((randVal * (uint64_t)1000) / 0x7fffffff);

          // make as many assertions as possible about the global state
          if (exclusive) {
            EXPECT_EQ(1, globalExclusiveCountPtr->load(memory_order_acquire));
            EXPECT_EQ(0, globalUpgradeCountPtr->load(memory_order_acquire));
            EXPECT_EQ(0, globalSharedCountPtr->load(memory_order_acquire));
          }
          if (upgrade) {
            EXPECT_EQ(0, globalExclusiveCountPtr->load(memory_order_acquire));
            EXPECT_EQ(1, globalUpgradeCountPtr->load(memory_order_acquire));
          }
          if (shared) {
            EXPECT_EQ(0, globalExclusiveCountPtr->load(memory_order_acquire));
            EXPECT_TRUE(globalSharedCountPtr->load(memory_order_acquire) > 0);
          } else {
            EXPECT_FALSE(ourGlobalTokenUsed);
          }

          // independent 20% chance we do something to the private lock
          if (randVal < 200) {
            // it's okay to take multiple private shared locks because
            // we never take an exclusive lock, so reader versus writer
            // priority doesn't cause deadlocks
            if (randVal < 100 && privateTokens.size() > 0) {
              auto i = randVal % privateTokens.size();
              privateLock.unlock_shared(privateTokens[i]);
              privateTokens.erase(privateTokens.begin() + i);
            } else {
              SharedMutexToken token;
              privateLock.lock_shared(token);
              privateTokens.push_back(token);
            }
            continue;
          }

          // if we've got a lock, the only thing we can do is release it
          // or transform it into a different kind of lock
          if (exclusive) {
            exclusive = false;
            --*globalExclusiveCountPtr;
            if (randVal < 500) {
              globalLock.unlock();
            } else if (randVal < 700) {
              globalLock.unlock_and_lock_shared();
              ++*globalSharedCountPtr;
              shared = true;
            } else if (randVal < 900) {
              globalLock.unlock_and_lock_shared(ourGlobalToken);
              ++*globalSharedCountPtr;
              shared = true;
              ourGlobalTokenUsed = true;
            } else {
              globalLock.unlock_and_lock_upgrade();
              ++*globalUpgradeCountPtr;
              upgrade = true;
            }
          } else if (upgrade) {
            upgrade = false;
            --*globalUpgradeCountPtr;
            if (randVal < 500) {
              globalLock.unlock_upgrade();
            } else if (randVal < 700) {
              globalLock.unlock_upgrade_and_lock_shared();
              ++*globalSharedCountPtr;
              shared = true;
            } else if (randVal < 900) {
              globalLock.unlock_upgrade_and_lock_shared(ourGlobalToken);
              ++*globalSharedCountPtr;
              shared = true;
              ourGlobalTokenUsed = true;
            } else {
              globalLock.unlock_upgrade_and_lock();
              ++*globalExclusiveCountPtr;
              exclusive = true;
            }
          } else if (shared) {
            shared = false;
            --*globalSharedCountPtr;
            if (ourGlobalTokenUsed) {
              globalLock.unlock_shared(ourGlobalToken);
              ourGlobalTokenUsed = false;
            } else {
              globalLock.unlock_shared();
            }
          } else if (randVal < 400) {
            // 40% chance of shared lock with token, 5 ways to get it

            // delta t goes from -1 millis to 7 millis
            auto dt = microseconds(10 * (randVal - 100));

            if (randVal < 400) {
              globalLock.lock_shared(ourGlobalToken);
              shared = true;
            } else if (randVal < 500) {
              shared = globalLock.try_lock_shared(ourGlobalToken);
            } else if (randVal < 600) {
              shared = globalLock.try_lock_shared_for(dt, ourGlobalToken);
            } else if (randVal < 800) {
              shared = globalLock.try_lock_shared_until(
                  system_clock::now() + dt, ourGlobalToken);
            }
            if (shared) {
              ourGlobalTokenUsed = true;
              ++*globalSharedCountPtr;
            }
          } else if (randVal < 800) {
            // 40% chance of shared lock without token
            auto dt = microseconds(10 * (randVal - 100));
            if (randVal < 400) {
              globalLock.lock_shared();
              shared = true;
            } else if (randVal < 500) {
              shared = globalLock.try_lock_shared();
            } else if (randVal < 600) {
              shared = globalLock.try_lock_shared_for(dt);
            } else if (randVal < 800) {
              shared = globalLock.try_lock_shared_until(
                  system_clock::now() + dt);
            }
            if (shared) {
              ++*globalSharedCountPtr;
            }
          } else if (randVal < 900) {
            // 10% change of upgrade lock
            globalLock.lock_upgrade();
            upgrade = true;
            ++*globalUpgradeCountPtr;
          } else {
            // 10% chance of exclusive lock, 5 ways to get it

            // delta t goes from -1 millis to 9 millis
            auto dt = microseconds(100 * (randVal - 910));

            if (randVal < 400) {
              globalLock.lock();
              exclusive = true;
            } else if (randVal < 500) {
              exclusive = globalLock.try_lock();
            } else if (randVal < 600) {
              exclusive = globalLock.try_lock_for(dt);
            } else if (randVal < 700) {
              exclusive = globalLock.try_lock_until(steady_clock::now() + dt);
            } else {
              exclusive = globalLock.try_lock_until(system_clock::now() + dt);
            }
            if (exclusive) {
              ++*globalExclusiveCountPtr;
            }
          }
        }

        if (exclusive) {
          --*globalExclusiveCountPtr;
          globalLock.unlock();
        }
        if (upgrade) {
          --*globalUpgradeCountPtr;
          globalLock.unlock_upgrade();
        }
        if (shared) {
          --*globalSharedCountPtr;
          if (ourGlobalTokenUsed) {
            globalLock.unlock_shared(ourGlobalToken);
            ourGlobalTokenUsed = false;
          } else {
            globalLock.unlock_shared();
          }
        }
        for (auto& token : privateTokens) {
          privateLock.unlock_shared(token);
        }
      });
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    DSched::join(thr);
  }
}

TEST(SharedMutex, deterministic_concurrent_readers_of_one_lock_read_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexReadPriority,
                        Locker>(1000, 3, false);
  }
}

TEST(SharedMutex, deterministic_concurrent_readers_of_one_lock_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexWritePriority,
                        Locker>(1000, 3, false);
  }
}

TEST(SharedMutex, concurrent_readers_of_one_lock_read_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    runContendedReaders<atomic, SharedMutexReadPriority, Locker>(
        100000, 32, false);
  }
}

TEST(SharedMutex, concurrent_readers_of_one_lock_write_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    runContendedReaders<atomic, SharedMutexWritePriority, Locker>(
        100000, 32, false);
  }
}

TEST(SharedMutex, deterministic_readers_of_concurrent_locks_read_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexReadPriority,
                        Locker>(1000, 3, true);
  }
}

TEST(SharedMutex, deterministic_readers_of_concurrent_locks_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexWritePriority,
                        Locker>(1000, 3, true);
  }
}

TEST(SharedMutex, readers_of_concurrent_locks_read_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    runContendedReaders<atomic, SharedMutexReadPriority, TokenLocker>(
        100000, 32, true);
  }
}

TEST(SharedMutex, readers_of_concurrent_locks_write_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    runContendedReaders<atomic, SharedMutexWritePriority, TokenLocker>(
        100000, 32, true);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_read_read_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexReadPriority, Locker>(
        1000, 3, 0.1, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_read_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexWritePriority, Locker>(
        1000, 3, 0.1, false);
  }
}

TEST(SharedMutex, mixed_mostly_read_read_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexReadPriority, TokenLocker>(
        10000, 32, 0.1, false);
  }
}

TEST(SharedMutex, mixed_mostly_read_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexWritePriority, TokenLocker>(
        10000, 32, 0.1, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_write_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexReadPriority, TokenLocker>(
        1000, 10, 0.9, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_write_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexWritePriority, TokenLocker>(
        1000, 10, 0.9, false);
  }
}

TEST(SharedMutex, deterministic_lost_wakeup_write_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    DSched sched(DSched::uniformSubset(pass, 2, 200));
    runMixed<DeterministicAtomic, DSharedMutexWritePriority, TokenLocker>(
        1000, 3, 1.0, false);
  }
}

TEST(SharedMutex, mixed_mostly_write_read_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexReadPriority, TokenLocker>(
        50000, 300, 0.9, false);
  }
}

TEST(SharedMutex, mixed_mostly_write_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexWritePriority, TokenLocker>(
        50000, 300, 0.9, false);
  }
}

TEST(SharedMutex, deterministic_all_ops_read_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    DSched sched(DSched::uniform(pass));
    runAllAndValidate<DSharedMutexReadPriority, DeterministicAtomic>(1000, 8);
  }
}

TEST(SharedMutex, deterministic_all_ops_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    DSched sched(DSched::uniform(pass));
    runAllAndValidate<DSharedMutexWritePriority, DeterministicAtomic>(1000, 8);
  }
}

TEST(SharedMutex, all_ops_read_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runAllAndValidate<SharedMutexReadPriority, atomic>(100000, 32);
  }
}

TEST(SharedMutex, all_ops_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runAllAndValidate<SharedMutexWritePriority, atomic>(100000, 32);
  }
}

FOLLY_ASSUME_FBVECTOR_COMPATIBLE(
    boost::optional<boost::optional<SharedMutexToken>>)

// Setup is a set of threads that either grab a shared lock, or exclusive
// and then downgrade it, or upgrade then upgrade and downgrade, then
// enqueue the shared lock to a second set of threads that just performs
// unlocks.  Half of the shared locks use tokens, the others don't.
template <typename Lock, template <typename> class Atom>
static void runRemoteUnlock(size_t numOps,
                            double preWriteFraction,
                            double preUpgradeFraction,
                            size_t numSendingThreads,
                            size_t numReceivingThreads) {
  Lock globalLock;
  MPMCQueue<boost::optional<boost::optional<SharedMutexToken>>, Atom>
    queue(10);
  auto queuePtr = &queue; // workaround for clang crash

  Atom<bool> go(false);
  auto goPtr = &go; // workaround for clang crash
  Atom<int> pendingSenders(numSendingThreads);
  auto pendingSendersPtr = &pendingSenders; // workaround for clang crash
  vector<thread> threads(numSendingThreads + numReceivingThreads);

  BENCHMARK_SUSPEND {
    for (size_t t = 0; t < threads.size(); ++t) {
      threads[t] = DSched::thread([&, t, numSendingThreads] {
        if (t >= numSendingThreads) {
          // we're a receiver
          typename decltype(queue)::value_type elem;
          while (true) {
            queuePtr->blockingRead(elem);
            if (!elem) {
              // EOF, pass the EOF token
              queuePtr->blockingWrite(std::move(elem));
              break;
            }
            if (*elem) {
              globalLock.unlock_shared(**elem);
            } else {
              globalLock.unlock_shared();
            }
          }
          return;
        }
        // else we're a sender

        struct drand48_data buffer;
        srand48_r(t, &buffer);

        while (!goPtr->load()) {
          this_thread::yield();
        }
        for (size_t op = t; op < numOps; op += numSendingThreads) {
          long unscaledRandVal;
          lrand48_r(&buffer, &unscaledRandVal);

          // randVal in [0,1]
          double randVal = ((double)unscaledRandVal) / 0x7fffffff;

          // extract a bit and rescale
          bool useToken = randVal >= 0.5;
          randVal = (randVal - (useToken ? 0.5 : 0.0)) * 2;

          boost::optional<SharedMutexToken> maybeToken;

          if (useToken) {
            SharedMutexToken token;
            if (randVal < preWriteFraction) {
              globalLock.lock();
              globalLock.unlock_and_lock_shared(token);
            } else if (randVal < preWriteFraction + preUpgradeFraction / 2) {
              globalLock.lock_upgrade();
              globalLock.unlock_upgrade_and_lock_shared(token);
            } else if (randVal < preWriteFraction + preUpgradeFraction) {
              globalLock.lock_upgrade();
              globalLock.unlock_upgrade_and_lock();
              globalLock.unlock_and_lock_shared(token);
            } else {
              globalLock.lock_shared(token);
            }
            maybeToken = token;
          } else {
            if (randVal < preWriteFraction) {
              globalLock.lock();
              globalLock.unlock_and_lock_shared();
            } else if (randVal < preWriteFraction + preUpgradeFraction / 2) {
              globalLock.lock_upgrade();
              globalLock.unlock_upgrade_and_lock_shared();
            } else if (randVal < preWriteFraction + preUpgradeFraction) {
              globalLock.lock_upgrade();
              globalLock.unlock_upgrade_and_lock();
              globalLock.unlock_and_lock_shared();
            } else {
              globalLock.lock_shared();
            }
          }

          // blockingWrite is emplace-like, so this automatically adds
          // another level of wrapping
          queuePtr->blockingWrite(maybeToken);
        }
        if (--*pendingSendersPtr == 0) {
          queuePtr->blockingWrite(boost::none);
        }
      });
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    DSched::join(thr);
  }
}

TEST(SharedMutex, deterministic_remote_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runRemoteUnlock<DSharedMutexWritePriority, DeterministicAtomic>(
        500, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, deterministic_remote_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runRemoteUnlock<DSharedMutexReadPriority, DeterministicAtomic>(
        500, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, remote_write_prio) {
  for (int pass = 0; pass < 10; ++pass) {
    runRemoteUnlock<SharedMutexWritePriority, atomic>(100000, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, remote_read_prio) {
  for (int pass = 0; pass < 100; ++pass) {
    runRemoteUnlock<SharedMutexReadPriority, atomic>(100000, 0.1, 0.1, 5, 5);
  }
}

static void burn(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(i);
  }
}

// Two threads and three locks, arranged so that they have to proceed
// in turn with reader/writer conflict
template <typename Lock, template <typename> class Atom = atomic>
static void runPingPong(size_t numRounds, size_t burnCount) {
  char padding1[56];
  (void)padding1;
  pair<Lock, char[56]> locks[3];
  char padding2[56];
  (void)padding2;

  Atom<int> avail(0);
  auto availPtr = &avail; // workaround for clang crash
  Atom<bool> go(false);
  auto goPtr = &go; // workaround for clang crash
  vector<thread> threads(2);

  locks[0].first.lock();
  locks[1].first.lock();
  locks[2].first.lock_shared();

  BENCHMARK_SUSPEND {
    threads[0] = DSched::thread([&] {
      ++*availPtr;
      while (!goPtr->load()) {
        this_thread::yield();
      }
      for (size_t i = 0; i < numRounds; ++i) {
        locks[i % 3].first.unlock();
        locks[(i + 2) % 3].first.lock();
        burn(burnCount);
      }
    });
    threads[1] = DSched::thread([&] {
      ++*availPtr;
      while (!goPtr->load()) {
        this_thread::yield();
      }
      for (size_t i = 0; i < numRounds; ++i) {
        locks[i % 3].first.lock_shared();
        burn(burnCount);
        locks[(i + 2) % 3].first.unlock_shared();
      }
    });

    while (avail.load() < 2) {
      this_thread::yield();
    }
  }

  go.store(true);
  for (auto& thr : threads) {
    DSched::join(thr);
  }
  locks[numRounds % 3].first.unlock();
  locks[(numRounds + 1) % 3].first.unlock();
  locks[(numRounds + 2) % 3].first.unlock_shared();
}

static void folly_rwspin_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<RWSpinLock>(n / scale, burnCount);
}

static void shmtx_w_bare_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<SharedMutexWritePriority>(n / scale, burnCount);
}

static void shmtx_r_bare_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<SharedMutexReadPriority>(n / scale, burnCount);
}

static void folly_ticket_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<RWTicketSpinLock64>(n / scale, burnCount);
}

static void boost_shared_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<boost::shared_mutex>(n / scale, burnCount);
}

static void pthrd_rwlock_ping_pong(size_t n, size_t scale, size_t burnCount) {
  runPingPong<PosixRWLock>(n / scale, burnCount);
}

TEST(SharedMutex, deterministic_ping_pong_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runPingPong<DSharedMutexWritePriority, DeterministicAtomic>(500, 0);
  }
}

TEST(SharedMutex, deterministic_ping_pong_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    DSched sched(DSched::uniform(pass));
    runPingPong<DSharedMutexReadPriority, DeterministicAtomic>(500, 0);
  }
}

TEST(SharedMutex, ping_pong_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    runPingPong<SharedMutexWritePriority, atomic>(50000, 0);
  }
}

TEST(SharedMutex, ping_pong_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    runPingPong<SharedMutexReadPriority, atomic>(50000, 0);
  }
}

// This is here so you can tell how much of the runtime reported by the
// more complex harnesses is due to the harness, although due to the
// magic of compiler optimization it may also be slower
BENCHMARK(single_thread_lock_shared_unlock_shared, iters) {
  SharedMutex lock;
  for (size_t n = 0; n < iters; ++n) {
    SharedMutex::Token token;
    lock.lock_shared(token);
    folly::doNotOptimizeAway(0);
    lock.unlock_shared(token);
  }
}

BENCHMARK(single_thread_lock_unlock, iters) {
  SharedMutex lock;
  for (size_t n = 0; n < iters; ++n) {
    lock.lock();
    folly::doNotOptimizeAway(0);
    lock.unlock();
  }
}

#define BENCH_BASE(args...) BENCHMARK_NAMED_PARAM(args)
#define BENCH_REL(args...) BENCHMARK_RELATIVE_NAMED_PARAM(args)

// 100% reads.  Best-case scenario for deferred locks.  Lock is colocated
// with read data, so inline lock takes cache miss every time but deferred
// lock has only cache hits and local access.
BENCHMARK_DRAW_LINE()
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 1thread, 1, false)
BENCH_REL (shmtx_wr_pri_reads, 1thread, 1, false)
BENCH_REL (shmtx_w_bare_reads, 1thread, 1, false)
BENCH_REL (shmtx_rd_pri_reads, 1thread, 1, false)
BENCH_REL (shmtx_r_bare_reads, 1thread, 1, false)
BENCH_REL (folly_ticket_reads, 1thread, 1, false)
BENCH_REL (boost_shared_reads, 1thread, 1, false)
BENCH_REL (pthrd_rwlock_reads, 1thread, 1, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 2thread, 2, false)
BENCH_REL (shmtx_wr_pri_reads, 2thread, 2, false)
BENCH_REL (shmtx_w_bare_reads, 2thread, 2, false)
BENCH_REL (shmtx_rd_pri_reads, 2thread, 2, false)
BENCH_REL (shmtx_r_bare_reads, 2thread, 2, false)
BENCH_REL (folly_ticket_reads, 2thread, 2, false)
BENCH_REL (boost_shared_reads, 2thread, 2, false)
BENCH_REL (pthrd_rwlock_reads, 2thread, 2, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 4thread, 4, false)
BENCH_REL (shmtx_wr_pri_reads, 4thread, 4, false)
BENCH_REL (shmtx_w_bare_reads, 4thread, 4, false)
BENCH_REL (shmtx_rd_pri_reads, 4thread, 4, false)
BENCH_REL (shmtx_r_bare_reads, 4thread, 4, false)
BENCH_REL (folly_ticket_reads, 4thread, 4, false)
BENCH_REL (boost_shared_reads, 4thread, 4, false)
BENCH_REL (pthrd_rwlock_reads, 4thread, 4, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 8thread, 8, false)
BENCH_REL (shmtx_wr_pri_reads, 8thread, 8, false)
BENCH_REL (shmtx_w_bare_reads, 8thread, 8, false)
BENCH_REL (shmtx_rd_pri_reads, 8thread, 8, false)
BENCH_REL (shmtx_r_bare_reads, 8thread, 8, false)
BENCH_REL (folly_ticket_reads, 8thread, 8, false)
BENCH_REL (boost_shared_reads, 8thread, 8, false)
BENCH_REL (pthrd_rwlock_reads, 8thread, 8, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 16thread, 16, false)
BENCH_REL (shmtx_wr_pri_reads, 16thread, 16, false)
BENCH_REL (shmtx_w_bare_reads, 16thread, 16, false)
BENCH_REL (shmtx_rd_pri_reads, 16thread, 16, false)
BENCH_REL (shmtx_r_bare_reads, 16thread, 16, false)
BENCH_REL (folly_ticket_reads, 16thread, 16, false)
BENCH_REL (boost_shared_reads, 16thread, 16, false)
BENCH_REL (pthrd_rwlock_reads, 16thread, 16, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 32thread, 32, false)
BENCH_REL (shmtx_wr_pri_reads, 32thread, 32, false)
BENCH_REL (shmtx_w_bare_reads, 32thread, 32, false)
BENCH_REL (shmtx_rd_pri_reads, 32thread, 32, false)
BENCH_REL (shmtx_r_bare_reads, 32thread, 32, false)
BENCH_REL (folly_ticket_reads, 32thread, 32, false)
BENCH_REL (boost_shared_reads, 32thread, 32, false)
BENCH_REL (pthrd_rwlock_reads, 32thread, 32, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_reads, 64thread, 64, false)
BENCH_REL (shmtx_wr_pri_reads, 64thread, 64, false)
BENCH_REL (shmtx_w_bare_reads, 64thread, 64, false)
BENCH_REL (shmtx_rd_pri_reads, 64thread, 64, false)
BENCH_REL (shmtx_r_bare_reads, 64thread, 64, false)
BENCH_REL (folly_ticket_reads, 64thread, 64, false)
BENCH_REL (boost_shared_reads, 64thread, 64, false)
BENCH_REL (pthrd_rwlock_reads, 64thread, 64, false)

// 1 lock used by everybody, 100% writes.  Threads only hurt, but it is
// good to not fail catastrophically.  Compare to single_thread_lock_unlock
// to see the overhead of the generic driver (and its pseudo-random number
// generator).  pthrd_mutex_ is a pthread_mutex_t (default, not adaptive),
// which is better than any of the reader-writer locks for this scenario.
BENCHMARK_DRAW_LINE()
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 1thread_all_write, 1, 1.0, false)
BENCH_REL (shmtx_wr_pri, 1thread_all_write, 1, 1.0, false)
BENCH_REL (shmtx_rd_pri, 1thread_all_write, 1, 1.0, false)
BENCH_REL (folly_ticket, 1thread_all_write, 1, 1.0, false)
BENCH_REL (boost_shared, 1thread_all_write, 1, 1.0, false)
BENCH_REL (pthrd_rwlock, 1thread_all_write, 1, 1.0, false)
BENCH_REL (pthrd_mutex_, 1thread_all_write, 1, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thread_all_write, 2, 1.0, false)
BENCH_REL (shmtx_wr_pri, 2thread_all_write, 2, 1.0, false)
BENCH_REL (shmtx_rd_pri, 2thread_all_write, 2, 1.0, false)
BENCH_REL (folly_ticket, 2thread_all_write, 2, 1.0, false)
BENCH_REL (boost_shared, 2thread_all_write, 2, 1.0, false)
BENCH_REL (pthrd_rwlock, 2thread_all_write, 2, 1.0, false)
BENCH_REL (pthrd_mutex_, 2thread_all_write, 2, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 4thread_all_write, 4, 1.0, false)
BENCH_REL (shmtx_wr_pri, 4thread_all_write, 4, 1.0, false)
BENCH_REL (shmtx_rd_pri, 4thread_all_write, 4, 1.0, false)
BENCH_REL (folly_ticket, 4thread_all_write, 4, 1.0, false)
BENCH_REL (boost_shared, 4thread_all_write, 4, 1.0, false)
BENCH_REL (pthrd_rwlock, 4thread_all_write, 4, 1.0, false)
BENCH_REL (pthrd_mutex_, 4thread_all_write, 4, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 8thread_all_write, 8, 1.0, false)
BENCH_REL (shmtx_wr_pri, 8thread_all_write, 8, 1.0, false)
BENCH_REL (shmtx_rd_pri, 8thread_all_write, 8, 1.0, false)
BENCH_REL (folly_ticket, 8thread_all_write, 8, 1.0, false)
BENCH_REL (boost_shared, 8thread_all_write, 8, 1.0, false)
BENCH_REL (pthrd_rwlock, 8thread_all_write, 8, 1.0, false)
BENCH_REL (pthrd_mutex_, 8thread_all_write, 8, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 16thread_all_write, 16, 1.0, false)
BENCH_REL (shmtx_wr_pri, 16thread_all_write, 16, 1.0, false)
BENCH_REL (shmtx_rd_pri, 16thread_all_write, 16, 1.0, false)
BENCH_REL (folly_ticket, 16thread_all_write, 16, 1.0, false)
BENCH_REL (boost_shared, 16thread_all_write, 16, 1.0, false)
BENCH_REL (pthrd_rwlock, 16thread_all_write, 16, 1.0, false)
BENCH_REL (pthrd_mutex_, 16thread_all_write, 16, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 32thread_all_write, 32, 1.0, false)
BENCH_REL (shmtx_wr_pri, 32thread_all_write, 32, 1.0, false)
BENCH_REL (shmtx_rd_pri, 32thread_all_write, 32, 1.0, false)
BENCH_REL (folly_ticket, 32thread_all_write, 32, 1.0, false)
BENCH_REL (boost_shared, 32thread_all_write, 32, 1.0, false)
BENCH_REL (pthrd_rwlock, 32thread_all_write, 32, 1.0, false)
BENCH_REL (pthrd_mutex_, 32thread_all_write, 32, 1.0, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 64thread_all_write, 64, 1.0, false)
BENCH_REL (shmtx_wr_pri, 64thread_all_write, 64, 1.0, false)
BENCH_REL (shmtx_rd_pri, 64thread_all_write, 64, 1.0, false)
BENCH_REL (folly_ticket, 64thread_all_write, 64, 1.0, false)
BENCH_REL (boost_shared, 64thread_all_write, 64, 1.0, false)
BENCH_REL (pthrd_rwlock, 64thread_all_write, 64, 1.0, false)
BENCH_REL (pthrd_mutex_, 64thread_all_write, 64, 1.0, false)

// 1 lock used by everybody, 10% writes.  Not much scaling to be had.  Perf
// is best at 1 thread, once you've got multiple threads > 8 threads hurts.
BENCHMARK_DRAW_LINE()
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 1thread_10pct_write, 1, 0.10, false)
BENCH_REL (shmtx_wr_pri, 1thread_10pct_write, 1, 0.10, false)
BENCH_REL (shmtx_rd_pri, 1thread_10pct_write, 1, 0.10, false)
BENCH_REL (folly_ticket, 1thread_10pct_write, 1, 0.10, false)
BENCH_REL (boost_shared, 1thread_10pct_write, 1, 0.10, false)
BENCH_REL (pthrd_rwlock, 1thread_10pct_write, 1, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thread_10pct_write, 2, 0.10, false)
BENCH_REL (shmtx_wr_pri, 2thread_10pct_write, 2, 0.10, false)
BENCH_REL (shmtx_rd_pri, 2thread_10pct_write, 2, 0.10, false)
BENCH_REL (folly_ticket, 2thread_10pct_write, 2, 0.10, false)
BENCH_REL (boost_shared, 2thread_10pct_write, 2, 0.10, false)
BENCH_REL (pthrd_rwlock, 2thread_10pct_write, 2, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 4thread_10pct_write, 4, 0.10, false)
BENCH_REL (shmtx_wr_pri, 4thread_10pct_write, 4, 0.10, false)
BENCH_REL (shmtx_rd_pri, 4thread_10pct_write, 4, 0.10, false)
BENCH_REL (folly_ticket, 4thread_10pct_write, 4, 0.10, false)
BENCH_REL (boost_shared, 4thread_10pct_write, 4, 0.10, false)
BENCH_REL (pthrd_rwlock, 4thread_10pct_write, 4, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 8thread_10pct_write, 8, 0.10, false)
BENCH_REL (shmtx_wr_pri, 8thread_10pct_write, 8, 0.10, false)
BENCH_REL (shmtx_rd_pri, 8thread_10pct_write, 8, 0.10, false)
BENCH_REL (folly_ticket, 8thread_10pct_write, 8, 0.10, false)
BENCH_REL (boost_shared, 8thread_10pct_write, 8, 0.10, false)
BENCH_REL (pthrd_rwlock, 8thread_10pct_write, 8, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 16thread_10pct_write, 16, 0.10, false)
BENCH_REL (shmtx_wr_pri, 16thread_10pct_write, 16, 0.10, false)
BENCH_REL (shmtx_rd_pri, 16thread_10pct_write, 16, 0.10, false)
BENCH_REL (folly_ticket, 16thread_10pct_write, 16, 0.10, false)
BENCH_REL (boost_shared, 16thread_10pct_write, 16, 0.10, false)
BENCH_REL (pthrd_rwlock, 16thread_10pct_write, 16, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 32thread_10pct_write, 32, 0.10, false)
BENCH_REL (shmtx_wr_pri, 32thread_10pct_write, 32, 0.10, false)
BENCH_REL (shmtx_rd_pri, 32thread_10pct_write, 32, 0.10, false)
BENCH_REL (folly_ticket, 32thread_10pct_write, 32, 0.10, false)
BENCH_REL (boost_shared, 32thread_10pct_write, 32, 0.10, false)
BENCH_REL (pthrd_rwlock, 32thread_10pct_write, 32, 0.10, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 64thread_10pct_write, 64, 0.10, false)
BENCH_REL (shmtx_wr_pri, 64thread_10pct_write, 64, 0.10, false)
BENCH_REL (shmtx_rd_pri, 64thread_10pct_write, 64, 0.10, false)
BENCH_REL (folly_ticket, 64thread_10pct_write, 64, 0.10, false)
BENCH_REL (boost_shared, 64thread_10pct_write, 64, 0.10, false)
BENCH_REL (pthrd_rwlock, 64thread_10pct_write, 64, 0.10, false)

// 1 lock used by everybody, 1% writes.  This is a more realistic example
// than the concurrent_*_reads benchmark, but still shows SharedMutex locks
// winning over all of the others
BENCHMARK_DRAW_LINE()
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (shmtx_wr_pri, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (shmtx_w_bare, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (shmtx_rd_pri, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (shmtx_r_bare, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (folly_ticket, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (boost_shared, 1thread_1pct_write, 1, 0.01, false)
BENCH_REL (pthrd_rwlock, 1thread_1pct_write, 1, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (shmtx_wr_pri, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (shmtx_w_bare, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (shmtx_rd_pri, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (shmtx_r_bare, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (folly_ticket, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (boost_shared, 2thread_1pct_write, 2, 0.01, false)
BENCH_REL (pthrd_rwlock, 2thread_1pct_write, 2, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (shmtx_wr_pri, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (shmtx_w_bare, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (shmtx_rd_pri, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (shmtx_r_bare, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (folly_ticket, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (boost_shared, 4thread_1pct_write, 4, 0.01, false)
BENCH_REL (pthrd_rwlock, 4thread_1pct_write, 4, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (shmtx_wr_pri, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (shmtx_w_bare, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (shmtx_rd_pri, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (shmtx_r_bare, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (folly_ticket, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (boost_shared, 8thread_1pct_write, 8, 0.01, false)
BENCH_REL (pthrd_rwlock, 8thread_1pct_write, 8, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (shmtx_wr_pri, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (shmtx_w_bare, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (shmtx_rd_pri, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (shmtx_r_bare, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (folly_ticket, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (boost_shared, 16thread_1pct_write, 16, 0.01, false)
BENCH_REL (pthrd_rwlock, 16thread_1pct_write, 16, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (shmtx_wr_pri, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (shmtx_w_bare, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (shmtx_rd_pri, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (shmtx_r_bare, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (folly_ticket, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (boost_shared, 32thread_1pct_write, 32, 0.01, false)
BENCH_REL (pthrd_rwlock, 32thread_1pct_write, 32, 0.01, false)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (shmtx_wr_pri, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (shmtx_w_bare, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (shmtx_rd_pri, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (shmtx_r_bare, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (folly_ticket, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (boost_shared, 64thread_1pct_write, 64, 0.01, false)
BENCH_REL (pthrd_rwlock, 64thread_1pct_write, 64, 0.01, false)

// Worst case scenario for deferred locks. No actual sharing, likely that
// read operations will have to first set the kDeferredReadersPossibleBit,
// and likely that writers will have to scan deferredReaders[].
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thr_2lock_50pct_write, 2, 0.50, true)
BENCH_REL (shmtx_wr_pri, 2thr_2lock_50pct_write, 2, 0.50, true)
BENCH_REL (shmtx_rd_pri, 2thr_2lock_50pct_write, 2, 0.50, true)
BENCH_BASE(folly_rwspin, 4thr_4lock_50pct_write, 4, 0.50, true)
BENCH_REL (shmtx_wr_pri, 4thr_4lock_50pct_write, 4, 0.50, true)
BENCH_REL (shmtx_rd_pri, 4thr_4lock_50pct_write, 4, 0.50, true)
BENCH_BASE(folly_rwspin, 8thr_8lock_50pct_write, 8, 0.50, true)
BENCH_REL (shmtx_wr_pri, 8thr_8lock_50pct_write, 8, 0.50, true)
BENCH_REL (shmtx_rd_pri, 8thr_8lock_50pct_write, 8, 0.50, true)
BENCH_BASE(folly_rwspin, 16thr_16lock_50pct_write, 16, 0.50, true)
BENCH_REL (shmtx_wr_pri, 16thr_16lock_50pct_write, 16, 0.50, true)
BENCH_REL (shmtx_rd_pri, 16thr_16lock_50pct_write, 16, 0.50, true)
BENCH_BASE(folly_rwspin, 32thr_32lock_50pct_write, 32, 0.50, true)
BENCH_REL (shmtx_wr_pri, 32thr_32lock_50pct_write, 32, 0.50, true)
BENCH_REL (shmtx_rd_pri, 32thr_32lock_50pct_write, 32, 0.50, true)
BENCH_BASE(folly_rwspin, 64thr_64lock_50pct_write, 64, 0.50, true)
BENCH_REL (shmtx_wr_pri, 64thr_64lock_50pct_write, 64, 0.50, true)
BENCH_REL (shmtx_rd_pri, 64thr_64lock_50pct_write, 64, 0.50, true)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thr_2lock_10pct_write, 2, 0.10, true)
BENCH_REL (shmtx_wr_pri, 2thr_2lock_10pct_write, 2, 0.10, true)
BENCH_REL (shmtx_rd_pri, 2thr_2lock_10pct_write, 2, 0.10, true)
BENCH_BASE(folly_rwspin, 4thr_4lock_10pct_write, 4, 0.10, true)
BENCH_REL (shmtx_wr_pri, 4thr_4lock_10pct_write, 4, 0.10, true)
BENCH_REL (shmtx_rd_pri, 4thr_4lock_10pct_write, 4, 0.10, true)
BENCH_BASE(folly_rwspin, 8thr_8lock_10pct_write, 8, 0.10, true)
BENCH_REL (shmtx_wr_pri, 8thr_8lock_10pct_write, 8, 0.10, true)
BENCH_REL (shmtx_rd_pri, 8thr_8lock_10pct_write, 8, 0.10, true)
BENCH_BASE(folly_rwspin, 16thr_16lock_10pct_write, 16, 0.10, true)
BENCH_REL (shmtx_wr_pri, 16thr_16lock_10pct_write, 16, 0.10, true)
BENCH_REL (shmtx_rd_pri, 16thr_16lock_10pct_write, 16, 0.10, true)
BENCH_BASE(folly_rwspin, 32thr_32lock_10pct_write, 32, 0.10, true)
BENCH_REL (shmtx_wr_pri, 32thr_32lock_10pct_write, 32, 0.10, true)
BENCH_REL (shmtx_rd_pri, 32thr_32lock_10pct_write, 32, 0.10, true)
BENCH_BASE(folly_rwspin, 64thr_64lock_10pct_write, 64, 0.10, true)
BENCH_REL (shmtx_wr_pri, 64thr_64lock_10pct_write, 64, 0.10, true)
BENCH_REL (shmtx_rd_pri, 64thr_64lock_10pct_write, 64, 0.10, true)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin, 2thr_2lock_1pct_write, 2, 0.01, true)
BENCH_REL (shmtx_wr_pri, 2thr_2lock_1pct_write, 2, 0.01, true)
BENCH_REL (shmtx_rd_pri, 2thr_2lock_1pct_write, 2, 0.01, true)
BENCH_BASE(folly_rwspin, 4thr_4lock_1pct_write, 4, 0.01, true)
BENCH_REL (shmtx_wr_pri, 4thr_4lock_1pct_write, 4, 0.01, true)
BENCH_REL (shmtx_rd_pri, 4thr_4lock_1pct_write, 4, 0.01, true)
BENCH_BASE(folly_rwspin, 8thr_8lock_1pct_write, 8, 0.01, true)
BENCH_REL (shmtx_wr_pri, 8thr_8lock_1pct_write, 8, 0.01, true)
BENCH_REL (shmtx_rd_pri, 8thr_8lock_1pct_write, 8, 0.01, true)
BENCH_BASE(folly_rwspin, 16thr_16lock_1pct_write, 16, 0.01, true)
BENCH_REL (shmtx_wr_pri, 16thr_16lock_1pct_write, 16, 0.01, true)
BENCH_REL (shmtx_rd_pri, 16thr_16lock_1pct_write, 16, 0.01, true)
BENCH_BASE(folly_rwspin, 32thr_32lock_1pct_write, 32, 0.01, true)
BENCH_REL (shmtx_wr_pri, 32thr_32lock_1pct_write, 32, 0.01, true)
BENCH_REL (shmtx_rd_pri, 32thr_32lock_1pct_write, 32, 0.01, true)
BENCH_BASE(folly_rwspin, 64thr_64lock_1pct_write, 64, 0.01, true)
BENCH_REL (shmtx_wr_pri, 64thr_64lock_1pct_write, 64, 0.01, true)
BENCH_REL (shmtx_rd_pri, 64thr_64lock_1pct_write, 64, 0.01, true)

// Ping-pong tests have a scaled number of iterations, because their burn
// loop would make them too slow otherwise.  Ping-pong with burn count of
// 100k or 300k shows the advantage of soft-spin, reducing the cost of
// each wakeup by about 20 usec.  (Take benchmark reported difference,
// ~400 nanos, multiply by the scale of 100, then divide by 2 because
// each round has two wakeups.)
BENCHMARK_DRAW_LINE()
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_ping_pong, burn0, 1, 0)
BENCH_REL (shmtx_w_bare_ping_pong, burn0, 1, 0)
BENCH_REL (shmtx_r_bare_ping_pong, burn0, 1, 0)
BENCH_REL (folly_ticket_ping_pong, burn0, 1, 0)
BENCH_REL (boost_shared_ping_pong, burn0, 1, 0)
BENCH_REL (pthrd_rwlock_ping_pong, burn0, 1, 0)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_ping_pong, burn100k, 100, 100000)
BENCH_REL (shmtx_w_bare_ping_pong, burn100k, 100, 100000)
BENCH_REL (shmtx_r_bare_ping_pong, burn100k, 100, 100000)
BENCH_REL (folly_ticket_ping_pong, burn100k, 100, 100000)
BENCH_REL (boost_shared_ping_pong, burn100k, 100, 100000)
BENCH_REL (pthrd_rwlock_ping_pong, burn100k, 100, 100000)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_ping_pong, burn300k, 100, 300000)
BENCH_REL (shmtx_w_bare_ping_pong, burn300k, 100, 300000)
BENCH_REL (shmtx_r_bare_ping_pong, burn300k, 100, 300000)
BENCH_REL (folly_ticket_ping_pong, burn300k, 100, 300000)
BENCH_REL (boost_shared_ping_pong, burn300k, 100, 300000)
BENCH_REL (pthrd_rwlock_ping_pong, burn300k, 100, 300000)
BENCHMARK_DRAW_LINE()
BENCH_BASE(folly_rwspin_ping_pong, burn1M, 1000, 1000000)
BENCH_REL (shmtx_w_bare_ping_pong, burn1M, 1000, 1000000)
BENCH_REL (shmtx_r_bare_ping_pong, burn1M, 1000, 1000000)
BENCH_REL (folly_ticket_ping_pong, burn1M, 1000, 1000000)
BENCH_REL (boost_shared_ping_pong, burn1M, 1000, 1000000)
BENCH_REL (pthrd_rwlock_ping_pong, burn1M, 1000, 1000000)

// Reproduce with 10 minutes and
//   sudo nice -n -20
//     shared_mutex_test --benchmark --bm_min_iters=1000000
//
// Comparison use folly::RWSpinLock as the baseline, with the
// following row being the default SharedMutex (using *Holder or
// Token-ful methods).
// ============================================================================
// folly/experimental/test/SharedMutexTest.cpp     relative  time/iter  iters/s
// ============================================================================
// single_thread_lock_shared_unlock_shared                     22.78ns   43.89M
// single_thread_lock_unlock                                   26.01ns   38.45M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin_reads(1thread)                                 15.09ns   66.25M
// shmtx_wr_pri_reads(1thread)                       69.89%    21.60ns   46.30M
// shmtx_w_bare_reads(1thread)                       58.25%    25.91ns   38.59M
// shmtx_rd_pri_reads(1thread)                       72.50%    20.82ns   48.03M
// shmtx_r_bare_reads(1thread)                       58.27%    25.91ns   38.60M
// folly_ticket_reads(1thread)                       54.80%    27.55ns   36.30M
// boost_shared_reads(1thread)                       10.88%   138.80ns    7.20M
// pthrd_rwlock_reads(1thread)                       40.68%    37.11ns   26.95M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(2thread)                                 92.63ns   10.80M
// shmtx_wr_pri_reads(2thread)                      462.86%    20.01ns   49.97M
// shmtx_w_bare_reads(2thread)                      430.53%    21.51ns   46.48M
// shmtx_rd_pri_reads(2thread)                      487.13%    19.01ns   52.59M
// shmtx_r_bare_reads(2thread)                      433.35%    21.37ns   46.79M
// folly_ticket_reads(2thread)                       69.82%   132.67ns    7.54M
// boost_shared_reads(2thread)                       36.66%   252.63ns    3.96M
// pthrd_rwlock_reads(2thread)                      127.76%    72.50ns   13.79M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(4thread)                                 97.45ns   10.26M
// shmtx_wr_pri_reads(4thread)                      978.22%     9.96ns  100.38M
// shmtx_w_bare_reads(4thread)                      908.35%    10.73ns   93.21M
// shmtx_rd_pri_reads(4thread)                     1032.29%     9.44ns  105.93M
// shmtx_r_bare_reads(4thread)                      912.38%    10.68ns   93.63M
// folly_ticket_reads(4thread)                       46.08%   211.46ns    4.73M
// boost_shared_reads(4thread)                       25.00%   389.74ns    2.57M
// pthrd_rwlock_reads(4thread)                       47.53%   205.01ns    4.88M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(8thread)                                147.24ns    6.79M
// shmtx_wr_pri_reads(8thread)                     2915.66%     5.05ns  198.02M
// shmtx_w_bare_reads(8thread)                     2699.32%     5.45ns  183.32M
// shmtx_rd_pri_reads(8thread)                     3092.58%     4.76ns  210.03M
// shmtx_r_bare_reads(8thread)                     2744.63%     5.36ns  186.40M
// folly_ticket_reads(8thread)                       54.84%   268.47ns    3.72M
// boost_shared_reads(8thread)                       42.40%   347.30ns    2.88M
// pthrd_rwlock_reads(8thread)                       78.90%   186.63ns    5.36M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(16thread)                               166.25ns    6.02M
// shmtx_wr_pri_reads(16thread)                    6133.03%     2.71ns  368.91M
// shmtx_w_bare_reads(16thread)                    5936.05%     2.80ns  357.06M
// shmtx_rd_pri_reads(16thread)                    6786.57%     2.45ns  408.22M
// shmtx_r_bare_reads(16thread)                    5995.54%     2.77ns  360.64M
// folly_ticket_reads(16thread)                      56.35%   295.01ns    3.39M
// boost_shared_reads(16thread)                      51.62%   322.08ns    3.10M
// pthrd_rwlock_reads(16thread)                      92.47%   179.79ns    5.56M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(32thread)                               107.72ns    9.28M
// shmtx_wr_pri_reads(32thread)                    6772.80%     1.59ns  628.77M
// shmtx_w_bare_reads(32thread)                    6236.13%     1.73ns  578.94M
// shmtx_rd_pri_reads(32thread)                    8143.32%     1.32ns  756.00M
// shmtx_r_bare_reads(32thread)                    6485.18%     1.66ns  602.06M
// folly_ticket_reads(32thread)                      35.12%   306.73ns    3.26M
// boost_shared_reads(32thread)                      28.19%   382.17ns    2.62M
// pthrd_rwlock_reads(32thread)                      65.29%   164.99ns    6.06M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(64thread)                               119.46ns    8.37M
// shmtx_wr_pri_reads(64thread)                    6744.92%     1.77ns  564.60M
// shmtx_w_bare_reads(64thread)                    6268.50%     1.91ns  524.72M
// shmtx_rd_pri_reads(64thread)                    7508.56%     1.59ns  628.52M
// shmtx_r_bare_reads(64thread)                    6299.53%     1.90ns  527.32M
// folly_ticket_reads(64thread)                      37.42%   319.26ns    3.13M
// boost_shared_reads(64thread)                      32.58%   366.70ns    2.73M
// pthrd_rwlock_reads(64thread)                      73.64%   162.24ns    6.16M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_all_write)                             25.51ns   39.19M
// shmtx_wr_pri(1thread_all_write)                   97.38%    26.20ns   38.17M
// shmtx_rd_pri(1thread_all_write)                   97.55%    26.16ns   38.23M
// folly_ticket(1thread_all_write)                   90.98%    28.04ns   35.66M
// boost_shared(1thread_all_write)                   16.80%   151.89ns    6.58M
// pthrd_rwlock(1thread_all_write)                   63.86%    39.96ns   25.03M
// pthrd_mutex_(1thread_all_write)                   82.05%    31.09ns   32.16M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_all_write)                            100.70ns    9.93M
// shmtx_wr_pri(2thread_all_write)                   40.83%   246.61ns    4.05M
// shmtx_rd_pri(2thread_all_write)                   40.53%   248.44ns    4.03M
// folly_ticket(2thread_all_write)                   58.49%   172.17ns    5.81M
// boost_shared(2thread_all_write)                   24.26%   415.00ns    2.41M
// pthrd_rwlock(2thread_all_write)                   41.35%   243.49ns    4.11M
// pthrd_mutex_(2thread_all_write)                  146.91%    68.55ns   14.59M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_all_write)                            199.52ns    5.01M
// shmtx_wr_pri(4thread_all_write)                   51.71%   385.86ns    2.59M
// shmtx_rd_pri(4thread_all_write)                   49.43%   403.62ns    2.48M
// folly_ticket(4thread_all_write)                  117.88%   169.26ns    5.91M
// boost_shared(4thread_all_write)                    9.81%     2.03us  491.48K
// pthrd_rwlock(4thread_all_write)                   28.23%   706.69ns    1.42M
// pthrd_mutex_(4thread_all_write)                  111.54%   178.88ns    5.59M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_all_write)                            304.61ns    3.28M
// shmtx_wr_pri(8thread_all_write)                   69.77%   436.59ns    2.29M
// shmtx_rd_pri(8thread_all_write)                   66.58%   457.51ns    2.19M
// folly_ticket(8thread_all_write)                  141.00%   216.03ns    4.63M
// boost_shared(8thread_all_write)                    6.11%     4.99us  200.59K
// pthrd_rwlock(8thread_all_write)                   38.03%   800.88ns    1.25M
// pthrd_mutex_(8thread_all_write)                  177.66%   171.45ns    5.83M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_all_write)                           576.97ns    1.73M
// shmtx_wr_pri(16thread_all_write)                 105.72%   545.77ns    1.83M
// shmtx_rd_pri(16thread_all_write)                 105.13%   548.83ns    1.82M
// folly_ticket(16thread_all_write)                 161.70%   356.82ns    2.80M
// boost_shared(16thread_all_write)                   7.73%     7.46us  134.03K
// pthrd_rwlock(16thread_all_write)                  96.88%   595.54ns    1.68M
// pthrd_mutex_(16thread_all_write)                 330.44%   174.61ns    5.73M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_all_write)                             1.41us  707.76K
// shmtx_wr_pri(32thread_all_write)                 240.46%   587.58ns    1.70M
// shmtx_rd_pri(32thread_all_write)                 393.71%   358.87ns    2.79M
// folly_ticket(32thread_all_write)                 325.07%   434.65ns    2.30M
// boost_shared(32thread_all_write)                  18.57%     7.61us  131.43K
// pthrd_rwlock(32thread_all_write)                 266.78%   529.62ns    1.89M
// pthrd_mutex_(32thread_all_write)                 877.89%   160.94ns    6.21M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_all_write)                             1.76us  566.94K
// shmtx_wr_pri(64thread_all_write)                 255.67%   689.91ns    1.45M
// shmtx_rd_pri(64thread_all_write)                 468.82%   376.23ns    2.66M
// folly_ticket(64thread_all_write)                 294.72%   598.49ns    1.67M
// boost_shared(64thread_all_write)                  23.39%     7.54us  132.58K
// pthrd_rwlock(64thread_all_write)                 321.39%   548.83ns    1.82M
// pthrd_mutex_(64thread_all_write)                1165.04%   151.40ns    6.61M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_10pct_write)                           19.51ns   51.26M
// shmtx_wr_pri(1thread_10pct_write)                 83.25%    23.43ns   42.67M
// shmtx_rd_pri(1thread_10pct_write)                 83.31%    23.42ns   42.71M
// folly_ticket(1thread_10pct_write)                 70.88%    27.52ns   36.34M
// boost_shared(1thread_10pct_write)                 13.09%   148.99ns    6.71M
// pthrd_rwlock(1thread_10pct_write)                 47.41%    41.15ns   24.30M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_10pct_write)                          159.42ns    6.27M
// shmtx_wr_pri(2thread_10pct_write)                188.44%    84.60ns   11.82M
// shmtx_rd_pri(2thread_10pct_write)                188.29%    84.67ns   11.81M
// folly_ticket(2thread_10pct_write)                140.28%   113.64ns    8.80M
// boost_shared(2thread_10pct_write)                 42.09%   378.81ns    2.64M
// pthrd_rwlock(2thread_10pct_write)                103.86%   153.49ns    6.51M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_10pct_write)                          193.35ns    5.17M
// shmtx_wr_pri(4thread_10pct_write)                184.30%   104.91ns    9.53M
// shmtx_rd_pri(4thread_10pct_write)                163.76%   118.07ns    8.47M
// folly_ticket(4thread_10pct_write)                124.07%   155.84ns    6.42M
// boost_shared(4thread_10pct_write)                 16.32%     1.18us  843.92K
// pthrd_rwlock(4thread_10pct_write)                 48.59%   397.94ns    2.51M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_10pct_write)                          373.17ns    2.68M
// shmtx_wr_pri(8thread_10pct_write)                252.02%   148.08ns    6.75M
// shmtx_rd_pri(8thread_10pct_write)                203.59%   183.30ns    5.46M
// folly_ticket(8thread_10pct_write)                184.37%   202.40ns    4.94M
// boost_shared(8thread_10pct_write)                 15.85%     2.35us  424.72K
// pthrd_rwlock(8thread_10pct_write)                 83.03%   449.45ns    2.22M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_10pct_write)                         742.87ns    1.35M
// shmtx_wr_pri(16thread_10pct_write)               344.27%   215.78ns    4.63M
// shmtx_rd_pri(16thread_10pct_write)               287.04%   258.80ns    3.86M
// folly_ticket(16thread_10pct_write)               277.25%   267.94ns    3.73M
// boost_shared(16thread_10pct_write)                15.33%     4.85us  206.30K
// pthrd_rwlock(16thread_10pct_write)               158.34%   469.16ns    2.13M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_10pct_write)                         799.97ns    1.25M
// shmtx_wr_pri(32thread_10pct_write)               351.40%   227.65ns    4.39M
// shmtx_rd_pri(32thread_10pct_write)               341.71%   234.11ns    4.27M
// folly_ticket(32thread_10pct_write)               245.91%   325.31ns    3.07M
// boost_shared(32thread_10pct_write)                 7.72%    10.36us   96.56K
// pthrd_rwlock(32thread_10pct_write)               165.87%   482.30ns    2.07M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_10pct_write)                           1.12us  892.01K
// shmtx_wr_pri(64thread_10pct_write)               429.84%   260.81ns    3.83M
// shmtx_rd_pri(64thread_10pct_write)               456.93%   245.35ns    4.08M
// folly_ticket(64thread_10pct_write)               219.21%   511.42ns    1.96M
// boost_shared(64thread_10pct_write)                 5.43%    20.65us   48.44K
// pthrd_rwlock(64thread_10pct_write)               233.93%   479.23ns    2.09M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_1pct_write)                            18.88ns   52.98M
// shmtx_wr_pri(1thread_1pct_write)                  81.53%    23.15ns   43.19M
// shmtx_w_bare(1thread_1pct_write)                  67.90%    27.80ns   35.97M
// shmtx_rd_pri(1thread_1pct_write)                  81.50%    23.16ns   43.18M
// shmtx_r_bare(1thread_1pct_write)                  67.74%    27.86ns   35.89M
// folly_ticket(1thread_1pct_write)                  68.68%    27.48ns   36.39M
// boost_shared(1thread_1pct_write)                  12.80%   147.51ns    6.78M
// pthrd_rwlock(1thread_1pct_write)                  45.81%    41.20ns   24.27M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_1pct_write)                           125.85ns    7.95M
// shmtx_wr_pri(2thread_1pct_write)                 359.04%    35.05ns   28.53M
// shmtx_w_bare(2thread_1pct_write)                 475.60%    26.46ns   37.79M
// shmtx_rd_pri(2thread_1pct_write)                 332.75%    37.82ns   26.44M
// shmtx_r_bare(2thread_1pct_write)                 115.64%   108.83ns    9.19M
// folly_ticket(2thread_1pct_write)                 140.24%    89.74ns   11.14M
// boost_shared(2thread_1pct_write)                  40.62%   309.82ns    3.23M
// pthrd_rwlock(2thread_1pct_write)                 134.67%    93.45ns   10.70M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_1pct_write)                           126.70ns    7.89M
// shmtx_wr_pri(4thread_1pct_write)                 422.20%    30.01ns   33.32M
// shmtx_w_bare(4thread_1pct_write)                 403.52%    31.40ns   31.85M
// shmtx_rd_pri(4thread_1pct_write)                 282.50%    44.85ns   22.30M
// shmtx_r_bare(4thread_1pct_write)                  66.30%   191.10ns    5.23M
// folly_ticket(4thread_1pct_write)                  91.93%   137.83ns    7.26M
// boost_shared(4thread_1pct_write)                  22.74%   557.10ns    1.80M
// pthrd_rwlock(4thread_1pct_write)                  55.66%   227.62ns    4.39M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_1pct_write)                           169.42ns    5.90M
// shmtx_wr_pri(8thread_1pct_write)                 567.81%    29.84ns   33.51M
// shmtx_w_bare(8thread_1pct_write)                 519.18%    32.63ns   30.64M
// shmtx_rd_pri(8thread_1pct_write)                 172.36%    98.30ns   10.17M
// shmtx_r_bare(8thread_1pct_write)                  75.56%   224.21ns    4.46M
// folly_ticket(8thread_1pct_write)                 104.03%   162.85ns    6.14M
// boost_shared(8thread_1pct_write)                  22.01%   769.73ns    1.30M
// pthrd_rwlock(8thread_1pct_write)                  71.79%   235.99ns    4.24M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_1pct_write)                          385.88ns    2.59M
// shmtx_wr_pri(16thread_1pct_write)               1039.03%    37.14ns   26.93M
// shmtx_w_bare(16thread_1pct_write)                997.26%    38.69ns   25.84M
// shmtx_rd_pri(16thread_1pct_write)                263.60%   146.39ns    6.83M
// shmtx_r_bare(16thread_1pct_write)                173.16%   222.85ns    4.49M
// folly_ticket(16thread_1pct_write)                179.37%   215.13ns    4.65M
// boost_shared(16thread_1pct_write)                 26.95%     1.43us  698.42K
// pthrd_rwlock(16thread_1pct_write)                166.70%   231.48ns    4.32M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_1pct_write)                          382.49ns    2.61M
// shmtx_wr_pri(32thread_1pct_write)               1046.64%    36.54ns   27.36M
// shmtx_w_bare(32thread_1pct_write)                922.87%    41.45ns   24.13M
// shmtx_rd_pri(32thread_1pct_write)                251.93%   151.82ns    6.59M
// shmtx_r_bare(32thread_1pct_write)                176.44%   216.78ns    4.61M
// folly_ticket(32thread_1pct_write)                131.07%   291.82ns    3.43M
// boost_shared(32thread_1pct_write)                 12.77%     2.99us  333.95K
// pthrd_rwlock(32thread_1pct_write)                173.43%   220.55ns    4.53M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_1pct_write)                          510.54ns    1.96M
// shmtx_wr_pri(64thread_1pct_write)               1378.27%    37.04ns   27.00M
// shmtx_w_bare(64thread_1pct_write)               1178.24%    43.33ns   23.08M
// shmtx_rd_pri(64thread_1pct_write)                325.29%   156.95ns    6.37M
// shmtx_r_bare(64thread_1pct_write)                247.82%   206.02ns    4.85M
// folly_ticket(64thread_1pct_write)                117.87%   433.13ns    2.31M
// boost_shared(64thread_1pct_write)                  9.45%     5.40us  185.09K
// pthrd_rwlock(64thread_1pct_write)                236.72%   215.68ns    4.64M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_50pct_write)                        10.85ns   92.15M
// shmtx_wr_pri(2thr_2lock_50pct_write)              81.73%    13.28ns   75.32M
// shmtx_rd_pri(2thr_2lock_50pct_write)              81.82%    13.26ns   75.40M
// folly_rwspin(4thr_4lock_50pct_write)                         5.29ns  188.90M
// shmtx_wr_pri(4thr_4lock_50pct_write)              80.89%     6.54ns  152.80M
// shmtx_rd_pri(4thr_4lock_50pct_write)              81.07%     6.53ns  153.14M
// folly_rwspin(8thr_8lock_50pct_write)                         2.63ns  380.57M
// shmtx_wr_pri(8thr_8lock_50pct_write)              80.56%     3.26ns  306.57M
// shmtx_rd_pri(8thr_8lock_50pct_write)              80.29%     3.27ns  305.54M
// folly_rwspin(16thr_16lock_50pct_write)                       1.31ns  764.70M
// shmtx_wr_pri(16thr_16lock_50pct_write)            79.32%     1.65ns  606.54M
// shmtx_rd_pri(16thr_16lock_50pct_write)            79.62%     1.64ns  608.84M
// folly_rwspin(32thr_32lock_50pct_write)                       1.20ns  836.75M
// shmtx_wr_pri(32thr_32lock_50pct_write)            91.67%     1.30ns  767.07M
// shmtx_rd_pri(32thr_32lock_50pct_write)            92.00%     1.30ns  769.82M
// folly_rwspin(64thr_64lock_50pct_write)                       1.39ns  717.80M
// shmtx_wr_pri(64thr_64lock_50pct_write)            93.21%     1.49ns  669.08M
// shmtx_rd_pri(64thr_64lock_50pct_write)            92.49%     1.51ns  663.89M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_10pct_write)                        10.24ns   97.70M
// shmtx_wr_pri(2thr_2lock_10pct_write)              76.46%    13.39ns   74.70M
// shmtx_rd_pri(2thr_2lock_10pct_write)              76.35%    13.41ns   74.60M
// folly_rwspin(4thr_4lock_10pct_write)                         5.02ns  199.03M
// shmtx_wr_pri(4thr_4lock_10pct_write)              75.83%     6.63ns  150.91M
// shmtx_rd_pri(4thr_4lock_10pct_write)              76.10%     6.60ns  151.46M
// folly_rwspin(8thr_8lock_10pct_write)                         2.47ns  405.50M
// shmtx_wr_pri(8thr_8lock_10pct_write)              74.54%     3.31ns  302.27M
// shmtx_rd_pri(8thr_8lock_10pct_write)              74.85%     3.29ns  303.52M
// folly_rwspin(16thr_16lock_10pct_write)                       1.22ns  818.68M
// shmtx_wr_pri(16thr_16lock_10pct_write)            73.35%     1.67ns  600.47M
// shmtx_rd_pri(16thr_16lock_10pct_write)            73.38%     1.66ns  600.73M
// folly_rwspin(32thr_32lock_10pct_write)                       1.21ns  827.95M
// shmtx_wr_pri(32thr_32lock_10pct_write)            96.13%     1.26ns  795.89M
// shmtx_rd_pri(32thr_32lock_10pct_write)            96.01%     1.26ns  794.95M
// folly_rwspin(64thr_64lock_10pct_write)                       1.40ns  716.17M
// shmtx_wr_pri(64thr_64lock_10pct_write)            96.91%     1.44ns  694.03M
// shmtx_rd_pri(64thr_64lock_10pct_write)            96.85%     1.44ns  693.64M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_1pct_write)                         10.11ns   98.91M
// shmtx_wr_pri(2thr_2lock_1pct_write)               75.07%    13.47ns   74.25M
// shmtx_rd_pri(2thr_2lock_1pct_write)               74.98%    13.48ns   74.16M
// folly_rwspin(4thr_4lock_1pct_write)                          4.96ns  201.77M
// shmtx_wr_pri(4thr_4lock_1pct_write)               74.59%     6.64ns  150.49M
// shmtx_rd_pri(4thr_4lock_1pct_write)               74.60%     6.64ns  150.51M
// folly_rwspin(8thr_8lock_1pct_write)                          2.44ns  410.42M
// shmtx_wr_pri(8thr_8lock_1pct_write)               73.68%     3.31ns  302.41M
// shmtx_rd_pri(8thr_8lock_1pct_write)               73.38%     3.32ns  301.16M
// folly_rwspin(16thr_16lock_1pct_write)                        1.21ns  827.53M
// shmtx_wr_pri(16thr_16lock_1pct_write)             72.11%     1.68ns  596.74M
// shmtx_rd_pri(16thr_16lock_1pct_write)             72.23%     1.67ns  597.73M
// folly_rwspin(32thr_32lock_1pct_write)                        1.22ns  819.53M
// shmtx_wr_pri(32thr_32lock_1pct_write)             98.17%     1.24ns  804.50M
// shmtx_rd_pri(32thr_32lock_1pct_write)             98.21%     1.24ns  804.86M
// folly_rwspin(64thr_64lock_1pct_write)                        1.41ns  710.26M
// shmtx_wr_pri(64thr_64lock_1pct_write)             97.81%     1.44ns  694.71M
// shmtx_rd_pri(64thr_64lock_1pct_write)             99.44%     1.42ns  706.28M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn0)                              641.24ns    1.56M
// shmtx_w_bare_ping_pong(burn0)                     91.07%   704.12ns    1.42M
// shmtx_r_bare_ping_pong(burn0)                     78.70%   814.84ns    1.23M
// folly_ticket_ping_pong(burn0)                     85.67%   748.53ns    1.34M
// boost_shared_ping_pong(burn0)                      5.58%    11.50us   86.96K
// pthrd_rwlock_ping_pong(burn0)                      8.81%     7.28us  137.40K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn100k)                           678.97ns    1.47M
// shmtx_w_bare_ping_pong(burn100k)                  99.73%   680.78ns    1.47M
// shmtx_r_bare_ping_pong(burn100k)                  98.67%   688.13ns    1.45M
// folly_ticket_ping_pong(burn100k)                  99.31%   683.68ns    1.46M
// boost_shared_ping_pong(burn100k)                  58.23%     1.17us  857.64K
// pthrd_rwlock_ping_pong(burn100k)                  57.43%     1.18us  845.86K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn300k)                             2.03us  492.99K
// shmtx_w_bare_ping_pong(burn300k)                  99.98%     2.03us  492.88K
// shmtx_r_bare_ping_pong(burn300k)                  99.94%     2.03us  492.68K
// folly_ticket_ping_pong(burn300k)                  99.88%     2.03us  492.40K
// boost_shared_ping_pong(burn300k)                  81.43%     2.49us  401.47K
// pthrd_rwlock_ping_pong(burn300k)                  83.22%     2.44us  410.29K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn1M)                             677.07ns    1.48M
// shmtx_w_bare_ping_pong(burn1M)                   100.50%   673.74ns    1.48M
// shmtx_r_bare_ping_pong(burn1M)                   100.14%   676.12ns    1.48M
// folly_ticket_ping_pong(burn1M)                   100.44%   674.14ns    1.48M
// boost_shared_ping_pong(burn1M)                    93.04%   727.72ns    1.37M
// pthrd_rwlock_ping_pong(burn1M)                    94.52%   716.30ns    1.40M
// ============================================================================

int main(int argc, char** argv) {
  (void)folly_rwspin_reads;
  (void)shmtx_wr_pri_reads;
  (void)shmtx_w_bare_reads;
  (void)shmtx_rd_pri_reads;
  (void)shmtx_r_bare_reads;
  (void)folly_ticket_reads;
  (void)boost_shared_reads;
  (void)pthrd_rwlock_reads;
  (void)folly_rwspin;
  (void)shmtx_wr_pri;
  (void)shmtx_w_bare;
  (void)shmtx_rd_pri;
  (void)shmtx_r_bare;
  (void)folly_ticket;
  (void)boost_shared;
  (void)pthrd_rwlock;
  (void)pthrd_mutex_;
  (void)folly_rwspin_ping_pong;
  (void)shmtx_w_bare_ping_pong;
  (void)shmtx_r_bare_ping_pong;
  (void)folly_ticket_ping_pong;
  (void)boost_shared_ping_pong;
  (void)pthrd_rwlock_ping_pong;

  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rv;
}
