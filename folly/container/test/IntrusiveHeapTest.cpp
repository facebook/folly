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
#include <memory>
#include <ostream>
#include <random>
#include <type_traits>
#include <vector>

#include <folly/Random.h>
#include <folly/container/IntrusiveHeap.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

DEFINE_int32(fuzz_count, 10000, "Number of operations to fuzz");

namespace folly {

namespace {

struct A;
struct B;

struct TaskBase {
  explicit TaskBase(char id) : id(id) {}

  bool operator<(const TaskBase& other) const { return pri < other.pri; }

  friend std::ostream& operator<<(std::ostream& os, const TaskBase& t) {
    return os << t.pri << ": " << t.id;
  }

  char id;
  int pri = 0;
};

struct Task : public TaskBase,
              public IntrusiveHeapNode<A>,
              public IntrusiveHeapNode<B> {
  using TaskBase::TaskBase;
};

struct TrackedTask : public Task {
  using Task::Task;

  bool& in(IntrusiveHeap<TrackedTask, std::less<>, A>&) { return inA; }
  bool& in(IntrusiveHeap<TrackedTask, std::less<>, B>&) { return inB; }

  bool inA = false;
  bool inB = false;
};

class TaskWithComposition : public TaskBase {
 public:
  using TaskBase::TaskBase;

 private:
  IntrusiveHeapNode<void> node_;

 public:
  using NodeTraits =
      MemberNodeTraits<TaskWithComposition, void, &TaskWithComposition::node_>;
};

} // namespace

class IntrusiveHeapTest {
 public:
  template <class Heap>
  static void print(const Heap& heap, std::ostream& os) {
    if (heap.root_ == nullptr) {
      os << "EMPTY";
      return;
    }
    print<Heap>(0, heap.root_, os << '\n');
  }

  template <class Heap>
  static void check(const Heap& heap) {
    check(heap, heap.root_, nullptr);
  }

 private:
  template <class Heap>
  static void check(
      const Heap& heap,
      const typename Heap::Node* node,
      const typename Heap::Node* parent) {
    if (node == nullptr)
      return;
    CHECK_EQ(node->parent_, parent) << "on node " << node << " in " << heap;
    if (parent != nullptr) {
      CHECK(!Heap::compare(parent, node))
          << "on node " << node << " in " << heap;
    }
    check(heap, node->left_, node);
    check(heap, node->right_, node);
  }

