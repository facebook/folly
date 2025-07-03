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

#pragma once

#include <algorithm>
#include <iterator>

#include <folly/lang/Builtin.h>

namespace folly {

/**
 * Companion to std::push/pop_heap(). Restores the heap property if the heap's
 * top is modified.
 */
template <class RandomIt, class Compare>
void down_heap(RandomIt first, RandomIt last, Compare comp) {
  size_t size = last - first;
  size_t parent = 0;
  size_t child;
  // Iterate while both left and right children exist.
  while ((child = 2 * parent + 2) < size) {
    // Find the max among the two children.
    child = FOLLY_BUILTIN_UNPREDICTABLE(comp(first[child], first[child - 1]))
        ? child - 1
        : child;
    if (comp(first[parent], first[child])) {
      std::iter_swap(first + parent, first + child);
      parent = child;
    } else {
      return;
    }
  }

  // Now parent can have either no children or only a left child.
  if (--child < size && comp(first[parent], first[child])) {
    std::iter_swap(first + parent, first + child);
  }
}

template <class RandomIt>
void down_heap(RandomIt first, RandomIt last) {
  down_heap(first, last, std::less<>{});
}

} // namespace folly
