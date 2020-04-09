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

#include <folly/synchronization/SmallLocks.h>

#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include <folly/Random.h>
#include <folly/portability/Asm.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/PThread.h>
#include <folly/portability/Unistd.h>
#include <folly/test/TestUtils.h>

using folly::MicroLock;
using folly::MicroSpinLock;
using folly::MSLGuard;

#ifdef FOLLY_PICO_SPIN_LOCK_H_
using folly::PicoSpinLock;
#endif

DEFINE_int64(
    stress_test_seconds,
    2,
    "Number of seconds for which to run stress tests");

namespace {

struct LockedVal {
  int ar[1024];
  MicroSpinLock lock;

  LockedVal() {
    lock.init();
    memset(ar, 0, sizeof ar);
  }
};

// Compile time test for packed struct support (requires that both of
// these classes are POD).
FOLLY_PACK_PUSH
struct ignore1 {
  MicroSpinLock msl;
  int16_t foo;
} FOLLY_PACK_ATTR;
static_assert(sizeof(ignore1) == 3, "Size check failed");
static_assert(sizeof(MicroSpinLock) == 1, "Size check failed");
#ifdef FOLLY_PICO_SPIN_LOCK_H_
struct ignore2 {
  PicoSpinLock<uint32_t> psl;
  int16_t foo;
} FOLLY_PACK_ATTR;
static_assert(sizeof(ignore2) == 6, "Size check failed");
#endif
FOLLY_PACK_POP

LockedVal v;
void splock_test() {
  const int max = 1000;
  auto rng = folly::ThreadLocalPRNG();
  for (int i = 0; i < max; i++) {
    folly::asm_volatile_pause();
    MSLGuard g(v.lock);

    EXPECT_THAT(v.ar, testing::Each(testing::Eq(v.ar[0])));

    int byte = folly::Random::rand32(rng);
    memset(v.ar, char(byte), sizeof v.ar);
  }
}

#ifdef FOLLY_PICO_SPIN_LOCK_H_
template <class T>
struct PslTest {
  PicoSpinLock<T> lock;

  PslTest() {
    lock.init();
  }

  void doTest() {
    using UT = typename std::make_unsigned<T>::type;
    T ourVal = rand() % T(UT(1) << (sizeof(UT) * 8 - 1));
    for (int i = 0; i < 100; ++i) {
      std::lock_guard<PicoSpinLock<T>> guard(lock);
      lock.setData(ourVal);
      for (int n = 0; n < 10; ++n) {
        folly::asm_volatile_pause();
        EXPECT_EQ(lock.getData(), ourVal);
      }
    }
  }
};

template <class T>
void doPslTest() {
  PslTest<T> testObj;

  const int nthrs = 17;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(&PslTest<T>::doTest, &testObj));
  }
  for (auto& t : threads) {
    t.join();
  }
}
#endif

struct TestClobber {
  TestClobber() {
    lock_.init();
  }

  void go() {
    std::lock_guard<MicroSpinLock> g(lock_);
    // This bug depends on gcc register allocation and is very sensitive. We
    // have to use DCHECK instead of EXPECT_*.
    DCHECK(!lock_.try_lock());
  }

 private:
  MicroSpinLock lock_;
};

} // namespace

TEST(SmallLocks, SpinLockCorrectness) {
  EXPECT_EQ(sizeof(MicroSpinLock), 1);

  int nthrs = sysconf(_SC_NPROCESSORS_ONLN) * 2;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(splock_test));
  }
  for (auto& t : threads) {
    t.join();
  }
}

#ifdef FOLLY_PICO_SPIN_LOCK_H_
TEST(SmallLocks, PicoSpinCorrectness) {
  doPslTest<int16_t>();
  doPslTest<uint16_t>();
  doPslTest<int32_t>();
  doPslTest<uint32_t>();
  doPslTest<int64_t>();
  doPslTest<uint64_t>();
}

TEST(SmallLocks, PicoSpinSigned) {
  typedef PicoSpinLock<int16_t, 0> Lock;
  Lock val;
  val.init(-4);
  EXPECT_EQ(val.getData(), -4);

  {
    std::lock_guard<Lock> guard(val);
    EXPECT_EQ(val.getData(), -4);
    val.setData(-8);
    EXPECT_EQ(val.getData(), -8);
  }
  EXPECT_EQ(val.getData(), -8);
}

