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

#include <thread>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>

namespace folly {
namespace threadlocal_detail {

class ThreadLocalDetailTest : public ::testing::Test {};

template <typename Tag>
struct ThreadLocalTestHelper {
  using Meta = StaticMeta<Tag, void>;
  using TLElem = ThreadLocal<int, Tag>;
  std::vector<TLElem> elements;
};

TEST_F(ThreadLocalDetailTest, Basic) {
  struct Tag {};

  ThreadLocalTestHelper<Tag> helper;
  auto& meta = ThreadLocalTestHelper<Tag>::Meta::instance();

  // No TL object created. Count should be 0.
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  const int32_t count = 16;
  helper.elements.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    helper.elements.emplace_back();
  }

  // TL wrapper obejcts created but no thread has accessed its
  // local copy. Wrappers should still be 0.
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  // Access 1st element. A wrapper array will be allocated. One for
  // the current thread. Vector growth is not precise to minimize churn. Can
  // only check it should be >= 1.
  *helper.elements[0] = 0;
  ASSERT_GE(meta.totalElementWrappers_.load(), 1);

  for (int32_t i = 0; i < count; ++i) {
    *helper.elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), count);
}

// Test the totalElementWrappers_ grows and shrinks as threads come and go.
TEST_F(ThreadLocalDetailTest, MultiThreadedTest) {
  struct Tag {};

  ThreadLocalTestHelper<Tag> helper;
  auto& meta = ThreadLocalTestHelper<Tag>::Meta::instance();

  const int32_t count = 1000;
  helper.elements.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    helper.elements.emplace_back();
  }
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  for (int32_t i = 0; i < count; ++i) {
    *helper.elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), count);

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<test::Barrier>> threadBarriers(count);
  test::Barrier allThreadsBarriers{count + 1};

  for (int32_t i = 0; i < count; ++i) {
    threadBarriers[i] = std::make_unique<test::Barrier>(2);
    threads.emplace_back([&, index = i]() {
      // This thread's vector will sized to have index elements at least.
      *helper.elements[index] = index;
      allThreadsBarriers.wait();
      threadBarriers[index]->wait();
    });
  }

  // Wait for all threads to start.
  allThreadsBarriers.wait();

  // check totalElementWrappers_ is within expected range. Due to vector growth,
  // we cannot assume precise counts but can use a crude range. Thread i touches
  // thread local with index i, and its array should be a bit over i in size.
  // Total count will be count (baseline) plus summation(i) for i over
  // range(num threads).
  auto lowerBound = [](int32_t numThreads) {
    return numThreads * (numThreads - 1) / 2 + (count);
  };

  auto upperBound = [](int32_t numThreads) { return (numThreads + 2) * count; };

  int32_t threadBarriersIndex = count - 1;
  while (!threads.empty()) {
    ASSERT_GE(meta.totalElementWrappers_.load(), lowerBound(threads.size()));
    ASSERT_LE(meta.totalElementWrappers_.load(), upperBound(threads.size()));
    threadBarriers[threadBarriersIndex]->wait();
    threads.back().join();
    threads.pop_back();
    threadBarriersIndex -= 1;
  }
}

// Test the totalElementWrappers_ is stable if TL variables come and go.
TEST_F(ThreadLocalDetailTest, TLObjectsChurn) {
  struct Tag {};

  Synchronized<ThreadLocalTestHelper<Tag>> helper;
  auto& meta = ThreadLocalTestHelper<Tag>::Meta::instance();

  const int32_t count = 1000;
  helper.wlock()->elements.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    helper.wlock()->elements.emplace_back();
  }
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  for (int32_t i = 0; i < count; ++i) {
    *helper.wlock()->elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), count);

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<test::Barrier>> threadBarriers;
  test::Barrier allThreadsBarriers{count + 1};

  for (int32_t i = 0; i < count; ++i) {
    threadBarriers.push_back(std::make_unique<test::Barrier>(2));
    threads.emplace_back([&, index = i]() {
      *helper.wlock()->elements[index] = index;
      allThreadsBarriers.wait();

      // wait once for main thread to replace the index entry with a new TL
      // variable.
      threadBarriers[index]->wait();
      // This thread's vector will sized to have index elements at least.
      *helper.wlock()->elements[index] = index;
      // Wait to exit.
      allThreadsBarriers.wait();
    });
  }

  // Wait for all threads to start.
  allThreadsBarriers.wait();

  auto lowerBound = [](int32_t numThreads) {
    return numThreads * (numThreads - 1) / 2 + (count);
  };

  auto upperBound = [](int32_t numThreads) { return (numThreads + 2) * count; };

  // Replace each element with a new one. Overall wrappers should stay stable as
  // freed up id get recycled.
  for (int32_t i = 0; i < count; ++i) {
    helper.wlock()->elements[i] = {};
    *helper.wlock()->elements[i] = 0;
    ASSERT_EQ(meta.nextId_, count + 1);
    threadBarriers[i]->wait();
    ASSERT_GE(meta.totalElementWrappers_.load(), lowerBound(threads.size()));
    ASSERT_LE(meta.totalElementWrappers_.load(), upperBound(threads.size()));
  }

  allThreadsBarriers.wait();
  for (int32_t i = 0; i < count; ++i) {
    threads[i].join();
  }
  threads.clear();
  threadBarriers.clear();
}

