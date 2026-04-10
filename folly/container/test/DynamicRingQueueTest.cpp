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

#include <folly/container/DynamicRingQueue.h>

#include <folly/lang/Bits.h>
#include <folly/portability/GTest.h>

using namespace folly;

template <typename T>
class DynamicRingQueueTestHelper {
 public:
  static void setTail(DynamicRingQueue<T>& q, uint32_t tail) { q.tail_ = tail; }
};

TEST(DynamicRingQueueTest, DefaultConstructed) {
  DynamicRingQueue<int> q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.size(), 0);
  EXPECT_EQ(q.capacity(), 0);
}

TEST(DynamicRingQueueTest, PushToDefaultConstructed) {
  DynamicRingQueue<int> q;
  q.push(42);
  EXPECT_EQ(q.size(), 1);
  EXPECT_EQ(q.pop(), 42);
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, PushAndPop) {
  DynamicRingQueue<int> q(4);

  q.push(10);
  q.push(20);
  q.push(30);

  EXPECT_EQ(q.size(), 3);
  EXPECT_FALSE(q.empty());

  EXPECT_EQ(q.pop(), 10);
  EXPECT_EQ(q.pop(), 20);
  EXPECT_EQ(q.pop(), 30);
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, CapacityRoundsUpToPowerOfTwo) {
  DynamicRingQueue<int> q(5);
  EXPECT_EQ(q.capacity(), 8);
}

TEST(DynamicRingQueueTest, CapacityExactPowerOfTwo) {
  DynamicRingQueue<int> q(8);
  EXPECT_EQ(q.capacity(), 8);
}

TEST(DynamicRingQueueTest, FillToCapacity) {
  DynamicRingQueue<int> q(16);

  for (int i = 0; i < 16; ++i) {
    q.push(i);
  }
  EXPECT_EQ(q.size(), 16);

  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(q.pop(), i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, Wraparound) {
  DynamicRingQueue<int> q(4);

  q.push(0);
  q.push(1);
  q.push(2);
  q.push(3);

  EXPECT_EQ(q.pop(), 0);
  EXPECT_EQ(q.pop(), 1);

  q.push(100);
  q.push(101);

  EXPECT_EQ(q.size(), 4);
  EXPECT_EQ(q.pop(), 2);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), 100);
  EXPECT_EQ(q.pop(), 101);
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, GrowOnOverflow) {
  DynamicRingQueue<int> q(4);
  EXPECT_EQ(q.capacity(), 4);

  q.push(1);
  q.push(2);
  q.push(3);
  q.push(4);
  EXPECT_EQ(q.size(), 4);

  q.push(5);
  EXPECT_EQ(q.capacity(), 8);
  EXPECT_EQ(q.size(), 5);

  EXPECT_EQ(q.pop(), 1);
  EXPECT_EQ(q.pop(), 2);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), 4);
  EXPECT_EQ(q.pop(), 5);
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, GrowPreservesFifoAcrossCycles) {
  DynamicRingQueue<int> q(4);

  for (int i = 0; i < 4; ++i) {
    q.push(i);
  }
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(q.pop(), i);
  }

  for (int i = 10; i < 17; ++i) {
    q.push(i);
  }

  EXPECT_EQ(q.pop(), 3);
  for (int i = 10; i < 17; ++i) {
    EXPECT_EQ(q.pop(), i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, MultipleGrows) {
  DynamicRingQueue<int> q(2);
  EXPECT_EQ(q.capacity(), 2);

  for (int i = 0; i < 100; ++i) {
    q.push(i);
  }

  EXPECT_EQ(q.capacity(), folly::nextPowTwo(100u));
  EXPECT_EQ(q.size(), 100);

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(q.pop(), i);
  }
  EXPECT_TRUE(q.empty());
}

TEST(DynamicRingQueueTest, MoveConstruct) {
  DynamicRingQueue<int> q(8);
  q.push(10);
  q.push(20);

  DynamicRingQueue<int> q2(std::move(q));

  EXPECT_EQ(q2.size(), 2);
  EXPECT_EQ(q2.capacity(), 8);
  EXPECT_EQ(q2.pop(), 10);
  EXPECT_EQ(q2.pop(), 20);
}

TEST(DynamicRingQueueTest, MoveAssign) {
  DynamicRingQueue<int> q(8);
  q.push(10);
  q.push(20);

  DynamicRingQueue<int> q2(4);
  q2.push(99);

  q2 = std::move(q);

  EXPECT_EQ(q2.size(), 2);
  EXPECT_EQ(q2.capacity(), 8);
  EXPECT_EQ(q2.pop(), 10);
  EXPECT_EQ(q2.pop(), 20);
}

TEST(DynamicRingQueueTest, NotCopyable) {
  EXPECT_FALSE(std::is_copy_constructible_v<DynamicRingQueue<int>>);
  EXPECT_FALSE(std::is_copy_assignable_v<DynamicRingQueue<int>>);
}

TEST(DynamicRingQueueTest, ConstructorRejectsOverflow) {
  constexpr auto kMax = DynamicRingQueue<int>::kMaxCapacity;
  EXPECT_THROW(DynamicRingQueue<int>(kMax + 1), std::length_error);
}

TEST(DynamicRingQueueTest, GrowRejectsOverflow) {
  constexpr auto kMax = DynamicRingQueue<char>::kMaxCapacity;
  DynamicRingQueue<char> q(kMax);
  EXPECT_EQ(q.capacity(), kMax);
  DynamicRingQueueTestHelper<char>::setTail(q, kMax);
  EXPECT_EQ(q.size(), kMax);
  EXPECT_THROW(q.push(0), std::length_error);
}

#ifndef NDEBUG
TEST(DynamicRingQueueTest, PopWhenEmptyDChecks) {
  DynamicRingQueue<int> q(4);
  EXPECT_DEATH(q.pop(), "");
}
#endif
