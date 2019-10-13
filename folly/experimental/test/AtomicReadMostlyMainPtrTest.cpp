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

#include <folly/experimental/AtomicReadMostlyMainPtr.h>

#include <array>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <folly/portability/GTest.h>

using folly::AtomicReadMostlyMainPtr;
using folly::ReadMostlySharedPtr;

class AtomicReadMostlyMainPtrSimpleTest : public testing::Test {
 protected:
  AtomicReadMostlyMainPtrSimpleTest()
      : sharedPtr123(std::make_shared<int>(123)),
        sharedPtr123Dup(sharedPtr123),
        sharedPtr456(std::make_shared<int>(456)),
        sharedPtr456Dup(sharedPtr456) {}

  AtomicReadMostlyMainPtr<int> data;
  ReadMostlySharedPtr<int> readMostlySharedPtr;
  std::shared_ptr<int> nullSharedPtr;
  std::shared_ptr<int> sharedPtr123;
  std::shared_ptr<int> sharedPtr123Dup;

  std::shared_ptr<int> sharedPtr456;
  std::shared_ptr<int> sharedPtr456Dup;

  enum CasType {
    kWeakCas,
    kStrongCas,
  };

  template <typename T0, typename T1, typename T2>
  bool cas(T0&& atom, T1&& expected, T2&& desired, CasType casType) {
    if (casType == kWeakCas) {
      return std::forward<T0>(atom).compare_exchange_weak(
          std::forward<T1>(expected), std::forward<T2>(desired));
    } else {
      return std::forward<T0>(atom).compare_exchange_strong(
          std::forward<T1>(expected), std::forward<T2>(desired));
    }
  }

  void casSuccessTest(CasType casType) {
    // Nominally, the weak compare exchange allows spurious failures, but we
    // peek inside the implementation a little to realize that it doesn't.

    // Null -> non-null
    EXPECT_TRUE(cas(data, nullSharedPtr, sharedPtr123, casType));
    EXPECT_EQ(sharedPtr123.get(), data.load().get());
    EXPECT_EQ(nullptr, nullSharedPtr);
    EXPECT_EQ(sharedPtr123, sharedPtr123Dup);

    // Non-null -> non-null
    EXPECT_TRUE(cas(data, sharedPtr123, sharedPtr456, casType));
    EXPECT_EQ(sharedPtr456.get(), data.load().get());
    EXPECT_EQ(sharedPtr123, sharedPtr123Dup);
    EXPECT_EQ(sharedPtr456, sharedPtr456Dup);

    // Non-null -> null
    EXPECT_TRUE(cas(data, sharedPtr456, nullSharedPtr, casType));
    EXPECT_EQ(nullptr, data.load().get());
    EXPECT_EQ(sharedPtr123, sharedPtr123Dup);
    EXPECT_EQ(nullSharedPtr, nullptr);
  }

  void casFailureTest(CasType casType) {
    std::shared_ptr<int> expected;

    // Null -> non-null
    expected = std::make_shared<int>(789);
    EXPECT_FALSE(cas(data, expected, sharedPtr123, casType));
    EXPECT_EQ(nullptr, data.load().get());
    EXPECT_EQ(nullptr, expected);
    EXPECT_EQ(sharedPtr123, sharedPtr123Dup);

    // Non-null -> non-null
    expected = std::make_shared<int>(789);
    data.store(sharedPtr123);
    EXPECT_FALSE(cas(data, expected, sharedPtr456, casType));
    EXPECT_EQ(sharedPtr123.get(), data.load().get());
    EXPECT_EQ(sharedPtr123, expected);
    EXPECT_EQ(sharedPtr456, sharedPtr456Dup);

    // Non-null -> null
    expected = std::make_shared<int>(789);
    data.store(sharedPtr123);
    EXPECT_FALSE(cas(data, expected, nullSharedPtr, casType));
    EXPECT_EQ(sharedPtr123.get(), data.load().get());
    EXPECT_EQ(sharedPtr123, expected);
    EXPECT_EQ(nullptr, nullSharedPtr);
  }
};

TEST_F(AtomicReadMostlyMainPtrSimpleTest, StartsNull) {
  EXPECT_EQ(data.load(), nullptr);
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, Store) {
  data.store(sharedPtr123);
  EXPECT_EQ(sharedPtr123.get(), data.load().get());
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, Exchange) {
  data.store(sharedPtr123);
  auto prev = data.exchange(sharedPtr456);
  EXPECT_EQ(sharedPtr123, prev);
  EXPECT_EQ(sharedPtr456.get(), data.load().get());
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, CompareExchangeWeakSuccess) {
  casSuccessTest(kWeakCas);
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, CompareExchangeStrongSuccess) {
  casSuccessTest(kStrongCas);
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, CompareExchangeWeakFailure) {
  casFailureTest(kWeakCas);
}

