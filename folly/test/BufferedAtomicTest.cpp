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

#include <folly/test/BufferedAtomic.h>

#include <random>

#include <folly/SingletonThreadLocal.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

using namespace folly::test;
using DSched = DeterministicSchedule;

template <typename T>
class RecordBufferTest : public RecordBuffer<T> {
 public:
  void assertOldestAllowed(
      size_t expected,
      std::memory_order mo,
      const ThreadTimestamps& acqRelOrder) {
    size_t oldestAllowed = RecordBuffer<T>::getOldestAllowed(mo, acqRelOrder);
    ASSERT_EQ(expected, RecordBuffer<T>::history_[oldestAllowed].val_);
  }
};

struct DSchedTimestampTest : public DSchedTimestamp {
  explicit DSchedTimestampTest(size_t v) : DSchedTimestamp(v) {}
};

TEST(BufferedAtomic, basic) {
  RecordBufferTest<int> buf;
  DSchedThreadId tid(0);
  ThreadInfo threadInfo(tid);

  ASSERT_TRUE(
      threadInfo.acqRelOrder_.atLeastAsRecentAs(tid, DSchedTimestampTest(1)));
  ASSERT_FALSE(
      threadInfo.acqRelOrder_.atLeastAsRecentAs(tid, DSchedTimestampTest(2)));

  // value stored is equal to ts at time of store
  for (int i = 2; i < 12; i++) {
    buf.store(tid, threadInfo, i, std::memory_order_relaxed);
  }

  ASSERT_TRUE(
      threadInfo.acqRelOrder_.atLeastAsRecentAs(tid, DSchedTimestampTest(11)));
  ASSERT_FALSE(
      threadInfo.acqRelOrder_.atLeastAsRecentAs(tid, DSchedTimestampTest(12)));

  ThreadTimestamps tts;
  buf.assertOldestAllowed(2, std::memory_order_relaxed, tts);

  tts.setIfNotPresent(tid, DSchedTimestampTest(8));
  buf.assertOldestAllowed(8, std::memory_order_relaxed, tts);

  tts.clear();
  tts.setIfNotPresent(tid, DSchedTimestampTest(10));
  buf.assertOldestAllowed(10, std::memory_order_relaxed, tts);

  tts.clear();
  tts.setIfNotPresent(tid, DSchedTimestampTest(115));
  buf.assertOldestAllowed(11, std::memory_order_relaxed, tts);
}

TEST(BufferedAtomic, seqCst) {
  RecordBufferTest<int> buf;
  DSchedThreadId tid(0);
  ThreadInfo threadInfo(tid);

  buf.store(tid, threadInfo, 0, std::memory_order_relaxed);
  buf.store(tid, threadInfo, 1, std::memory_order_seq_cst);
  buf.store(tid, threadInfo, 2, std::memory_order_relaxed);

  ThreadTimestamps tts;
  buf.assertOldestAllowed(0, std::memory_order_relaxed, tts);
  buf.assertOldestAllowed(0, std::memory_order_acquire, tts);
  buf.assertOldestAllowed(1, std::memory_order_seq_cst, tts);
}

TEST(BufferedAtomic, transitiveSync) {
  RecordBufferTest<int> buf;
  DSchedThreadId tid0(0);
  DSchedThreadId tid1(1);
  DSchedThreadId tid2(2);
  ThreadInfo threadInfo0(tid0);
  ThreadInfo threadInfo1(tid1);
  ThreadInfo threadInfo2(tid2);

  buf.store(tid0, threadInfo0, 0, std::memory_order_relaxed);
  buf.store(tid0, threadInfo0, 1, std::memory_order_seq_cst);

  int val = buf.load(tid1, threadInfo1, std::memory_order_seq_cst);
  ASSERT_EQ(1, val);

  buf.assertOldestAllowed(
      0, std::memory_order_relaxed, threadInfo2.acqRelOrder_);
  threadInfo2.acqRelOrder_.sync(threadInfo1.acqRelOrder_);
  buf.assertOldestAllowed(
      1, std::memory_order_relaxed, threadInfo2.acqRelOrder_);
}