TEST(SmallLocks, PicoSpinLockThreadSanitizer) {
  SKIP_IF(!folly::kIsSanitizeThread) << "Enabled in TSAN mode only";

  typedef PicoSpinLock<int16_t, 0> Lock;

  {
    Lock a;
    Lock b;
    a.init(-8);
    b.init(-8);
    {
      std::lock_guard<Lock> ga(a);
      std::lock_guard<Lock> gb(b);
    }
    {
      std::lock_guard<Lock> gb(b);
      EXPECT_DEATH(
          [&]() { std::lock_guard<Lock> ga(a); }(),
          "Cycle in lock order graph");
    }
  }
}
#endif

TEST(SmallLocks, RegClobber) {
  TestClobber().go();
}

static_assert(sizeof(MicroLock) == 1, "Size check failed");

namespace {

struct SimpleBarrier {
  SimpleBarrier() : lock_(), cv_(), ready_(false) {}

  void wait() {
    std::unique_lock<std::mutex> lockHeld(lock_);
    while (!ready_) {
      cv_.wait(lockHeld);
    }
  }

  void run() {
    {
      std::unique_lock<std::mutex> lockHeld(lock_);
      ready_ = true;
    }

    cv_.notify_all();
  }

 private:
  std::mutex lock_;
  std::condition_variable cv_;
  bool ready_;
};
} // namespace

TEST(SmallLocks, MicroLock) {
  volatile uint64_t counters[4] = {0, 0, 0, 0};
  std::vector<std::thread> threads;
  static const unsigned nrThreads = 20;
  static const unsigned iterPerThread = 10000;
  SimpleBarrier startBarrier;

  assert(iterPerThread % 4 == 0);

  // Embed the lock in a larger structure to ensure that we do not
  // affect bits outside the ones MicroLock is defined to affect.
  struct {
    uint8_t a;
    std::atomic<uint8_t> b;
    MicroLock alock;
    std::atomic<uint8_t> d;
  } x;

  uint8_t origB = 'b';
  uint8_t origD = 'd';

  x.a = 'a';
  x.b = origB;
  x.alock.init();
  x.d = origD;

  // This thread touches other parts of the host word to show that
  // MicroLock does not interfere with memory outside of the byte
  // it owns.
  std::thread adjacentMemoryToucher = std::thread([&] {
    startBarrier.wait();
    for (unsigned iter = 0; iter < iterPerThread; ++iter) {
      if (iter % 2) {
        x.b++;
      } else {
        x.d++;
      }
    }
  });

  for (unsigned i = 0; i < nrThreads; ++i) {
    threads.emplace_back([&] {
      startBarrier.wait();
      for (unsigned iter = 0; iter < iterPerThread; ++iter) {
        unsigned slotNo = iter % 4;
        x.alock.lock(slotNo);
        counters[slotNo] += 1;
        // The occasional sleep makes it more likely that we'll
        // exercise the futex-wait path inside MicroLock.
        if (iter % 1000 == 0) {
          struct timespec ts = {0, 10000};
          (void)nanosleep(&ts, nullptr);
        }
        x.alock.unlock(slotNo);
      }
    });
  }

  startBarrier.run();

  for (auto it = threads.begin(); it != threads.end(); ++it) {
    it->join();
  }

  adjacentMemoryToucher.join();

  EXPECT_EQ(x.a, 'a');
  EXPECT_EQ(x.b, (uint8_t)(origB + iterPerThread / 2));
  EXPECT_EQ(x.d, (uint8_t)(origD + iterPerThread / 2));
  for (unsigned i = 0; i < 4; ++i) {
    EXPECT_EQ(counters[i], ((uint64_t)nrThreads * iterPerThread) / 4);
  }
}

TEST(SmallLocks, MicroLockTryLock) {
  MicroLock lock;
  lock.init();
  EXPECT_TRUE(lock.try_lock());
  EXPECT_FALSE(lock.try_lock());
  lock.unlock();
}