TEST_F(AtomicReadMostlyMainPtrSimpleTest, CompareExchangeStrongFailure) {
  casFailureTest(kStrongCas);
}

class AtomicReadMostlyMainPtrCounterTest : public testing::Test {
 protected:
  struct InstanceCounter {
    explicit InstanceCounter(int* counter_) : counter(counter_) {
      ++*counter;
    }

    ~InstanceCounter() {
      --*counter;
    }

    int* counter;
  };

  AtomicReadMostlyMainPtr<InstanceCounter> data;
  int counter1 = 0;
  int counter2 = 0;
};

TEST_F(AtomicReadMostlyMainPtrCounterTest, DestroysOldValuesSimple) {
  data.store(std::make_shared<InstanceCounter>(&counter1));
  EXPECT_EQ(1, counter1);
  data.store(std::make_shared<InstanceCounter>(&counter2));
  EXPECT_EQ(0, counter1);
  EXPECT_EQ(1, counter2);
}

TEST_F(AtomicReadMostlyMainPtrCounterTest, DestroysOldValuesWithReuse) {
  for (int i = 0; i < 100; ++i) {
    data.store(std::make_shared<InstanceCounter>(&counter1));
    EXPECT_EQ(1, counter1);
    EXPECT_EQ(0, counter2);
    data.store(std::make_shared<InstanceCounter>(&counter2));
    EXPECT_EQ(0, counter1);
    EXPECT_EQ(1, counter2);
  }
}

TEST(AtomicReadMostlyMainPtrTest, HandlesDestructionModifications) {
  struct RunOnDestruction {
    explicit RunOnDestruction(std::function<void()> func) : func_(func) {}

    ~RunOnDestruction() {
      func_();
    }
    std::function<void()> func_;
  };

  AtomicReadMostlyMainPtr<RunOnDestruction> data;

  // All the ways to trigger the AtomicReadMostlyMainPtr's "destroy the
  // pointed-to object" paths.
  std::vector<std::function<void()>> outerAccesses = {
      [&]() { data.store(nullptr); },
      [&]() { data.exchange(nullptr); },
      [&]() {
        auto old = data.load().getStdShared();
        EXPECT_TRUE(data.compare_exchange_strong(old, nullptr));
      },
      // We don't test CAS failure; unlike other accesses, it doesn't destroy
      // the object pointed to by the AtomicReadMostlyMainPtr, it destroys an
      // object pointed to by an external shared_ptr. (We'll test that later
      // on).
  };

  // All the ways the destructor might access the AtomicReadMostlyMainPtr.
  std::vector<std::function<void()>> innerAccesses = {
      [&]() { data.load(); },
      [&]() { data.store(std::make_shared<RunOnDestruction>([] {})); },
      [&]() { data.exchange(std::make_shared<RunOnDestruction>([] {})); },
      [&]() {
        auto expected = data.load().getStdShared();
        EXPECT_TRUE(data.compare_exchange_strong(
            expected, std::make_shared<RunOnDestruction>([] {})));
      },
      [&]() {
        auto notExpected = std::make_shared<RunOnDestruction>([] {});
        EXPECT_FALSE(data.compare_exchange_strong(
            notExpected, std::make_shared<RunOnDestruction>([] {})));
      },
  };

  for (auto& outerAccess : outerAccesses) {
    for (auto& innerAccess : innerAccesses) {
      data.store(std::make_shared<RunOnDestruction>(innerAccess));
      outerAccess();
    }
  }

  // The case we left out is when the destroyed object is actually from the CAS
  // failure path overwriting a pointer it doesn't hold.
  for (auto& innerAccess : innerAccesses) {
    data.store(std::make_shared<RunOnDestruction>([] {}));
    auto expected = std::make_shared<RunOnDestruction>(innerAccess);
    auto desired = std::make_shared<RunOnDestruction>([] {});
    EXPECT_FALSE(data.compare_exchange_strong(expected, desired));
  }
}

TEST(AtomicReadMostlyMainPtrTest, TakesMemoryOrders) {
  // We don't really try to test the memory-order-y-ness of these; just that the
  // code compiles and runs without anything falling over.
  AtomicReadMostlyMainPtr<int> data;
  std::shared_ptr<int> ptr;

  data.load();
  data.load(std::memory_order_relaxed);
  data.load(std::memory_order_acquire);
  data.load(std::memory_order_seq_cst);

  data.store(ptr);
  data.store(ptr, std::memory_order_relaxed);
  data.store(ptr, std::memory_order_release);
  data.store(ptr, std::memory_order_seq_cst);

  data.exchange(ptr);
  data.exchange(ptr, std::memory_order_relaxed);
  data.exchange(ptr, std::memory_order_acquire);
  data.exchange(ptr, std::memory_order_release);
  data.exchange(ptr, std::memory_order_acq_rel);
  data.exchange(ptr, std::memory_order_seq_cst);

  auto successOrders = {
      std::memory_order_relaxed,
      std::memory_order_acquire,
      std::memory_order_release,
      std::memory_order_acq_rel,
      std::memory_order_seq_cst,
  };
  auto failureOrders = {
      std::memory_order_relaxed,
      std::memory_order_acquire,
      std::memory_order_seq_cst,
  };

  data.compare_exchange_weak(ptr, ptr);
  data.compare_exchange_strong(ptr, ptr);

  for (auto successOrder : successOrders) {
    data.compare_exchange_weak(ptr, ptr, successOrder);
    data.compare_exchange_strong(ptr, ptr, successOrder);
    for (auto failureOrder : failureOrders) {
      data.compare_exchange_weak(ptr, ptr, successOrder, failureOrder);
      data.compare_exchange_strong(ptr, ptr, successOrder, failureOrder);
    }
  }
}

