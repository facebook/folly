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
    helper.elements.push_back({});
  }

  // TL wrapper obejcts created but no thread has accessed its
  // local copy. Wrappers should still be 0.
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  // Access 1st element. At least 2 wrapper arrays will be allocated. One for
  // the current thread and one for the embedded head_ in the StaticMeta.
  // Vector growth is not precise to minimize churn. Can only check it should be
  // >= 2.
  *helper.elements[0] = 0;
  ASSERT_GE(meta.totalElementWrappers_.load(), 2);
  ;

  for (int32_t i = 0; i < count; ++i) {
    *helper.elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), 2 * count);
  ;
}

// Test the totalElementWrappers_ grows and shrinks as threads come and go.
TEST_F(ThreadLocalDetailTest, MultiThreadedTest) {
  struct Tag {};

  ThreadLocalTestHelper<Tag> helper;
  auto& meta = ThreadLocalTestHelper<Tag>::Meta::instance();

  const int32_t count = 1000;
  helper.elements.reserve(count);
  for (int32_t i = 0; i < count; ++i) {
    helper.elements.push_back({});
  }
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  for (int32_t i = 0; i < count; ++i) {
    *helper.elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), 2 * count);

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<test::Barrier>> threadBarriers;
  test::Barrier allThreadsBarriers{count + 1};

  for (int32_t i = 0; i < count; ++i) {
    threadBarriers.push_back(std::make_unique<test::Barrier>(2));
    threads.push_back(std::thread([&, index = i]() {
      // This thread's vector will sized to have index elements at least.
      *helper.elements[index] = index;
      allThreadsBarriers.wait();
      threadBarriers[index]->wait();
    }));
  }

  // Wait for all threads to start.
  allThreadsBarriers.wait();

  // check totalElementWrappers_ is within expected range. Due to vector growth,
  // we cannot assume precise counts but can use a crude range. Thread i touches
  // thread local with index i, and its array should be a bit over i in size.
  // Total count will be 2*count (baseline) plus summation(i) for i over
  // range(num threads).
  auto lowerBound = [](int32_t numThreads) {
    return numThreads * (numThreads - 1) / 2 + (2 * count);
  };

  auto upperBound = [](int32_t numThreads) { return (numThreads + 2) * count; };

  while (!threads.empty()) {
    ASSERT_GE(meta.totalElementWrappers_.load(), lowerBound(threads.size()));
    ASSERT_LE(meta.totalElementWrappers_.load(), upperBound(threads.size()));
    threadBarriers.back()->wait();
    threads.back().join();
    threads.pop_back();
    threadBarriers.pop_back();
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
    helper.wlock()->elements.push_back({});
  }
  ASSERT_EQ(meta.totalElementWrappers_.load(), 0);

  for (int32_t i = 0; i < count; ++i) {
    *helper.wlock()->elements[i] = i;
  }
  ASSERT_GE(meta.totalElementWrappers_.load(), 2 * count);

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<test::Barrier>> threadBarriers;
  test::Barrier allThreadsBarriers{count + 1};

  for (int32_t i = 0; i < count; ++i) {
    threadBarriers.push_back(std::make_unique<test::Barrier>(2));
    threads.push_back(std::thread([&, index = i]() {
      *helper.wlock()->elements[index] = index;
      allThreadsBarriers.wait();

      // wait once for main thread to replace the index entry with a new TL
      // variable.
      threadBarriers[index]->wait();
      // This thread's vector will sized to have index elements at least.
      *helper.wlock()->elements[index] = index;
      // Wait to exit.
      allThreadsBarriers.wait();
    }));
  }

  // Wait for all threads to start.
  allThreadsBarriers.wait();

  auto lowerBound = [](int32_t numThreads) {
    return numThreads * (numThreads - 1) / 2 + (2 * count);
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

} // namespace threadlocal_detail
} // namespace folly