TEST(BufferedAtomic, acqRel) {
  RecordBufferTest<int> buf;
  DSchedThreadId tid0(0);
  DSchedThreadId tid1(1);
  ThreadInfo threadInfo0(tid0);
  ThreadInfo threadInfo1(tid1);

  buf.store(tid0, threadInfo0, 0, std::memory_order_relaxed);
  buf.store(tid0, threadInfo0, 1, std::memory_order_release);
  while (buf.load(tid1, threadInfo1, std::memory_order_relaxed) == 0) {
  }

  ASSERT_TRUE(threadInfo1.acqFenceOrder_.atLeastAsRecentAs(
      tid0, DSchedTimestampTest(3)));
  ASSERT_FALSE(threadInfo1.acqFenceOrder_.atLeastAsRecentAs(
      tid0, DSchedTimestampTest(4)));
  ASSERT_FALSE(
      threadInfo1.acqRelOrder_.atLeastAsRecentAs(tid0, DSchedTimestampTest(1)));
}

TEST(BufferedAtomic, atomicBufferThreadCreateJoinSync) {
  for (int i = 0; i < 32; i++) {
    DSched sched(DSched::uniform(i));

    DeterministicAtomicImpl<int, DeterministicSchedule, BufferedAtomic> x;

    x.store(0, std::memory_order_relaxed);
    x.store(1, std::memory_order_relaxed);

    std::thread thread = DeterministicSchedule::thread([&]() {
      ASSERT_EQ(1, x.load(std::memory_order_relaxed));
      x.store(2, std::memory_order_relaxed);
    });
    DeterministicSchedule::join(thread);

    thread = DeterministicSchedule::thread([&]() {
      ASSERT_EQ(2, x.load(std::memory_order_relaxed));
      x.store(3, std::memory_order_relaxed);
    });
    DeterministicSchedule::join(thread);

    ASSERT_EQ(3, x.load(std::memory_order_relaxed));
  }
}

TEST(BufferedAtomic, atomicBufferFence) {
  for (int i = 0; i < 1024; i++) {
    FOLLY_TEST_DSCHED_VLOG("seed: " << i);
    DSched sched(DSched::uniform(i));

    DeterministicMutex mutex;
    mutex.lock();
    DeterministicAtomicImpl<int, DeterministicSchedule, BufferedAtomic> x;
    DeterministicAtomicImpl<int, DeterministicSchedule, BufferedAtomic> y;
    DeterministicAtomicImpl<int, DeterministicSchedule, BufferedAtomic> z;

    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    z.store(0, std::memory_order_relaxed);

    std::thread threadA = DeterministicSchedule::thread([&]() {
      x.store(1, std::memory_order_relaxed);

      DeterministicSchedule::atomic_thread_fence(std::memory_order_release);
      y.store(1, std::memory_order_relaxed);

      mutex.lock();
      ASSERT_EQ(1, z.load(std::memory_order_relaxed));
      mutex.unlock();
    });
    std::thread threadB = DeterministicSchedule::thread([&]() {
      while (y.load(std::memory_order_relaxed) != 1) {
      }
      DeterministicSchedule::atomic_thread_fence(std::memory_order_acquire);
      ASSERT_EQ(1, x.load(std::memory_order_relaxed));
    });
    DeterministicSchedule::join(threadB);
    z.store(1, std::memory_order_relaxed);
    mutex.unlock();
    DeterministicSchedule::join(threadA);
  }
}

TEST(BufferedAtomic, singleThreadUnguardedAccess) {
  DSched* sched = new DSched(DSched::uniform(0));
  DeterministicAtomicImpl<int, DeterministicSchedule, BufferedAtomic> x(0);
  delete sched;

  x.store(1);
  ASSERT_EQ(1, x.load());
}
