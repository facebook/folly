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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <folly/AtomicLinkedList.h>
#include <folly/Utility.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>

namespace {

class TestIntrusiveObject {
 public:
  explicit TestIntrusiveObject(size_t id) : id_(id) {}

  // No copies or moves are allowed.
  TestIntrusiveObject(const TestIntrusiveObject&) = delete;
  TestIntrusiveObject& operator=(const TestIntrusiveObject&) = delete;
  TestIntrusiveObject(TestIntrusiveObject&&) = delete;
  TestIntrusiveObject& operator=(TestIntrusiveObject&&) = delete;

  size_t id() const { return id_; }

  TestIntrusiveObject*& next() { return hook_.next; }

 private:
  folly::AtomicIntrusiveLinkedListHook<TestIntrusiveObject> hook_;
  size_t id_;

 public:
  using List = folly::AtomicIntrusiveLinkedList<
      TestIntrusiveObject,
      &TestIntrusiveObject::hook_>;
};

} // namespace

TEST(AtomicIntrusiveLinkedList, Basic) {
  TestIntrusiveObject a(1), b(2), c(3);

  TestIntrusiveObject::List list;

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(nullptr, list.unsafeHead());

  {
    EXPECT_TRUE(list.insertHead(&a));
    EXPECT_EQ(&a, list.unsafeHead());
    EXPECT_FALSE(list.insertHead(&b));
    EXPECT_EQ(&b, list.unsafeHead());

    EXPECT_FALSE(list.empty());

    size_t id = 0;
    list.sweep([&](TestIntrusiveObject* obj) mutable {
      EXPECT_EQ(nullptr, list.unsafeHead());
      ++id;
      EXPECT_EQ(id, obj->id());
    });

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(nullptr, list.unsafeHead());
  }

  // Try re-inserting the same item (b) and a new item (c)
  {
    EXPECT_TRUE(list.insertHead(&b));
    EXPECT_FALSE(list.insertHead(&c));

    EXPECT_FALSE(list.empty());

    size_t id = 1;
    list.sweep([&](TestIntrusiveObject* obj) mutable {
      ++id;
      EXPECT_EQ(id, obj->id());
    });

    EXPECT_TRUE(list.empty());
  }

  TestIntrusiveObject::List movedList = std::move(list);
}

TEST(AtomicIntrusiveLinkedList, ReverseSweep) {
  TestIntrusiveObject a(1), b(2), c(3);

  TestIntrusiveObject::List list;
  list.insertHead(&a);
  list.insertHead(&b);
  list.insertHead(&c);

  size_t id = 3;
  list.reverseSweep([&](TestIntrusiveObject* obj) {
    EXPECT_EQ(id, obj->id());
    --id;
  });

  EXPECT_TRUE(list.empty());

  // Test that we can still insert
  list.insertHead(&a);

  EXPECT_FALSE(list.empty());
  list.reverseSweep([](TestIntrusiveObject* obj) { EXPECT_EQ(1, obj->id()); });
}

TEST(AtomicIntrusiveLinkedList, Move) {
  TestIntrusiveObject a(1), b(2);

  TestIntrusiveObject::List list1;

  EXPECT_TRUE(list1.insertHead(&a));
  EXPECT_FALSE(list1.insertHead(&b));

  EXPECT_FALSE(list1.empty());

  TestIntrusiveObject::List list2(std::move(list1));

  EXPECT_TRUE(list1.empty());
  EXPECT_FALSE(list2.empty());

  TestIntrusiveObject::List list3 = list2.spliceAll();

  EXPECT_TRUE(list2.empty());
  EXPECT_FALSE(list3.empty());

  size_t id = 0;
  list3.sweep([&](TestIntrusiveObject* obj) mutable {
    ++id;
    EXPECT_EQ(id, obj->id());
  });
}

