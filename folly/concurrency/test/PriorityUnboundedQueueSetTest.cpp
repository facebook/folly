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

#include <vector>

#include <folly/concurrency/PriorityUnboundedQueueSet.h>
#include <folly/container/Enumerate.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;

class PriorityUnboundedQueueSetTest : public testing::Test {};

TEST_F(PriorityUnboundedQueueSetTest, try_dequeue) {
  PriorityUSPSCQueueSet<int, false> q(3);
  q.at_priority(1).enqueue(42);
  EXPECT_EQ(42, q.try_dequeue().value());
}

TEST_F(PriorityUnboundedQueueSetTest, try_peek) {
  PriorityUSPSCQueueSet<int, false> q(3);
  q.at_priority(1).enqueue(42);
  EXPECT_EQ(42, *q.try_peek());
  q.at_priority(1).enqueue(42);
  EXPECT_EQ(42, q.try_dequeue().value());
}

TEST_F(PriorityUnboundedQueueSetTest, size_empty) {
  PriorityUSPSCQueueSet<int, false> q(3);
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(0, q.size());

  q.at_priority(1).enqueue(42);
  EXPECT_FALSE(q.empty());
  EXPECT_EQ(1, q.size());

  EXPECT_EQ(42, *q.try_peek());
  EXPECT_FALSE(q.empty());
  EXPECT_EQ(1, q.size());

  EXPECT_EQ(42, q.try_dequeue().value());
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(0, q.size());
}

TEST_F(PriorityUnboundedQueueSetTest, priority_order) {
  PriorityUSPSCQueueSet<int, false> q(3);
  EXPECT_EQ(0, q.size());
  q.at_priority(1).enqueue(55);
  q.at_priority(2).enqueue(42);
  q.at_priority(0).enqueue(12);
  q.at_priority(1).enqueue(27);
  EXPECT_EQ(4, q.size());
  EXPECT_EQ(12, q.try_dequeue().value());
  EXPECT_EQ(55, q.try_dequeue().value());
  EXPECT_EQ(27, q.try_dequeue().value());
  EXPECT_EQ(42, q.try_dequeue().value());
  EXPECT_EQ(0, q.size());
}
