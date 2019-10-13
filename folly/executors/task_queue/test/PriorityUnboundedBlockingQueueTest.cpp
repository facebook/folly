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

#include <folly/container/Enumerate.h>
#include <folly/executors/task_queue/PriorityUnboundedBlockingQueue.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;

class PriorityUnboundedBlockingQueueTest : public testing::Test {};

TEST_F(PriorityUnboundedBlockingQueueTest, push_pop) {
  PriorityUnboundedBlockingQueue<int> q(3);
  q.add(42);
  EXPECT_EQ(42, q.take());
}

TEST_F(PriorityUnboundedBlockingQueueTest, multiple_push_pop) {
  PriorityUnboundedBlockingQueue<int> q(3);
  q.add(42);
  q.add(77);
  EXPECT_EQ(42, q.take());
  EXPECT_EQ(77, q.take());
}

TEST_F(PriorityUnboundedBlockingQueueTest, size) {
  PriorityUnboundedBlockingQueue<int> q(3);
  EXPECT_EQ(0, q.size());
  q.add(42);
  EXPECT_EQ(1, q.size());
  q.take();
  EXPECT_EQ(0, q.size());
}

TEST_F(PriorityUnboundedBlockingQueueTest, concurrent_push_pop) {
  PriorityUnboundedBlockingQueue<int> q(3);
  Baton<> b1, b2;
  std::thread t([&] {
    b1.post();
    EXPECT_EQ(42, q.take());
    EXPECT_EQ(0, q.size());
    b2.post();
  });
  b1.wait();
  q.add(42);
  b2.wait();
  EXPECT_EQ(0, q.size());
  t.join();
}

TEST_F(PriorityUnboundedBlockingQueueTest, priority_order) {
  PriorityUnboundedBlockingQueue<int> q(3);
  EXPECT_EQ(0, q.size());
  q.addWithPriority(27, 0);
  q.addWithPriority(42, 1);
  q.addWithPriority(55, 0);
  q.addWithPriority(12, -1);
  EXPECT_EQ(4, q.size());
  EXPECT_EQ(42, q.take());
  EXPECT_EQ(27, q.take());
  EXPECT_EQ(55, q.take());
  EXPECT_EQ(12, q.take());
  EXPECT_EQ(0, q.size());
}

// Since PriorityUnboundedBlockingQueue implements folly::BlockingQueue<T>,
// addWithPriority method has to accept priority as int_8. This means invalid
// values for priority (such as negative or very large numbers) might get
// passed. Verify this behavior.
TEST_F(PriorityUnboundedBlockingQueueTest, invalid_priorities) {
  PriorityUnboundedBlockingQueue<int> q(2);
  q.addWithPriority(1, -1); // expected to be converted to the lowest priority
  q.addWithPriority(2, 50); // expected to be converted to the highest priority

  EXPECT_EQ(q.take(), 2);
  EXPECT_EQ(q.take(), 1);
}

TEST_F(PriorityUnboundedBlockingQueueTest, invalid_priorities_edge) {
  PriorityUnboundedBlockingQueue<int> q(2);
  q.addWithPriority(1, Executor::LO_PRI);
  q.addWithPriority(2, Executor::HI_PRI);

  EXPECT_EQ(q.take(), 2);
  EXPECT_EQ(q.take(), 1);
}