// Test race between tl.reset() and threads exiting.
TEST_F(ThreadLocalDetailTest, TLResetAndThreadExitRace) {
  struct Data {
    int value;
  };

  std::atomic<uint32_t> numTLObjects{0};

  folly::ThreadLocalPtr<Data, Data>* tlObject =
      new folly::ThreadLocalPtr<Data, Data>();

  const int32_t count = 256;
  std::vector<std::thread> threads;
  test::Barrier allThreadsBarriers{count + 1};

  threads.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    threads.emplace_back([&, index = i]() {
      Data* d = new Data();
      d->value = index;
      numTLObjects++;
      tlObject->reset(
          d, [&, expected = index](Data* d, TLPDestructionMode mode) {
            ASSERT_EQ(d->value, expected);
            if (mode == TLPDestructionMode::THIS_THREAD) {
              d->value = count + expected;
            } else {
              d->value = count * 2 + expected;
            }
            delete d;
          });
      allThreadsBarriers.wait();
      // Thread will exit and delete tl version of tlObject.
    });
  }

  // Wait for all threads to start.
  allThreadsBarriers.wait();

  // Destroy tlObject. The call will race with threads exiting
  // and also trying to destroy their individual version of it.
  delete tlObject;

  for (int32_t i = 0; i < count; ++i) {
    threads[i].join();
  }
  threads.clear();
}

// Test reallocation of elements array is not blocked by Accessor for
// accessAllThreads
TEST_F(ThreadLocalDetailTest, accessAllAndRealloc) {
  struct Tag {};

  ThreadLocalTestHelper<Tag> helper;

  const int32_t count = 1000;
  helper.elements.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    helper.elements.emplace_back();
  }

  // Elements 1..count are initialized
  // under accessor later.
  *helper.elements[0] = 0;

  std::thread spawnMoreThreads;
  const int32_t countSubThreads = 128;
  std::vector<std::thread> threads;
  test::Barrier threadsBarrier(countSubThreads + 1);
  // Barrier to ensure all threads started by
  // spawnMoreThreads is done after the Accessor is created.
  test::Barrier spawnHelperBarrier(2);

  spawnMoreThreads = std::thread([&]() {
    spawnHelperBarrier.wait();
    // create a bunch of threads. let them assign some value to the elemnts
    // other than one at index 0. This will cause elements array to be
    // reallocated a few times. They should not get blocked by presence of the
    // accessAllThreads accessor.
    for (int32_t i = 0; i < countSubThreads; ++i) {
      threads.emplace_back([count = count, &helper, &threadsBarrier]() {
        for (int32_t i = 1; i < count; ++i) {
          *helper.elements[i] = 1;
        }
        threadsBarrier.wait();
      });
    }
  });

  {
    auto accessor = helper.elements[0].accessAllThreads();
    spawnHelperBarrier.wait();
    // Thread holding accessor can itself also update any other
    // TL, trigger reallocs from it and not get stuck.
    for (int32_t i = 1; i < count; ++i) {
      *helper.elements[i] = i;
    }
    // Helper thread will spawn all the countSubThreads and each
    // will be able to setup a value for elements[1] without being
    // blocked on 'accessor'. They will not be able to exit, so we
    // only wait here for each to be done assigning to element[1]
    // but not to exit.
    threadsBarrier.wait();
  }
  spawnMoreThreads.join();
  for (int32_t i = 0; i < countSubThreads; ++i) {
    threads[i].join();
  }
}

} // namespace threadlocal_detail
} // namespace folly
