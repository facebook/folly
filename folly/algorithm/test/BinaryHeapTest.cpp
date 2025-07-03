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
#include <numeric>
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
