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

#include <folly/experimental/SharedMutex.h>

#include <stdlib.h>
#include <thread>
#include <vector>
#include <boost/optional.hpp>
#include <folly/Benchmark.h>
#include <folly/MPMCQueue.h>
#include <folly/Random.h>
#include <folly/test/DeterministicSchedule.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <boost/thread/shared_mutex.hpp>
#include <folly/RWSpinLock.h>

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
    typename Lock::WriteHolder holder(lock);
    EXPECT_FALSE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock_shared(token));

    typename Lock::WriteHolder holder2(std::move(holder));
    typename Lock::WriteHolder holder3;
    holder3 = std::move(holder2);

    typename Lock::UpgradeHolder holder4(std::move(holder3));
    typename Lock::WriteHolder holder5(std::move(holder4));

    typename Lock::ReadHolder holder6(std::move(holder5));

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
  Lock globalLock;
  int valueProtectedByLock = 10;
  char padding2[64];
  Atom<bool> go(false);
  Atom<bool>* goPtr = &go; // workaround for clang bug
  vector<thread> threads(numThreads);

  BENCHMARK_SUSPEND {
    for (int t = 0; t < numThreads; ++t) {
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
  Lock globalLock;
  int valueProtectedByLock = 0;
  char padding2[64];
  Atom<bool> go(false);
  Atom<bool>* goPtr = &go; // workaround for clang bug
  vector<thread> threads(numThreads);

  BENCHMARK_SUSPEND {
    for (int t = 0; t < numThreads; ++t) {
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
          SharedMutexToken token;
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
    for (int t = 0; t < numThreads; ++t) {
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
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexReadPriority,
                        Locker>(1000, 3, false);
  }
}

TEST(SharedMutex, deterministic_concurrent_readers_of_one_lock_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    // LOG(INFO) << "pass " << pass;
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
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runContendedReaders<DeterministicAtomic,
                        DSharedMutexReadPriority,
                        Locker>(1000, 3, true);
  }
}

TEST(SharedMutex, deterministic_readers_of_concurrent_locks_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    // LOG(INFO) << "pass " << pass;
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
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexReadPriority, Locker>(
        1000, 3, 0.1, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_read_write_prio) {
  for (int pass = 0; pass < 3; ++pass) {
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexWritePriority, Locker>(
        1000, 3, 0.1, false);
  }
}

TEST(SharedMutex, mixed_mostly_read_read_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexReadPriority, TokenLocker>(
        50000, 32, 0.1, false);
  }
}

TEST(SharedMutex, mixed_mostly_read_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    runMixed<atomic, SharedMutexWritePriority, TokenLocker>(
        50000, 32, 0.1, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_write_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexReadPriority, TokenLocker>(
        1000, 10, 0.9, false);
  }
}

TEST(SharedMutex, deterministic_mixed_mostly_write_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runMixed<DeterministicAtomic, DSharedMutexWritePriority, TokenLocker>(
        1000, 10, 0.9, false);
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
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runAllAndValidate<DSharedMutexReadPriority, DeterministicAtomic>(1000, 8);
  }
}

TEST(SharedMutex, deterministic_all_ops_write_prio) {
  for (int pass = 0; pass < 5; ++pass) {
    // LOG(INFO) << "pass " << pass;
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
    for (int t = 0; t < threads.size(); ++t) {
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
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runRemoteUnlock<DSharedMutexWritePriority, DeterministicAtomic>(
        500, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, deterministic_remote_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    // LOG(INFO) << "pass " << pass;
    DSched sched(DSched::uniform(pass));
    runRemoteUnlock<DSharedMutexReadPriority, DeterministicAtomic>(
        500, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, remote_write_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    // LOG(INFO) << "pass " << pass;
    runRemoteUnlock<SharedMutexWritePriority, atomic>(100000, 0.1, 0.1, 5, 5);
  }
}

TEST(SharedMutex, remote_read_prio) {
  for (int pass = 0; pass < 1; ++pass) {
    // LOG(INFO) << "pass " << pass;
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
  pair<Lock, char[56]> locks[3];
  char padding2[56];

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
      for (int i = 0; i < numRounds; ++i) {
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
      for (int i = 0; i < numRounds; ++i) {
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
//   sudo nice -n -20 \
//     shared_mutex_test --benchmark --bm_min_iters=1000000
//
// Comparison use folly::RWSpinLock as the baseline, with the
// following row being the default SharedMutex (using *Holder or
// Token-ful methods).
// ============================================================================
// folly/experimental/test/SharedMutexTest.cpp     relative  time/iter  iters/s
// ============================================================================
// single_thread_lock_shared_unlock_shared                     23.01ns   43.47M
// single_thread_lock_unlock                                   25.42ns   39.34M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin_reads(1thread)                                 15.13ns   66.10M
// shmtx_wr_pri_reads(1thread)                       73.76%    20.51ns   48.75M
// shmtx_w_bare_reads(1thread)                       59.49%    25.43ns   39.32M
// shmtx_rd_pri_reads(1thread)                       72.60%    20.84ns   47.99M
// shmtx_r_bare_reads(1thread)                       59.62%    25.37ns   39.41M
// folly_ticket_reads(1thread)                       55.40%    27.31ns   36.62M
// boost_shared_reads(1thread)                       10.88%   139.01ns    7.19M
// pthrd_rwlock_reads(1thread)                       40.70%    37.17ns   26.90M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(2thread)                                 47.51ns   21.05M
// shmtx_wr_pri_reads(2thread)                      237.28%    20.02ns   49.94M
// shmtx_w_bare_reads(2thread)                      222.10%    21.39ns   46.74M
// shmtx_rd_pri_reads(2thread)                      251.68%    18.88ns   52.97M
// shmtx_r_bare_reads(2thread)                      222.29%    21.37ns   46.78M
// folly_ticket_reads(2thread)                       55.00%    86.39ns   11.58M
// boost_shared_reads(2thread)                       22.86%   207.81ns    4.81M
// pthrd_rwlock_reads(2thread)                       61.36%    77.43ns   12.92M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(4thread)                                 69.29ns   14.43M
// shmtx_wr_pri_reads(4thread)                      694.46%     9.98ns  100.23M
// shmtx_w_bare_reads(4thread)                      650.25%    10.66ns   93.85M
// shmtx_rd_pri_reads(4thread)                      738.08%     9.39ns  106.53M
// shmtx_r_bare_reads(4thread)                      650.71%    10.65ns   93.92M
// folly_ticket_reads(4thread)                       63.86%   108.49ns    9.22M
// boost_shared_reads(4thread)                       19.53%   354.79ns    2.82M
// pthrd_rwlock_reads(4thread)                       33.86%   204.61ns    4.89M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(8thread)                                 75.34ns   13.27M
// shmtx_wr_pri_reads(8thread)                     1500.46%     5.02ns  199.16M
// shmtx_w_bare_reads(8thread)                     1397.84%     5.39ns  185.54M
// shmtx_rd_pri_reads(8thread)                     1589.99%     4.74ns  211.05M
// shmtx_r_bare_reads(8thread)                     1398.83%     5.39ns  185.67M
// folly_ticket_reads(8thread)                       53.26%   141.45ns    7.07M
// boost_shared_reads(8thread)                       26.24%   287.11ns    3.48M
// pthrd_rwlock_reads(8thread)                       43.40%   173.57ns    5.76M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(16thread)                                80.81ns   12.38M
// shmtx_wr_pri_reads(16thread)                    3119.49%     2.59ns  386.05M
// shmtx_w_bare_reads(16thread)                    2916.06%     2.77ns  360.87M
// shmtx_rd_pri_reads(16thread)                    3330.06%     2.43ns  412.11M
// shmtx_r_bare_reads(16thread)                    2909.05%     2.78ns  360.01M
// folly_ticket_reads(16thread)                      44.59%   181.21ns    5.52M
// boost_shared_reads(16thread)                      29.56%   273.40ns    3.66M
// pthrd_rwlock_reads(16thread)                      48.39%   166.99ns    5.99M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(32thread)                                73.29ns   13.64M
// shmtx_wr_pri_reads(32thread)                    4417.58%     1.66ns  602.77M
// shmtx_w_bare_reads(32thread)                    4463.71%     1.64ns  609.06M
// shmtx_rd_pri_reads(32thread)                    4777.84%     1.53ns  651.92M
// shmtx_r_bare_reads(32thread)                    4312.45%     1.70ns  588.42M
// folly_ticket_reads(32thread)                      25.56%   286.75ns    3.49M
// boost_shared_reads(32thread)                      22.08%   331.86ns    3.01M
// pthrd_rwlock_reads(32thread)                      46.72%   156.87ns    6.37M
// ----------------------------------------------------------------------------
// folly_rwspin_reads(64thread)                                74.92ns   13.35M
// shmtx_wr_pri_reads(64thread)                    4171.71%     1.80ns  556.83M
// shmtx_w_bare_reads(64thread)                    3973.49%     1.89ns  530.37M
// shmtx_rd_pri_reads(64thread)                    4404.73%     1.70ns  587.94M
// shmtx_r_bare_reads(64thread)                    3985.48%     1.88ns  531.98M
// folly_ticket_reads(64thread)                      26.07%   287.39ns    3.48M
// boost_shared_reads(64thread)                      23.59%   317.64ns    3.15M
// pthrd_rwlock_reads(64thread)                      49.54%   151.24ns    6.61M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_all_write)                             25.29ns   39.53M
// shmtx_wr_pri(1thread_all_write)                   96.76%    26.14ns   38.25M
// shmtx_rd_pri(1thread_all_write)                   96.60%    26.18ns   38.19M
// folly_ticket(1thread_all_write)                   89.58%    28.24ns   35.42M
// boost_shared(1thread_all_write)                   17.06%   148.29ns    6.74M
// pthrd_rwlock(1thread_all_write)                   63.32%    39.95ns   25.03M
// pthrd_mutex_(1thread_all_write)                   81.38%    31.08ns   32.17M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_all_write)                            104.60ns    9.56M
// shmtx_wr_pri(2thread_all_write)                   48.87%   214.06ns    4.67M
// shmtx_rd_pri(2thread_all_write)                   42.47%   246.31ns    4.06M
// folly_ticket(2thread_all_write)                   73.12%   143.05ns    6.99M
// boost_shared(2thread_all_write)                   24.59%   425.41ns    2.35M
// pthrd_rwlock(2thread_all_write)                   38.69%   270.37ns    3.70M
// pthrd_mutex_(2thread_all_write)                  155.45%    67.29ns   14.86M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_all_write)                            166.17ns    6.02M
// shmtx_wr_pri(4thread_all_write)                   45.40%   366.00ns    2.73M
// shmtx_rd_pri(4thread_all_write)                   62.81%   264.56ns    3.78M
// folly_ticket(4thread_all_write)                  118.11%   140.69ns    7.11M
// boost_shared(4thread_all_write)                    8.78%     1.89us  528.22K
// pthrd_rwlock(4thread_all_write)                   27.30%   608.59ns    1.64M
// pthrd_mutex_(4thread_all_write)                   92.18%   180.27ns    5.55M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_all_write)                            363.10ns    2.75M
// shmtx_wr_pri(8thread_all_write)                  163.18%   222.51ns    4.49M
// shmtx_rd_pri(8thread_all_write)                   91.20%   398.11ns    2.51M
// folly_ticket(8thread_all_write)                  150.11%   241.89ns    4.13M
// boost_shared(8thread_all_write)                    7.53%     4.82us  207.48K
// pthrd_rwlock(8thread_all_write)                   57.06%   636.32ns    1.57M
// pthrd_mutex_(8thread_all_write)                  218.78%   165.96ns    6.03M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_all_write)                           762.75ns    1.31M
// shmtx_wr_pri(16thread_all_write)                 131.04%   582.08ns    1.72M
// shmtx_rd_pri(16thread_all_write)                 130.26%   585.57ns    1.71M
// folly_ticket(16thread_all_write)                 253.39%   301.01ns    3.32M
// boost_shared(16thread_all_write)                  10.33%     7.38us  135.43K
// pthrd_rwlock(16thread_all_write)                 141.66%   538.43ns    1.86M
// pthrd_mutex_(16thread_all_write)                 471.34%   161.83ns    6.18M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_all_write)                             1.42us  705.40K
// shmtx_wr_pri(32thread_all_write)                 229.36%   618.09ns    1.62M
// shmtx_rd_pri(32thread_all_write)                 228.78%   619.65ns    1.61M
// folly_ticket(32thread_all_write)                 326.61%   434.04ns    2.30M
// boost_shared(32thread_all_write)                  18.65%     7.60us  131.59K
// pthrd_rwlock(32thread_all_write)                 261.56%   542.00ns    1.85M
// pthrd_mutex_(32thread_all_write)                 946.65%   149.75ns    6.68M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_all_write)                             1.83us  545.94K
// shmtx_wr_pri(64thread_all_write)                 248.08%   738.34ns    1.35M
// shmtx_rd_pri(64thread_all_write)                 249.47%   734.23ns    1.36M
// folly_ticket(64thread_all_write)                 342.38%   535.00ns    1.87M
// boost_shared(64thread_all_write)                  23.95%     7.65us  130.75K
// pthrd_rwlock(64thread_all_write)                 318.32%   575.42ns    1.74M
// pthrd_mutex_(64thread_all_write)                1288.43%   142.16ns    7.03M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_10pct_write)                           19.13ns   52.28M
// shmtx_wr_pri(1thread_10pct_write)                 80.47%    23.77ns   42.07M
// shmtx_rd_pri(1thread_10pct_write)                 80.63%    23.72ns   42.15M
// folly_ticket(1thread_10pct_write)                 69.33%    27.59ns   36.25M
// boost_shared(1thread_10pct_write)                 12.46%   153.53ns    6.51M
// pthrd_rwlock(1thread_10pct_write)                 46.35%    41.27ns   24.23M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_10pct_write)                          142.93ns    7.00M
// shmtx_wr_pri(2thread_10pct_write)                165.37%    86.43ns   11.57M
// shmtx_rd_pri(2thread_10pct_write)                159.35%    89.70ns   11.15M
// folly_ticket(2thread_10pct_write)                129.31%   110.53ns    9.05M
// boost_shared(2thread_10pct_write)                 39.42%   362.54ns    2.76M
// pthrd_rwlock(2thread_10pct_write)                 87.87%   162.65ns    6.15M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_10pct_write)                          197.39ns    5.07M
// shmtx_wr_pri(4thread_10pct_write)                171.06%   115.39ns    8.67M
// shmtx_rd_pri(4thread_10pct_write)                139.86%   141.13ns    7.09M
// folly_ticket(4thread_10pct_write)                129.34%   152.62ns    6.55M
// boost_shared(4thread_10pct_write)                 16.99%     1.16us  860.70K
// pthrd_rwlock(4thread_10pct_write)                 47.65%   414.28ns    2.41M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_10pct_write)                          392.62ns    2.55M
// shmtx_wr_pri(8thread_10pct_write)                273.40%   143.61ns    6.96M
// shmtx_rd_pri(8thread_10pct_write)                194.52%   201.84ns    4.95M
// folly_ticket(8thread_10pct_write)                189.91%   206.75ns    4.84M
// boost_shared(8thread_10pct_write)                 16.84%     2.33us  429.03K
// pthrd_rwlock(8thread_10pct_write)                 87.03%   451.14ns    2.22M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_10pct_write)                         794.93ns    1.26M
// shmtx_wr_pri(16thread_10pct_write)               352.64%   225.43ns    4.44M
// shmtx_rd_pri(16thread_10pct_write)               295.42%   269.09ns    3.72M
// folly_ticket(16thread_10pct_write)               296.11%   268.46ns    3.72M
// boost_shared(16thread_10pct_write)                17.04%     4.66us  214.39K
// pthrd_rwlock(16thread_10pct_write)               176.40%   450.64ns    2.22M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_10pct_write)                         821.14ns    1.22M
// shmtx_wr_pri(32thread_10pct_write)               355.74%   230.82ns    4.33M
// shmtx_rd_pri(32thread_10pct_write)               320.09%   256.53ns    3.90M
// folly_ticket(32thread_10pct_write)               262.01%   313.41ns    3.19M
// boost_shared(32thread_10pct_write)                 8.15%    10.08us   99.20K
// pthrd_rwlock(32thread_10pct_write)               175.15%   468.83ns    2.13M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_10pct_write)                           1.20us  836.33K
// shmtx_wr_pri(64thread_10pct_write)               437.20%   273.49ns    3.66M
// shmtx_rd_pri(64thread_10pct_write)               438.80%   272.49ns    3.67M
// folly_ticket(64thread_10pct_write)               254.51%   469.82ns    2.13M
// boost_shared(64thread_10pct_write)                 6.05%    19.78us   50.56K
// pthrd_rwlock(64thread_10pct_write)               254.24%   470.30ns    2.13M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin(1thread_1pct_write)                            18.60ns   53.76M
// shmtx_wr_pri(1thread_1pct_write)                  79.07%    23.52ns   42.51M
// shmtx_w_bare(1thread_1pct_write)                  66.09%    28.15ns   35.53M
// shmtx_rd_pri(1thread_1pct_write)                  79.21%    23.48ns   42.58M
// shmtx_r_bare(1thread_1pct_write)                  65.98%    28.19ns   35.47M
// folly_ticket(1thread_1pct_write)                  67.69%    27.48ns   36.39M
// boost_shared(1thread_1pct_write)                  12.17%   152.88ns    6.54M
// pthrd_rwlock(1thread_1pct_write)                  45.04%    41.30ns   24.22M
// ----------------------------------------------------------------------------
// folly_rwspin(2thread_1pct_write)                           128.42ns    7.79M
// shmtx_wr_pri(2thread_1pct_write)                 347.63%    36.94ns   27.07M
// shmtx_w_bare(2thread_1pct_write)                 475.37%    27.02ns   37.02M
// shmtx_rd_pri(2thread_1pct_write)                 312.94%    41.04ns   24.37M
// shmtx_r_bare(2thread_1pct_write)                 149.38%    85.97ns   11.63M
// folly_ticket(2thread_1pct_write)                 147.88%    86.84ns   11.52M
// boost_shared(2thread_1pct_write)                  45.50%   282.24ns    3.54M
// pthrd_rwlock(2thread_1pct_write)                 129.88%    98.88ns   10.11M
// ----------------------------------------------------------------------------
// folly_rwspin(4thread_1pct_write)                           148.88ns    6.72M
// shmtx_wr_pri(4thread_1pct_write)                 504.03%    29.54ns   33.86M
// shmtx_w_bare(4thread_1pct_write)                 471.63%    31.57ns   31.68M
// shmtx_rd_pri(4thread_1pct_write)                 291.84%    51.01ns   19.60M
// shmtx_r_bare(4thread_1pct_write)                  81.41%   182.86ns    5.47M
// folly_ticket(4thread_1pct_write)                 114.59%   129.92ns    7.70M
// boost_shared(4thread_1pct_write)                  26.70%   557.56ns    1.79M
// pthrd_rwlock(4thread_1pct_write)                  64.46%   230.97ns    4.33M
// ----------------------------------------------------------------------------
// folly_rwspin(8thread_1pct_write)                           213.06ns    4.69M
// shmtx_wr_pri(8thread_1pct_write)                 734.88%    28.99ns   34.49M
// shmtx_w_bare(8thread_1pct_write)                 676.88%    31.48ns   31.77M
// shmtx_rd_pri(8thread_1pct_write)                 196.93%   108.19ns    9.24M
// shmtx_r_bare(8thread_1pct_write)                  99.35%   214.46ns    4.66M
// folly_ticket(8thread_1pct_write)                 120.84%   176.31ns    5.67M
// boost_shared(8thread_1pct_write)                  28.51%   747.36ns    1.34M
// pthrd_rwlock(8thread_1pct_write)                  88.85%   239.81ns    4.17M
// ----------------------------------------------------------------------------
// folly_rwspin(16thread_1pct_write)                          481.61ns    2.08M
// shmtx_wr_pri(16thread_1pct_write)               1204.17%    40.00ns   25.00M
// shmtx_w_bare(16thread_1pct_write)               1241.61%    38.79ns   25.78M
// shmtx_rd_pri(16thread_1pct_write)                315.61%   152.60ns    6.55M
// shmtx_r_bare(16thread_1pct_write)                211.23%   228.00ns    4.39M
// folly_ticket(16thread_1pct_write)                227.88%   211.35ns    4.73M
// boost_shared(16thread_1pct_write)                 34.17%     1.41us  709.47K
// pthrd_rwlock(16thread_1pct_write)                210.97%   228.28ns    4.38M
// ----------------------------------------------------------------------------
// folly_rwspin(32thread_1pct_write)                          382.40ns    2.62M
// shmtx_wr_pri(32thread_1pct_write)                984.99%    38.82ns   25.76M
// shmtx_w_bare(32thread_1pct_write)                957.41%    39.94ns   25.04M
// shmtx_rd_pri(32thread_1pct_write)                248.87%   153.65ns    6.51M
// shmtx_r_bare(32thread_1pct_write)                175.33%   218.11ns    4.58M
// folly_ticket(32thread_1pct_write)                140.50%   272.18ns    3.67M
// boost_shared(32thread_1pct_write)                 12.67%     3.02us  331.22K
// pthrd_rwlock(32thread_1pct_write)                172.70%   221.42ns    4.52M
// ----------------------------------------------------------------------------
// folly_rwspin(64thread_1pct_write)                          448.64ns    2.23M
// shmtx_wr_pri(64thread_1pct_write)               1136.53%    39.47ns   25.33M
// shmtx_w_bare(64thread_1pct_write)               1037.84%    43.23ns   23.13M
// shmtx_rd_pri(64thread_1pct_write)                284.52%   157.68ns    6.34M
// shmtx_r_bare(64thread_1pct_write)                216.51%   207.21ns    4.83M
// folly_ticket(64thread_1pct_write)                114.00%   393.54ns    2.54M
// boost_shared(64thread_1pct_write)                  8.29%     5.41us  184.85K
// pthrd_rwlock(64thread_1pct_write)                207.19%   216.53ns    4.62M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_50pct_write)                        10.84ns   92.23M
// shmtx_wr_pri(2thr_2lock_50pct_write)              85.21%    12.72ns   78.59M
// shmtx_rd_pri(2thr_2lock_50pct_write)              84.80%    12.79ns   78.21M
// folly_rwspin(4thr_4lock_50pct_write)                         5.33ns  187.76M
// shmtx_wr_pri(4thr_4lock_50pct_write)              84.84%     6.28ns  159.30M
// shmtx_rd_pri(4thr_4lock_50pct_write)              84.38%     6.31ns  158.42M
// folly_rwspin(8thr_8lock_50pct_write)                         2.63ns  379.54M
// shmtx_wr_pri(8thr_8lock_50pct_write)              84.30%     3.13ns  319.97M
// shmtx_rd_pri(8thr_8lock_50pct_write)              84.35%     3.12ns  320.16M
// folly_rwspin(16thr_16lock_50pct_write)                       1.31ns  760.73M
// shmtx_wr_pri(16thr_16lock_50pct_write)            83.58%     1.57ns  635.80M
// shmtx_rd_pri(16thr_16lock_50pct_write)            83.72%     1.57ns  636.89M
// folly_rwspin(32thr_32lock_50pct_write)                       1.19ns  838.77M
// shmtx_wr_pri(32thr_32lock_50pct_write)            89.84%     1.33ns  753.55M
// shmtx_rd_pri(32thr_32lock_50pct_write)            89.39%     1.33ns  749.82M
// folly_rwspin(64thr_64lock_50pct_write)                       1.39ns  718.11M
// shmtx_wr_pri(64thr_64lock_50pct_write)            91.89%     1.52ns  659.90M
// shmtx_rd_pri(64thr_64lock_50pct_write)            91.08%     1.53ns  654.04M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_10pct_write)                        10.25ns   97.53M
// shmtx_wr_pri(2thr_2lock_10pct_write)              84.23%    12.17ns   82.14M
// shmtx_rd_pri(2thr_2lock_10pct_write)              84.03%    12.20ns   81.96M
// folly_rwspin(4thr_4lock_10pct_write)                         5.05ns  197.98M
// shmtx_wr_pri(4thr_4lock_10pct_write)              84.01%     6.01ns  166.31M
// shmtx_rd_pri(4thr_4lock_10pct_write)              83.98%     6.01ns  166.27M
// folly_rwspin(8thr_8lock_10pct_write)                         2.46ns  405.97M
// shmtx_wr_pri(8thr_8lock_10pct_write)              82.52%     2.98ns  335.03M
// shmtx_rd_pri(8thr_8lock_10pct_write)              82.47%     2.99ns  334.82M
// folly_rwspin(16thr_16lock_10pct_write)                       1.23ns  813.48M
// shmtx_wr_pri(16thr_16lock_10pct_write)            82.08%     1.50ns  667.72M
// shmtx_rd_pri(16thr_16lock_10pct_write)            81.53%     1.51ns  663.23M
// folly_rwspin(32thr_32lock_10pct_write)                       1.20ns  836.43M
// shmtx_wr_pri(32thr_32lock_10pct_write)            91.52%     1.31ns  765.47M
// shmtx_rd_pri(32thr_32lock_10pct_write)            91.87%     1.30ns  768.45M
// folly_rwspin(64thr_64lock_10pct_write)                       1.39ns  721.74M
// shmtx_wr_pri(64thr_64lock_10pct_write)            92.04%     1.51ns  664.28M
// shmtx_rd_pri(64thr_64lock_10pct_write)            92.57%     1.50ns  668.15M
// ----------------------------------------------------------------------------
// folly_rwspin(2thr_2lock_1pct_write)                         10.13ns   98.71M
// shmtx_wr_pri(2thr_2lock_1pct_write)               83.59%    12.12ns   82.51M
// shmtx_rd_pri(2thr_2lock_1pct_write)               83.59%    12.12ns   82.51M
// folly_rwspin(4thr_4lock_1pct_write)                          4.96ns  201.67M
// shmtx_wr_pri(4thr_4lock_1pct_write)               82.87%     5.98ns  167.13M
// shmtx_rd_pri(4thr_4lock_1pct_write)               83.05%     5.97ns  167.48M
// folly_rwspin(8thr_8lock_1pct_write)                          2.44ns  409.64M
// shmtx_wr_pri(8thr_8lock_1pct_write)               82.46%     2.96ns  337.79M
// shmtx_rd_pri(8thr_8lock_1pct_write)               82.40%     2.96ns  337.55M
// folly_rwspin(16thr_16lock_1pct_write)                        1.22ns  821.15M
// shmtx_wr_pri(16thr_16lock_1pct_write)             81.63%     1.49ns  670.29M
// shmtx_rd_pri(16thr_16lock_1pct_write)             81.65%     1.49ns  670.50M
// folly_rwspin(32thr_32lock_1pct_write)                        1.20ns  832.88M
// shmtx_wr_pri(32thr_32lock_1pct_write)             92.22%     1.30ns  768.06M
// shmtx_rd_pri(32thr_32lock_1pct_write)             92.21%     1.30ns  768.01M
// folly_rwspin(64thr_64lock_1pct_write)                        1.38ns  726.10M
// shmtx_wr_pri(64thr_64lock_1pct_write)             92.24%     1.49ns  669.75M
// shmtx_rd_pri(64thr_64lock_1pct_write)             92.13%     1.49ns  668.95M
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn0)                              503.21ns    1.99M
// shmtx_w_bare_ping_pong(burn0)                     79.13%   635.96ns    1.57M
// shmtx_r_bare_ping_pong(burn0)                     59.08%   851.81ns    1.17M
// folly_ticket_ping_pong(burn0)                     60.50%   831.77ns    1.20M
// boost_shared_ping_pong(burn0)                      4.46%    11.28us   88.65K
// pthrd_rwlock_ping_pong(burn0)                      6.86%     7.34us  136.27K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn100k)                           685.00ns    1.46M
// shmtx_w_bare_ping_pong(burn100k)                 100.05%   684.65ns    1.46M
// shmtx_r_bare_ping_pong(burn100k)                  99.93%   685.51ns    1.46M
// folly_ticket_ping_pong(burn100k)                  99.32%   689.72ns    1.45M
// boost_shared_ping_pong(burn100k)                  56.59%     1.21us  826.06K
// pthrd_rwlock_ping_pong(burn100k)                  58.32%     1.17us  851.41K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn300k)                             2.15us  464.20K
// shmtx_w_bare_ping_pong(burn300k)                 101.02%     2.13us  468.93K
// shmtx_r_bare_ping_pong(burn300k)                 103.95%     2.07us  482.55K
// folly_ticket_ping_pong(burn300k)                 104.06%     2.07us  483.05K
// boost_shared_ping_pong(burn300k)                  86.36%     2.49us  400.86K
// pthrd_rwlock_ping_pong(burn300k)                  87.30%     2.47us  405.25K
// ----------------------------------------------------------------------------
// folly_rwspin_ping_pong(burn1M)                             675.20ns    1.48M
// shmtx_w_bare_ping_pong(burn1M)                    99.73%   677.02ns    1.48M
// shmtx_r_bare_ping_pong(burn1M)                    99.23%   680.45ns    1.47M
// folly_ticket_ping_pong(burn1M)                    97.85%   690.01ns    1.45M
// boost_shared_ping_pong(burn1M)                    93.17%   724.67ns    1.38M
// pthrd_rwlock_ping_pong(burn1M)                    91.84%   735.22ns    1.36M
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
