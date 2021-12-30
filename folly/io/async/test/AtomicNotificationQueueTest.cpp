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

#include <functional>
#include <utility>
#include <vector>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseAtomicNotificationQueue.h>
#include <folly/portability/GTest.h>

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
  EventBaseAtomicNotificationQueue<int, decltype(consumer)> queue{
      std::move(consumer)};

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

TEST(AtomicNotificationQueueTest, DiscardDequeuedTasks) {
  struct TaskWithExpiry {
    int datum;
    bool isExpired;
  };

  struct Consumer {
    explicit Consumer(std::vector<int>& data) : data(data) {}
    AtomicNotificationQueueTaskStatus operator()(
        TaskWithExpiry&& task) noexcept {
      if (task.isExpired) {
        return AtomicNotificationQueueTaskStatus::DISCARD;
      }
      data.push_back(task.datum);
      return AtomicNotificationQueueTaskStatus::CONSUMED;
    }
    vector<int>& data;
  };
  vector<int> data;
  Consumer consumer{data};

  EventBaseAtomicNotificationQueue<TaskWithExpiry, Consumer> queue{
      std::move(consumer)};
  queue.setMaxReadAtOnce(10);

  vector<TaskWithExpiry> tasks = {
      {0, false},
      {1, true},
      {2, true},
      {3, false},
      {4, false},
      {5, false},
      {6, false},
      {7, true},
      {8, false},
      {9, false},
      {10, false},
      {11, false},
      {12, true},
      {13, false},
      {14, true},
      {15, false},
  };

  EventBase eventBase;
  queue.startConsuming(&eventBase);

  for (auto& t : tasks) {
    queue.putMessage(t);
  }

  eventBase.loopOnce();

  vector<int> expectedMessages = {0, 3, 4, 5, 6, 8, 9, 10, 11, 13};
  EXPECT_EQ(data.size(), expectedMessages.size());
  for (unsigned i = 0; i < expectedMessages.size(); ++i) {
    EXPECT_EQ(data.at(i), expectedMessages[i]);
  }

  data.clear();
  eventBase.loopOnce();

  EXPECT_EQ(data.size(), 1);
  EXPECT_EQ(data.at(0), 15);
}

TEST(AtomicNotificationQueueTest, PutMessage) {
  struct Data {
    int datum;
    bool isExpired;

    explicit Data(int datum, bool isExpired)
        : datum(datum), isExpired(isExpired) {}

    bool operator==(const Data& data) const {
      return datum == data.datum && isExpired == data.isExpired;
    }
  };

  struct Consumer {
    explicit Consumer(vector<Data>& data) : data(data) {}
    void operator()(Data&& task) noexcept { data.push_back(task); }
    vector<Data>& data;
  };

  vector<Data> expected =
                   {Data(10, false),
                    Data(20, true),
                    Data(-8, true),
                    Data(0, false)},
               actual;
  Consumer consumer{actual};

  EventBaseAtomicNotificationQueue<Data, decltype(consumer)> queue{
      std::move(consumer)};
  queue.setMaxReadAtOnce(0);

  EventBase eventBase;
  queue.startConsuming(&eventBase);

  for (auto& t : expected) {
    queue.putMessage(t.datum, t.isExpired);
  }

  eventBase.loopOnce();

  EXPECT_EQ(expected.size(), actual.size());
  for (unsigned i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i], actual[i]);
  }
}