TEST(AtomicReadMostlyMainPtrTest, LongLivedReadsDoNotBlockWrites) {
  std::vector<ReadMostlySharedPtr<int>> pointers;
  AtomicReadMostlyMainPtr<int> mainPtr(std::make_shared<int>(123));
  for (int i = 0; i < 10 * 1000; ++i) {
    // Try several ways of trigger refcount modifications.
    auto ptr = mainPtr.load();
    pointers.push_back(ptr);
    pointers.push_back(ptr);
    pointers.emplace_back(std::move(ptr));
  }

  mainPtr.store(std::make_shared<int>(456));
  EXPECT_EQ(*mainPtr.load(), 456);
}

TEST(AtomicReadMostlyMainPtrStressTest, ReadOnly) {
  const int kReaders = 8;
  const int kReads = 1000 * 1000;
  const int kValue = 123;

  AtomicReadMostlyMainPtr<int> data(std::make_shared<int>(kValue));
  std::vector<std::thread> readers(kReaders);
  for (auto& thread : readers) {
    thread = std::thread([&] {
      for (int i = 0; i < kReads; ++i) {
        auto ptr = data.load();
        ASSERT_EQ(*ptr, 123);
      }
    });
  }
  for (auto& thread : readers) {
    thread.join();
  }
}

TEST(AtomicReadMostlyMainPtrStressTest, ReadWrite) {
  const static int kReaders = 4;
  const static int kWriters = 4;
  // This gives a test that runs for about 4 seconds on my machine; that's about
  // the longest we should inflict on test runners.
  const int kNumWrites = (folly::kIsDebug ? 10 * 1000 : 30 * 1000);

  struct WriterAndData {
    WriterAndData(int writer_, int data_) : writer(writer_), data(data_) {}

    int writer;
    int data;
  };

  struct ConsistencyTracker {
    ConsistencyTracker() {
      for (int& i : maxSeenValue) {
        i = 0;
      }
    }

    void update(const WriterAndData& pair) {
      EXPECT_LE(maxSeenValue[pair.writer], pair.data);
      maxSeenValue[pair.writer] = pair.data;
    }

    std::array<int, kWriters> maxSeenValue;
  };

  AtomicReadMostlyMainPtr<WriterAndData> writerAndData(
      std::make_shared<WriterAndData>(0, 0));
  std::atomic<int> numStoppedWriters(0);

  std::vector<std::thread> readers(kReaders);
  std::vector<std::thread> writers(kWriters);

  for (auto& thread : readers) {
    thread = std::thread([&] {
      ConsistencyTracker consistencyTracker;
      while (numStoppedWriters.load() != kWriters) {
        auto ptr = writerAndData.load();
        consistencyTracker.update(*ptr);
      }
    });
  }

  std::atomic<int> casFailures(0);

  for (int threadId = 0; threadId < kWriters; ++threadId) {
    writers[threadId] = std::thread([&, threadId] {
      ConsistencyTracker consistencyTracker;

      for (int j = 0; j < kNumWrites; j++) {
        auto newValue = std::make_shared<WriterAndData>(threadId, j);
        std::shared_ptr<WriterAndData> oldValue;
        switch (j % 3) {
          case 0:
            writerAndData.store(newValue);
            break;
          case 1:
            oldValue = writerAndData.exchange(newValue);
            consistencyTracker.update(*oldValue);
            break;
          case 2:
            oldValue = writerAndData.load().getStdShared();
            consistencyTracker.update(*oldValue);
            while (!writerAndData.compare_exchange_strong(oldValue, newValue)) {
              consistencyTracker.update(*oldValue);
              casFailures.fetch_add(1);
            }
        }
      }
      numStoppedWriters.fetch_add(1);
    });
  }

  for (auto& thread : readers) {
    thread.join();
  }
  for (auto& thread : writers) {
    thread.join();
  }

  // This is a test of our test setup itself; we want to make sure it's
  // sufficiently racy that we actually see concurrent conflicting operations.
  // On a test on my machine, we see values in the thousands, so hopefully this
  // is highly unflakey.
  EXPECT_GE(casFailures.load(), 10);
}
