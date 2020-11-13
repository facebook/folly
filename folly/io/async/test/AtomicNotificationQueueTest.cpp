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

#include <folly/io/async/AtomicNotificationQueue.h>
#include <folly/portability/GTest.h>

#include <functional>
#include <utility>
#include <vector>

using namespace folly;
using namespace std;

template <typename Task>
struct AtomicNotificationQueueConsumer {
  explicit AtomicNotificationQueueConsumer(vector<Task>& tasks)
      : tasks(tasks) {}

  void operator()(Task&& value) noexcept {
    tasks.push_back(value);
    if (fn) {
      fn(std::move(value));
    }
  }

  function<void(Task&&)> fn;
  vector<Task>& tasks;
};

TEST(AtomicNotificationQueueTest, TryPutMessage) {
  vector<int> data;
  AtomicNotificationQueueConsumer<int> consumer{data};
  AtomicNotificationQueue<int, decltype(consumer)> queue{std::move(consumer)};

  constexpr uint32_t kMaxSize = 10;

  for (auto i = 1; i <= 9; ++i) {
    queue.putMessage(std::move(i));
  }

  EXPECT_TRUE(queue.tryPutMessage(10, kMaxSize));
  EXPECT_EQ(queue.size(), 10);
  EXPECT_FALSE(queue.tryPutMessage(11, kMaxSize));
  EXPECT_EQ(queue.size(), 10);
  queue.putMessage(11);
  EXPECT_EQ(queue.size(), 11);
  EXPECT_FALSE(queue.tryPutMessage(12, kMaxSize));

  queue.drain();
  EXPECT_TRUE(queue.tryPutMessage(0, kMaxSize));
  EXPECT_EQ(queue.size(), 1);
}