namespace {
template <typename Mutex, typename Duration>
void simpleStressTest(Duration duration, int numThreads) {
  auto&& mutex = Mutex{};
  auto&& data = std::atomic<std::uint64_t>{0};
  auto&& threads = std::vector<std::thread>{};
  auto&& stop = std::atomic<bool>{true};

  for (auto i = 0; i < numThreads; ++i) {
    threads.emplace_back([&mutex, &data, &stop] {
      while (!stop.load(std::memory_order_relaxed)) {
        auto lck = std::unique_lock<Mutex>{mutex};
        EXPECT_EQ(data.fetch_add(1, std::memory_order_relaxed), 0);
        EXPECT_EQ(data.fetch_sub(1, std::memory_order_relaxed), 1);
      }
    });
  }

  std::this_thread::sleep_for(duration);
  stop.store(true);
  for (auto& thread : threads) {
    thread.join();
  }
}
} // namespace

TEST(SmallLocks, MicroSpinLockStressTestLockTwoThreads) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  simpleStressTest<MicroSpinLock>(duration, 2);
}

TEST(SmallLocks, MicroSpinLockStressTestLockHardwareConcurrency) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  auto threads = std::thread::hardware_concurrency();
  simpleStressTest<MicroSpinLock>(duration, threads);
}

TEST(SmallLocks, PicoSpinLockStressTestLockTwoThreads) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  simpleStressTest<PicoSpinLock<std::uint16_t>>(duration, 2);
}

TEST(SmallLocks, PicoSpinLockStressTestLockHardwareConcurrency) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  auto threads = std::thread::hardware_concurrency();
  simpleStressTest<PicoSpinLock<std::uint16_t>>(duration, threads);
}

namespace {
template <typename Mutex>
class MutexWrapper {
 public:
  void lock() {
    while (!mutex_.try_lock()) {
    }
  }
  void unlock() {
    mutex_.unlock();
  }

  Mutex mutex_;
};

template <typename Mutex, typename Duration>
void simpleStressTestTryLock(Duration duration, int numThreads) {
  simpleStressTest<MutexWrapper<Mutex>>(duration, numThreads);
}
} // namespace

TEST(SmallLocks, MicroSpinLockStressTestTryLockTwoThreads) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  simpleStressTestTryLock<MicroSpinLock>(duration, 2);
}

TEST(SmallLocks, MicroSpinLockStressTestTryLockHardwareConcurrency) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  auto threads = std::thread::hardware_concurrency();
  simpleStressTestTryLock<MicroSpinLock>(duration, threads);
}

TEST(SmallLocksk, MicroSpinLockThreadSanitizer) {
  SKIP_IF(!folly::kIsSanitizeThread) << "Enabled in TSAN mode only";

  uint8_t val = 0;
  static_assert(sizeof(uint8_t) == sizeof(MicroSpinLock), "sanity check");
  // make sure TSAN handles this case too:
  // same lock but initialized via setting a value
  for (int i = 0; i < 10; i++) {
    val = 0;
    std::lock_guard<MicroSpinLock> g(*reinterpret_cast<MicroSpinLock*>(&val));
  }

  {
    MicroSpinLock a;
    MicroSpinLock b;
    a.init();
    b.init();
    {
      std::lock_guard<MicroSpinLock> ga(a);
      std::lock_guard<MicroSpinLock> gb(b);
    }
    {
      std::lock_guard<MicroSpinLock> gb(b);
      EXPECT_DEATH(
          [&]() { std::lock_guard<MicroSpinLock> ga(a); }(),
          "Cycle in lock order graph");
    }
  }

  {
    uint8_t a = 0;
    uint8_t b = 0;
    {
      std::lock_guard<MicroSpinLock> ga(*reinterpret_cast<MicroSpinLock*>(&a));
      std::lock_guard<MicroSpinLock> gb(*reinterpret_cast<MicroSpinLock*>(&b));
    }

    a = 0;
    b = 0;
    {
      std::lock_guard<MicroSpinLock> gb(*reinterpret_cast<MicroSpinLock*>(&b));
      EXPECT_DEATH(
          [&]() {
            std::lock_guard<MicroSpinLock> ga(
                *reinterpret_cast<MicroSpinLock*>(&a));
          }(),
          "Cycle in lock order graph");
    }
  }
}

TEST(SmallLocks, PicoSpinLockStressTestTryLockTwoThreads) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  simpleStressTestTryLock<PicoSpinLock<std::uint16_t>>(duration, 2);
}

TEST(SmallLocks, PicoSpinLockStressTestTryLockHardwareConcurrency) {
  auto duration = std::chrono::seconds{FLAGS_stress_test_seconds};
  auto threads = std::thread::hardware_concurrency();
  simpleStressTestTryLock<PicoSpinLock<std::uint16_t>>(duration, threads);
}