  template <class Heap>
  static void print(
      int indent, const typename Heap::Node* node, std::ostream& os) {
    if (node == nullptr)
      return;
    // Right first so it looks like a top-down tree, but with left := up.
    print<Heap>(indent + 2, node->right_, os);
    os << std::string(indent, ' ') << *Heap::asT(node) << " (" << node
       << "; parent: " << node->parent_ << "; left: " << node->left_
       << "; right: " << node->right_ << ")\n";
    print<Heap>(indent + 2, node->left_, os);
  }
};

template <class T, class Compare, class Tag, class Traits>
std::ostream& operator<<(
    std::ostream& os, const IntrusiveHeap<T, Compare, Tag, Traits>& heap) {
  IntrusiveHeapTest::print(heap, os);
  return os;
}

TEST(IntrusiveHeap, Static) {
  Task x('x');
  IntrusiveHeapNode<A>* na = &x;
  IntrusiveHeapNode<B>* nb = &x;
  EXPECT_EQ(static_cast<Task*>(na), &x);
  EXPECT_EQ(static_cast<Task*>(nb), &x);
  EXPECT_NE(static_cast<void*>(na), static_cast<void*>(nb));
  static_assert(!std::is_copy_assignable_v<Task>);
  static_assert(!std::is_move_assignable_v<Task>);
  static_assert(!std::is_copy_constructible_v<Task>);
  static_assert(!std::is_move_constructible_v<Task>);
}

template <class T, class Heap>
void testBasic() {
  T a('a'), b('b'), c('c'), d('d');
  Heap heap;
  const auto isLinked = [](T& x) {
    return Heap::NodeTraits::asNode(&x)->isLinked();
  };

  EXPECT_FALSE(isLinked(a));
  heap.push(&a);
  EXPECT_TRUE(isLinked(a));
  heap.push(&b);
  heap.push(&c);
  heap.push(&d);
  b.pri = 3;
  heap.update(&b);
  EXPECT_TRUE(isLinked(b));
  EXPECT_EQ(heap.top(), &b) << heap;
  d.pri = 4;
  heap.update(&d);
  EXPECT_EQ(heap.top(), &d) << heap;
  d.pri = 2;
  heap.update(&d);
  EXPECT_EQ(heap.pop(), &b) << heap;
  EXPECT_FALSE(isLinked(b));
  a.pri = 1;
  heap.update(&a);
  EXPECT_EQ(heap.pop(), &d) << heap;
  EXPECT_FALSE(isLinked(d));
  EXPECT_EQ(heap.pop(), &a) << heap;
  EXPECT_FALSE(isLinked(a));
  EXPECT_EQ(heap.pop(), &c) << heap;
  EXPECT_FALSE(isLinked(c));
  EXPECT_EQ(heap.top(), nullptr);
}

TEST(IntrusiveHeap, BasicDerived) {
  testBasic<Task, IntrusiveHeap<Task, std::less<>, A>>();
}

TEST(IntrusiveHeap, BasicComposition) {
  testBasic<
      TaskWithComposition,
      IntrusiveHeap<
          TaskWithComposition,
          std::less<>,
          void,
          TaskWithComposition::NodeTraits>>();
}

TEST(IntrusiveHeap, Fuzz) {
  std::default_random_engine rng(1729); // Deterministic seed.
  std::vector<std::unique_ptr<TrackedTask>> tasks;

  IntrusiveHeap<TrackedTask, std::less<>, A> aHeap;
  IntrusiveHeap<TrackedTask, std::less<>, B> bHeap;
  for (char id = 'a'; id <= 'z'; ++id) {
    tasks.push_back(std::make_unique<TrackedTask>(id));
  }
  for (auto i = FLAGS_fuzz_count; i-- > 0;) {
    auto item = tasks[folly::Random::rand32(0, tasks.size(), rng)].get();
    const auto fuzzHeap = [&](auto& heap) {
      using Heap = std::decay_t<decltype(heap)>;
      if (folly::Random::oneIn(2, rng)) {
        if (bool& contained = item->in(heap)) {
          VLOG(1) << "removing " << *item << " from " << heap;
          EXPECT_TRUE(Heap::NodeTraits::asNode(item)->isLinked());
          heap.erase(item);
          EXPECT_FALSE(Heap::NodeTraits::asNode(item)->isLinked());
          contained = false;
        } else {
          VLOG(1) << "pushing " << *item << " to " << heap;
          heap.push(item);
          contained = true;
        }
      }
      IntrusiveHeapTest::check(heap);
      VLOG(1) << heap;
      if (folly::Random::oneIn(50, rng)) {
        const auto compare = [](auto& a, auto& b) { return *a < *b; };
        std::sort(tasks.begin(), tasks.end(), compare);
        if (TrackedTask* top = heap.top()) {
          auto lb = std::lower_bound(tasks.begin(), tasks.end(), top, compare);
          ASSERT_TRUE(lb != tasks.end());
          for (auto it = lb; it != tasks.end(); ++it) {
            auto t = it->get();
            if (t->in(heap)) {
              ASSERT_EQ(top->pri, t->pri);
            }
          }
        }
      }
    };
    if (folly::Random::oneIn(2, rng)) {
      fuzzHeap(aHeap);
    } else {
      fuzzHeap(bHeap);
    }
    const auto dp = static_cast<int>(folly::Random::rand32(0, 7, rng)) - 3;
    VLOG(1) << "adjusting " << *item << " by " << dp;
    item->pri += dp;
    if (item->in(aHeap)) {
      aHeap.update(item);
      IntrusiveHeapTest::check(aHeap);
      VLOG(1) << aHeap;
    }
    if (item->in(bHeap)) {
      bHeap.update(item);
      IntrusiveHeapTest::check(bHeap);
      VLOG(1) << bHeap;
    }
  }
}

} // namespace folly