TEST(AtomicIntrusiveLinkedList, Arm) {
  TestIntrusiveObject a(1), b(2), c(3);

  TestIntrusiveObject::List list;

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(nullptr, list.unsafeHead());

  // arm first
  {
    EXPECT_EQ(nullptr, list.arm());
    EXPECT_TRUE(list.insertHeadArm(&a));
    EXPECT_EQ(&a, list.unsafeHead());
    EXPECT_FALSE(list.insertHeadArm(&b));
    EXPECT_EQ(&b, list.unsafeHead());

    EXPECT_FALSE(list.empty());

    size_t id = 0;
    list.sweep([&](TestIntrusiveObject* obj) mutable {
      EXPECT_EQ(nullptr, list.unsafeHead());
      ++id;
      EXPECT_EQ(id, obj->id());
    });

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(nullptr, list.unsafeHead());
  }

  // do not arm first
  {
    EXPECT_FALSE(list.insertHeadArm(&a));
    EXPECT_EQ(&a, list.unsafeHead());
    EXPECT_FALSE(list.insertHeadArm(&b));
    EXPECT_EQ(&b, list.unsafeHead());

    EXPECT_FALSE(list.empty());
    auto* ret = list.arm();
    EXPECT_EQ(&b, ret);
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(nullptr, list.unsafeHead());

    // unlink
    while (ret) {
      auto* next = ret->next();
      ret->next() = nullptr;
      ret = next;
    }
  }

  // Try re-inserting the same item (b) and a new item (c)
  {
    EXPECT_FALSE(list.insertHeadArm(&b));
    EXPECT_FALSE(list.insertHeadArm(&c));

    EXPECT_FALSE(list.empty());

    size_t id = 1;
    list.sweep([&](TestIntrusiveObject* obj) mutable {
      ++id;
      EXPECT_EQ(id, obj->id());
    });

    EXPECT_TRUE(list.empty());
  }

  TestIntrusiveObject::List movedList = std::move(list);
}

