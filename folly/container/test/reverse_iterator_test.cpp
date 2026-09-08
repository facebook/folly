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

#include <folly/container/reverse_iterator.h>

#include <iterator>
#include <list>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/portability/GTest.h>

namespace {

using IntIter = folly::reverse_iterator<int*>;
using ConstIntIter = folly::reverse_iterator<const int*>;

// The whole point of this type: std::reverse_iterator is not trivially
// copyable on either libstdc++ or libc++.
static_assert(std::is_trivially_copyable_v<IntIter>);
static_assert(std::is_trivially_destructible_v<IntIter>);
static_assert(sizeof(IntIter) == sizeof(int*));

static_assert(std::random_access_iterator<IntIter>);
static_assert(!std::contiguous_iterator<IntIter>);
static_assert(std::same_as<std::iter_value_t<IntIter>, int>);
static_assert(std::same_as<std::iter_reference_t<IntIter>, int&>);
static_assert(std::same_as<
              std::iterator_traits<IntIter>::iterator_category,
              std::random_access_iterator_tag>);

static_assert(std::convertible_to<IntIter, ConstIntIter>);
static_assert(!std::convertible_to<ConstIntIter, IntIter>);

using ListIter = folly::reverse_iterator<std::list<int>::iterator>;
static_assert(std::bidirectional_iterator<ListIter>);
static_assert(!std::random_access_iterator<ListIter>);

TEST(ReverseIterator, BaseIsOnePastTheElement) {
  int a[] = {0, 1, 2};
  IntIter it{a + 3};
  EXPECT_EQ(a + 3, it.base());
  EXPECT_EQ(2, *it);
  EXPECT_EQ(a + 2, &*it);
}

TEST(ReverseIterator, Traversal) {
  std::vector<int> v(5);
  std::iota(v.begin(), v.end(), 0);

  std::vector<int> seen;
  for (IntIter it{v.data() + v.size()}; it != IntIter{v.data()}; ++it) {
    seen.push_back(*it);
  }
  EXPECT_EQ(std::vector<int>({4, 3, 2, 1, 0}), seen);
}

TEST(ReverseIterator, Arithmetic) {
  int a[] = {0, 1, 2, 3, 4};
  IntIter first{a + 5};
  IntIter last{a};

  EXPECT_EQ(5, last - first);
  EXPECT_EQ(4, first[0]);
  EXPECT_EQ(0, first[4]);
  EXPECT_EQ(2, *(first + 2));
  EXPECT_EQ(2, *(2 + first));
  EXPECT_EQ(0, *(last - 1));

  IntIter it = first;
  it += 3;
  EXPECT_EQ(1, *it);
  it -= 2;
  EXPECT_EQ(3, *it);
  EXPECT_EQ(3, *it++);
  EXPECT_EQ(2, *it);
  EXPECT_EQ(2, *it--);
  EXPECT_EQ(3, *it);
}

TEST(ReverseIterator, Comparison) {
  int a[] = {0, 1, 2};
  IntIter first{a + 3};
  IntIter last{a};

  EXPECT_TRUE(first < last);
  EXPECT_TRUE(first <= last);
  EXPECT_TRUE(last > first);
  EXPECT_TRUE(last >= first);
  EXPECT_TRUE(first != last);
  EXPECT_TRUE(first == IntIter{a + 3});

  // Heterogeneous, as with iterator vs const_iterator.
  ConstIntIter cfirst = first;
  EXPECT_TRUE(cfirst == first);
  EXPECT_TRUE(cfirst < last);
  EXPECT_EQ(3, last - cfirst);
}

TEST(ReverseIterator, Arrow) {
  std::pair<int, int> a[] = {{0, 1}, {2, 3}};
  folly::reverse_iterator<std::pair<int, int>*> it{a + 2};
  EXPECT_EQ(2, it->first);
  EXPECT_EQ(3, it->second);
}

TEST(ReverseIterator, MakeAndDeduce) {
  int a[] = {0, 1, 2};
  auto it = folly::make_reverse_iterator(a + 3);
  static_assert(std::same_as<decltype(it), IntIter>);
  EXPECT_EQ(2, *it);

  folly::reverse_iterator deduced{a + 3};
  static_assert(std::same_as<decltype(deduced), IntIter>);
  EXPECT_EQ(2, *deduced);
}

TEST(ReverseIterator, MatchesStd) {
  std::vector<int> v(6);
  std::iota(v.begin(), v.end(), 0);

  auto ours = folly::reverse_iterator{v.data() + v.size()};
  auto theirs = std::reverse_iterator{v.data() + v.size()};
  for (std::size_t i = 0; i < v.size(); ++i, ++ours, ++theirs) {
    EXPECT_EQ(*theirs, *ours) << "at " << i;
    EXPECT_EQ(theirs.base(), ours.base()) << "at " << i;
  }
}

TEST(ReverseIterator, DefaultConstructed) {
  IntIter it;
  EXPECT_EQ(nullptr, it.base());
  EXPECT_EQ(it, IntIter{});
}

} // namespace
