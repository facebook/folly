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

#include <folly/algorithm/BinaryHeap.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include <folly/portability/GTest.h>

TEST(BinaryHeap, DownHeap) {
  std::vector<int> heap(16);
  std::iota(heap.begin(), heap.end(), 1);
  std::make_heap(heap.begin(), heap.end());

  // To test heaps of different sizes, decrement the top, remove 0, and adjust
  // until we get to an empty heap.
  while (!heap.empty()) {
    ASSERT_TRUE(std::is_heap(heap.begin(), heap.end()));
    if (heap.front() > 1) {
      --heap.front();
    } else {
      std::swap(heap.front(), heap.back());
      heap.pop_back();
    }
    folly::down_heap(heap.begin(), heap.end());
  }

  // Empty heap is still a heap.
  folly::down_heap(heap.begin(), heap.end());
}

namespace {
// Move-only element: each Item owns a heap-allocated payload (its id). Making
// it non-copyable verifies down_heap never copies elements, and a moved-from
// hole is observable as a null payload. The comparator asserts both operands
// are valid, so any comparison against a moved-from hole is caught immediately;
// and after every down_heap each live element must still hold its payload. A
// buggy move-based sift (use-after-move hole, self-move, double-free) thus
// surfaces here or as an ASAN fault.
struct Item {
  int key;
  int id;
  std::unique_ptr<int> payload;
  bool operator<(const Item& o) const {
    EXPECT_TRUE(payload) << "compared a moved-from hole (lhs)";
    EXPECT_TRUE(o.payload) << "compared a moved-from hole (rhs)";
    return key < o.key;
  }
};

void checkIntact(const std::vector<Item>& heap) {
  for (const auto& it : heap) {
    ASSERT_TRUE(it.payload) << "moved-from element left in heap";
    ASSERT_EQ(*it.payload, it.id) << "payload corrupted";
  }
}
} // namespace

// Stress down_heap with a non-trivially-movable element type to exercise the
// move-based hole sift-down: random heaps, repeated top mutation/removal, with
// heap-property, live-set, and payload-integrity checks (run under ASAN).
TEST(BinaryHeap, DownHeapMoveStressNonTrivial) {
  std::mt19937 rng(0xC0FFEE);
  for (int trial = 0; trial < 3000; ++trial) {
    int n = 1 + static_cast<int>(rng() % 64);
    std::vector<Item> heap;
    std::multiset<int> liveIds;
    for (int i = 0; i < n; ++i) {
      heap.push_back(
          Item{static_cast<int>(rng() % 1000), i, std::make_unique<int>(i)});
      liveIds.insert(i);
    }
    std::make_heap(heap.begin(), heap.end());

    while (!heap.empty()) {
      ASSERT_TRUE(std::is_heap(heap.begin(), heap.end()));
      checkIntact(heap);
      std::multiset<int> got;
      for (const auto& it : heap) {
        got.insert(it.id);
      }
      ASSERT_EQ(got, liveIds);

      if (rng() % 3 == 0) {
        std::swap(heap.front(), heap.back());
        liveIds.erase(liveIds.find(heap.back().id));
        heap.pop_back();
      } else {
        heap.front().key = static_cast<int>(rng() % 1000);
      }
      folly::down_heap(heap.begin(), heap.end());
    }
  }
}