TEST(AtomicIntrusiveLinkedList, Stress) {
  static constexpr size_t kNumThreads = 32;
  static constexpr size_t kNumElements = 100000;

  std::vector<std::unique_ptr<TestIntrusiveObject>> elements;
  for (size_t i = 0; i < kNumThreads * kNumElements; ++i) {
    elements.push_back(std::make_unique<TestIntrusiveObject>(i));
  }

  folly::test::Barrier gate(kNumThreads + 1);

  TestIntrusiveObject::List list;

  std::vector<std::thread> threads;
  for (size_t threadId = 0; threadId < kNumThreads; ++threadId) {
    threads.emplace_back([&, threadId] {
      gate.wait();
      for (size_t id = 0; id < kNumElements; ++id) {
        list.insertHead(elements[threadId + kNumThreads * id].get());
      }
    });
  }

  std::vector<size_t> ids;
  TestIntrusiveObject* prev{nullptr};

  gate.wait();

  while (ids.size() < kNumThreads * kNumElements) {
    list.sweep([&](TestIntrusiveObject* current) {
      ids.push_back(current->id());

      if (prev && prev->id() % kNumThreads == current->id() % kNumThreads) {
        EXPECT_EQ(prev->id() + kNumThreads, current->id());
      }

      prev = current;
    });
  }

  std::sort(ids.begin(), ids.end());

  for (size_t i = 0; i < kNumThreads * kNumElements; ++i) {
    EXPECT_EQ(i, ids[i]);
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

namespace {

template <typename T>
struct InstanceCounted {
  InstanceCounted() { incNumInstances(); }

  InstanceCounted(const InstanceCounted&) { incNumInstances(); }
  InstanceCounted& operator=(const InstanceCounted&) = default;

  InstanceCounted(InstanceCounted&&) noexcept { incNumInstances(); }
  InstanceCounted& operator=(InstanceCounted&&) = default;

  ~InstanceCounted() { decNumInstances(); }

  static size_t numInstances() {
    return numInstances_.load(std::memory_order_acquire);
  }

 private:
  static void incNumInstances() {
    numInstances_.fetch_add(1, std::memory_order_release);
  }

  static void decNumInstances() {
    numInstances_.fetch_sub(1, std::memory_order_release);
  }

  static inline std::atomic<size_t> numInstances_ = 0;
};

class TestObject : public InstanceCounted<TestObject>, public folly::MoveOnly {
 public:
  explicit TestObject(size_t id) : id_(id) {}

  size_t id() const { return id_; }

 private:
  size_t id_;
};

} // namespace

TEST(AtomicLinkedList, Basic) {
  constexpr size_t kNumElements = 10;

  folly::AtomicLinkedList<TestObject> list;
  EXPECT_EQ(0, TestObject::numInstances());

  for (size_t id = 0; id < kNumElements; ++id) {
    EXPECT_EQ(id == 0, list.insertHead(TestObject(id)));
    EXPECT_EQ(id + 1, TestObject::numInstances());
  }

  size_t id = 0;
  list.sweep([&](TestObject&& object) {
    EXPECT_EQ(id, object.id());
    EXPECT_EQ(kNumElements - id, TestObject::numInstances());
    ++id;
  });

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(0, TestObject::numInstances());
}

TEST(AtomicLinkedList, MoveConstructor) {
  constexpr size_t kNumElements = 10;

  folly::AtomicLinkedList<TestObject> list1;
  EXPECT_EQ(0, TestObject::numInstances());

  for (size_t id = 0; id < kNumElements; ++id) {
    EXPECT_EQ(id == 0, list1.insertHead(TestObject(id)));
    EXPECT_EQ(id + 1, TestObject::numInstances());
  }

  EXPECT_FALSE(list1.empty());
  EXPECT_EQ(kNumElements, TestObject::numInstances());

  auto list2(std::move(list1));

  EXPECT_TRUE(list1.empty());
  EXPECT_FALSE(list2.empty());
  EXPECT_EQ(kNumElements, TestObject::numInstances());

  auto list3(std::move(list2));
  EXPECT_TRUE(list2.empty());
  EXPECT_FALSE(list3.empty());
  EXPECT_EQ(kNumElements, TestObject::numInstances());

  size_t id = 0;
  list3.sweep([&](TestObject&& obj) mutable {
    EXPECT_EQ(id, obj.id());
    EXPECT_EQ(kNumElements - id, TestObject::numInstances());
    ++id;
  });

  EXPECT_TRUE(list3.empty());
  EXPECT_EQ(0, TestObject::numInstances());
}

TEST(AtomicLinkedList, MoveAssignment) {
  constexpr size_t kNumLists = 5;
  constexpr size_t kNumElements = 10;

  std::vector<folly::AtomicLinkedList<TestObject>> lists(kNumLists);
  EXPECT_EQ(0, TestObject::numInstances());

  for (auto& list : lists) {
    EXPECT_TRUE(list.empty());
    for (size_t id = 0; id < kNumElements; ++id) {
      EXPECT_EQ(id == 0, list.insertHead(TestObject(id)));
    }
    EXPECT_FALSE(list.empty());
  }

  EXPECT_EQ(kNumLists * kNumElements, TestObject::numInstances());

  for (size_t currIdx = 0, nextIdx = 1; nextIdx < kNumLists;
       ++currIdx, ++nextIdx) {
    lists[nextIdx] = std::move(lists[currIdx]);
    EXPECT_EQ((kNumLists - nextIdx) * kNumElements, TestObject::numInstances());
    EXPECT_FALSE(lists[nextIdx].empty());
    EXPECT_TRUE(lists[currIdx].empty());
  }

  auto& lastList = lists[kNumLists - 1];
  size_t id = 0;
  lastList.sweep([&](TestObject&& obj) mutable {
    EXPECT_EQ(id, obj.id());
    EXPECT_EQ(kNumElements - id, TestObject::numInstances());
    ++id;
  });

  EXPECT_TRUE(lastList.empty());
  EXPECT_EQ(0, TestObject::numInstances());
}
