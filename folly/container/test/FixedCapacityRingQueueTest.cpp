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

#include <folly/container/FixedCapacityRingQueue.h>

#include <string>
#include <vector>

#include <folly/portability/GTest.h>

using namespace folly;

TEST(FixedCapacityRingQueueTest, DefaultConstructed) {
  FixedCapacityRingQueue<int> q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.size(), 0);
}

TEST(FixedCapacityRingQueueTest, PushAndPop) {
  FixedCapacityRingQueue<int> q(4);

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

TEST(FixedCapacityRingQueueTest, CapacityRoundsUpToPowerOfTwo) {
  FixedCapacityRingQueue<int> q(5);
  // nextPowTwo(5) == 8
  EXPECT_EQ(q.capacity(), 8);
}

TEST(FixedCapacityRingQueueTest, CapacityExactPowerOfTwo) {
  FixedCapacityRingQueue<int> q(8);
  EXPECT_EQ(q.capacity(), 8);
}

TEST(FixedCapacityRingQueueTest, FillToCapacity) {
  constexpr uint32_t kCap = 16;
  FixedCapacityRingQueue<int> q(kCap);

  for (uint32_t i = 0; i < kCap; ++i) {
    q.push(static_cast<int>(i));
  }
  EXPECT_EQ(q.size(), kCap);

  for (uint32_t i = 0; i < kCap; ++i) {
    EXPECT_EQ(q.pop(), static_cast<int>(i));
  }
  EXPECT_TRUE(q.empty());
}

TEST(FixedCapacityRingQueueTest, Wraparound) {
  constexpr uint32_t kCap = 4;
  FixedCapacityRingQueue<int> q(kCap);

  // Fill completely
  for (uint32_t i = 0; i < kCap; ++i) {
    q.push(static_cast<int>(i));
  }

  // Drain half
  EXPECT_EQ(q.pop(), 0);
  EXPECT_EQ(q.pop(), 1);

  // Refill the freed slots (head has advanced, so new items wrap around)
  q.push(100);
  q.push(101);

  EXPECT_EQ(q.size(), kCap);
  EXPECT_EQ(q.pop(), 2);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), 100);
  EXPECT_EQ(q.pop(), 101);
  EXPECT_TRUE(q.empty());
}

TEST(FixedCapacityRingQueueTest, ManyWraparoundCycles) {
  constexpr uint32_t kCap = 8;
  FixedCapacityRingQueue<int> q(kCap);

  int pushVal = 0;
  for (int cycle = 0; cycle < 100; ++cycle) {
    for (uint32_t i = 0; i < kCap; ++i) {
      q.push(pushVal++);
    }
    for (uint32_t i = 0; i < kCap; ++i) {
      q.pop();
    }
    EXPECT_TRUE(q.empty());
  }
}

TEST(FixedCapacityRingQueueTest, FifoOrdering) {
  FixedCapacityRingQueue<int> q(64);

  std::vector<int> pushed;
  for (int i = 0; i < 50; ++i) {
    q.push(i * 7);
    pushed.push_back(i * 7);
  }

  std::vector<int> popped;
  while (!q.empty()) {
    popped.push_back(q.pop());
  }

  EXPECT_EQ(pushed, popped);
}

TEST(FixedCapacityRingQueueTest, WorksWithStrings) {
  FixedCapacityRingQueue<std::string> q(4);

  q.push("hello");
  q.push("world");

  EXPECT_EQ(q.pop(), "hello");
  EXPECT_EQ(q.pop(), "world");
  EXPECT_TRUE(q.empty());
}

TEST(FixedCapacityRingQueueTest, SizeTracksPushAndPop) {
  FixedCapacityRingQueue<int> q(8);

  EXPECT_EQ(q.size(), 0);
  q.push(1);
  EXPECT_EQ(q.size(), 1);
  q.push(2);
  EXPECT_EQ(q.size(), 2);
  q.pop();
  EXPECT_EQ(q.size(), 1);
  q.pop();
  EXPECT_EQ(q.size(), 0);
}

TEST(FixedCapacityRingQueueTest, SingleElementCapacity) {
  FixedCapacityRingQueue<int> q(1);
  EXPECT_EQ(q.capacity(), 1);

  q.push(42);
  EXPECT_EQ(q.size(), 1);
  EXPECT_EQ(q.pop(), 42);
  EXPECT_TRUE(q.empty());
}

TEST(FixedCapacityRingQueueTest, InterleavedPushPop) {
  FixedCapacityRingQueue<int> q(4);

  q.push(1);
  EXPECT_EQ(q.pop(), 1);

  q.push(2);
  q.push(3);
  EXPECT_EQ(q.pop(), 2);

  q.push(4);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), 4);
  EXPECT_TRUE(q.empty());
}

#ifndef NDEBUG
TEST(FixedCapacityRingQueueTest, PushBeyondCapacityDChecks) {
  FixedCapacityRingQueue<int> q(4);
  for (uint32_t i = 0; i < q.capacity(); ++i) {
    q.push(static_cast<int>(i));
  }
  EXPECT_DEATH(q.push(99), "");
}

TEST(FixedCapacityRingQueueTest, PopWhenEmptyDChecks) {
  FixedCapacityRingQueue<int> q(4);
  EXPECT_DEATH(q.pop(), "");
}
#endif

TEST(FixedCapacityRingQueueTest, MoveConstruct) {
  FixedCapacityRingQueue<int> q(8);
  q.push(10);
  q.push(20);

  FixedCapacityRingQueue<int> q2(std::move(q));

  EXPECT_EQ(q2.size(), 2);
  EXPECT_EQ(q2.capacity(), 8);
  EXPECT_EQ(q2.pop(), 10);
  EXPECT_EQ(q2.pop(), 20);
}

TEST(FixedCapacityRingQueueTest, MoveAssign) {
  FixedCapacityRingQueue<int> q(8);
  q.push(10);
  q.push(20);

  FixedCapacityRingQueue<int> q2(4);
  q2.push(99);

  q2 = std::move(q);

  EXPECT_EQ(q2.size(), 2);
  EXPECT_EQ(q2.capacity(), 8);
  EXPECT_EQ(q2.pop(), 10);
  EXPECT_EQ(q2.pop(), 20);
}

TEST(FixedCapacityRingQueueTest, NotCopyable) {
  EXPECT_FALSE(std::is_copy_constructible_v<FixedCapacityRingQueue<int>>);
  EXPECT_FALSE(std::is_copy_assignable_v<FixedCapacityRingQueue<int>>);
}
