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

#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <folly/Range.h>
#include <folly/Utility.h>
#include <folly/memory/Malloc.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/small_vector.h>
#include <folly/sorted_vector_types.h>

using folly::sorted_vector_map;
using folly::sorted_vector_set;

namespace {

template <class T>
struct less_invert {
  bool operator()(const T& a, const T& b) const { return b < a; }
};

template <class Container>
void check_invariant(Container& c) {
  auto it = c.begin();
  auto end = c.end();
  if (it == end) {
    return;
  }
  auto prev = it;
  ++it;
  for (; it != end; ++it, ++prev) {
    EXPECT_TRUE(c.value_comp()(*prev, *it));
  }
}

struct OneAtATimePolicy {
  template <class Container>
  void increase_capacity(Container& c) {
    if (c.size() == c.capacity()) {
      c.reserve(c.size() + 1);
    }
  }
};

struct CountCopyCtor {
  explicit CountCopyCtor() : val_(0), count_(0) {}

  explicit CountCopyCtor(int val) : val_(val), count_(0) {}

  CountCopyCtor(const CountCopyCtor& c) noexcept
      : val_(c.val_), count_(c.count_ + 1) {
    ++gCount_;
  }

  CountCopyCtor& operator=(const CountCopyCtor&) = default;

  bool operator<(const CountCopyCtor& o) const { return val_ < o.val_; }

  int val_;
  int count_;
  static int gCount_;
};

int CountCopyCtor::gCount_ = 0;

struct KeyCopiedException : public std::exception {};
/**
 * Key that may throw on copy when throwOnCopy is set, but never on move.
 * Use clone() to copy without throwing.
 */
struct KeyThatThrowsOnCopies {
  int32_t key{};
  bool throwOnCopy{};

  KeyThatThrowsOnCopies() {}

  /* implicit */ KeyThatThrowsOnCopies(int32_t key) noexcept
      : key(key), throwOnCopy(false) {}
  KeyThatThrowsOnCopies(int32_t key, bool throwOnCopy) noexcept
      : key(key), throwOnCopy(throwOnCopy) {}

  ~KeyThatThrowsOnCopies() noexcept {}

  KeyThatThrowsOnCopies(KeyThatThrowsOnCopies const& other)
      : key(other.key), throwOnCopy(other.throwOnCopy) {
    if (throwOnCopy) {
      throw KeyCopiedException{};
    }
  }

  KeyThatThrowsOnCopies(KeyThatThrowsOnCopies&& other) noexcept = default;

  KeyThatThrowsOnCopies& operator=(KeyThatThrowsOnCopies const& other) {
    key = other.key;
    throwOnCopy = other.throwOnCopy;
    if (throwOnCopy) {
      throw KeyCopiedException{};
    }
    return *this;
  }

  KeyThatThrowsOnCopies& operator=(KeyThatThrowsOnCopies&& other) noexcept =
      default;

  bool operator<(const KeyThatThrowsOnCopies& other) const {
    return key < other.key;
  }
};

static_assert(
    std::is_nothrow_move_constructible<KeyThatThrowsOnCopies>::value &&
        std::is_nothrow_move_assignable<KeyThatThrowsOnCopies>::value,
    "non-noexcept move-constructible or move-assignable");

} // namespace

TEST(SortedVectorTypes, SetAssignmentInitListTest) {
  sorted_vector_set<int> s{3, 4, 5};
  EXPECT_THAT(s, testing::ElementsAreArray({3, 4, 5}));
  s = {}; // empty ilist assignment
  EXPECT_THAT(s, testing::IsEmpty());
  s = {7, 8, 9}; // non-empty ilist assignment
  EXPECT_THAT(s, testing::ElementsAreArray({7, 8, 9}));
}

TEST(SortedVectorTypes, MapAssignmentInitListTest) {
  using v = std::pair<int, const char*>;
  v p = {3, "a"}, q = {4, "b"}, r = {5, "c"};
  sorted_vector_map<int, const char*> m{p, q, r};
  EXPECT_THAT(m, testing::ElementsAreArray({p, q, r}));
  m = {}; // empty ilist assignment
  EXPECT_THAT(m, testing::IsEmpty());
  m = {p, q, r}; // non-empty ilist assignment
  EXPECT_THAT(m, testing::ElementsAreArray({p, q, r}));
}

TEST(SortedVectorTypes, SimpleSetTest) {
  sorted_vector_set<int> s;
  EXPECT_TRUE(s.empty());
  for (int i = 0; i < 1000; ++i) {
    s.insert(rand() % 100000);
  }
  EXPECT_FALSE(s.empty());
  check_invariant(s);

  sorted_vector_set<int> s2;
  s2.insert(s.begin(), s.end());
  check_invariant(s2);
  EXPECT_TRUE(s == s2);

  auto it = s2.lower_bound(32);
  if (*it == 32) {
    s2.erase(it);
    it = s2.lower_bound(32);
  }
  check_invariant(s2);
  auto oldSz = s2.size();
  s2.insert(it, 32);
  EXPECT_TRUE(s2.size() == oldSz + 1);
  check_invariant(s2);

  const sorted_vector_set<int>& cs2 = s2;
  auto range = cs2.equal_range(32);
  auto lbound = cs2.lower_bound(32);
  auto ubound = cs2.upper_bound(32);
  EXPECT_TRUE(range.first == lbound);
  EXPECT_TRUE(range.second == ubound);
  EXPECT_TRUE(range.first != cs2.end());
  EXPECT_TRUE(range.second != cs2.end());
  EXPECT_TRUE(cs2.count(32) == 1);
  EXPECT_FALSE(cs2.find(32) == cs2.end());
  EXPECT_TRUE(cs2.contains(32));

  // Bad insert hint.
  s2.insert(s2.begin() + 3, 33);
  EXPECT_TRUE(s2.find(33) != s2.begin());
  EXPECT_TRUE(s2.find(33) != s2.end());
  check_invariant(s2);
  s2.erase(33);
  check_invariant(s2);

  it = s2.find(32);
  EXPECT_FALSE(it == s2.end());
  s2.erase(it);
  EXPECT_FALSE(cs2.contains(32));
  EXPECT_TRUE(s2.size() == oldSz);
  check_invariant(s2);

  sorted_vector_set<int> cpy(s);
  check_invariant(cpy);
  EXPECT_TRUE(cpy == s);
  sorted_vector_set<int> cpy2(s);
  cpy2.insert(100001);
  EXPECT_TRUE(cpy2 != cpy);
  EXPECT_TRUE(cpy2 != s);
  check_invariant(cpy2);
  EXPECT_TRUE(cpy2.count(100001) == 1);
  s.swap(cpy2);
  check_invariant(cpy2);
  check_invariant(s);
  EXPECT_TRUE(s != cpy);
  EXPECT_TRUE(s != cpy2);
  EXPECT_TRUE(cpy2 == cpy);

  sorted_vector_set<int> s3 = {};
  s3.insert({1, 2, 3});
  s3.emplace(4);
  EXPECT_EQ(s3.size(), 4);

  sorted_vector_set<std::string> s4;
  s4.emplace("foobar", 3);
  EXPECT_EQ(s4.count("foo"), 1);
}

TEST(SortedVectorTypes, TransparentSetTest) {
  using namespace folly::string_piece_literals;
  using Compare = folly::transparent<std::less<folly::StringPiece>>;

  constexpr auto buddy = "buddy"_sp;
  constexpr auto hello = "hello"_sp;
  constexpr auto stake = "stake"_sp;
  constexpr auto world = "world"_sp;
  constexpr auto zebra = "zebra"_sp;

  sorted_vector_set<std::string, Compare> const s({hello.str(), world.str()});

  // find
  EXPECT_TRUE(s.end() == s.find(buddy));
  EXPECT_EQ(hello, *s.find(hello));
  EXPECT_TRUE(s.end() == s.find(stake));
  EXPECT_EQ(world, *s.find(world));
  EXPECT_TRUE(s.end() == s.find(zebra));

  // count
  EXPECT_EQ(0, s.count(buddy));
  EXPECT_EQ(1, s.count(hello));
  EXPECT_EQ(0, s.count(stake));
  EXPECT_EQ(1, s.count(world));
  EXPECT_EQ(0, s.count(zebra));

  // contains
  EXPECT_FALSE(s.contains(buddy));
  EXPECT_TRUE(s.contains(hello));
  EXPECT_FALSE(s.contains(stake));
  EXPECT_TRUE(s.contains(world));
  EXPECT_FALSE(s.contains(zebra));

  // lower_bound
  EXPECT_TRUE(s.find(hello) == s.lower_bound(buddy));
  EXPECT_TRUE(s.find(hello) == s.lower_bound(hello));
  EXPECT_TRUE(s.find(world) == s.lower_bound(stake));
  EXPECT_TRUE(s.find(world) == s.lower_bound(world));
  EXPECT_TRUE(s.end() == s.lower_bound(zebra));

  // upper_bound
  EXPECT_TRUE(s.find(hello) == s.upper_bound(buddy));
  EXPECT_TRUE(s.find(world) == s.upper_bound(hello));
  EXPECT_TRUE(s.find(world) == s.upper_bound(stake));
  EXPECT_TRUE(s.end() == s.upper_bound(world));
  EXPECT_TRUE(s.end() == s.upper_bound(zebra));

  // equal_range
  for (auto value : {buddy, hello, stake, world, zebra}) {
    EXPECT_TRUE(
        std::make_pair(s.lower_bound(value), s.upper_bound(value)) ==
        s.equal_range(value))
        << value;
  }
}

TEST(SortedVectorTypes, BadHints) {
  for (int toInsert = -1; toInsert <= 7; ++toInsert) {
    for (int hintPos = 0; hintPos <= 4; ++hintPos) {
      sorted_vector_set<int> s;
      for (int i = 0; i <= 3; ++i) {
        s.insert(i * 2);
      }
      s.insert(s.begin() + hintPos, toInsert);
      size_t expectedSize = (toInsert % 2) == 0 ? 4 : 5;
      EXPECT_EQ(s.size(), expectedSize);
      check_invariant(s);
    }
  }
}

TEST(SortedVectorTypes, SimpleMapTest) {
  sorted_vector_map<int, float> m;
  for (int i = 0; i < 1000; ++i) {
    m[i] = i / 1000.0;
  }
  check_invariant(m);

  m[32] = 100.0;
  check_invariant(m);
  EXPECT_TRUE(m.count(32) == 1);
  EXPECT_DOUBLE_EQ(100.0, m.at(32));
  EXPECT_FALSE(m.find(32) == m.end());
  EXPECT_TRUE(m.contains(32));
  m.erase(32);
  EXPECT_TRUE(m.find(32) == m.end());
  EXPECT_FALSE(m.contains(32));
  check_invariant(m);
  EXPECT_THROW(m.at(32), std::out_of_range);

  sorted_vector_map<int, float> m2 = m;
  EXPECT_TRUE(m2 == m);
  EXPECT_FALSE(m2 != m);
  auto it = m2.lower_bound(1 << 20);
  EXPECT_TRUE(it == m2.end());
  m2.insert(it, std::make_pair(1 << 20, 10.0f));
  check_invariant(m2);
  EXPECT_TRUE(m2.count(1 << 20) == 1);
  EXPECT_TRUE(m < m2);
  EXPECT_TRUE(m <= m2);

  const sorted_vector_map<int, float>& cm = m;
  auto range = cm.equal_range(42);
  auto lbound = cm.lower_bound(42);
  auto ubound = cm.upper_bound(42);
  EXPECT_TRUE(range.first == lbound);
  EXPECT_TRUE(range.second == ubound);
  EXPECT_FALSE(range.first == cm.end());
  EXPECT_FALSE(range.second == cm.end());
  m.erase(m.lower_bound(42));
  check_invariant(m);

  sorted_vector_map<int, float> m3;
  m3.insert(m2.begin(), m2.end());
  check_invariant(m3);
  EXPECT_TRUE(m3 == m2);
  EXPECT_FALSE(m3 == m);

  sorted_vector_map<int, float> m4;
  m4.emplace(1, 2.0f);
  m4.emplace(3, 1.0f);
  m4.emplace(2, 1.5f);
  check_invariant(m4);
  EXPECT_TRUE(m4.size() == 3);

  sorted_vector_map<int, float> m5;
  for (auto& kv : m2) {
    m5.emplace(kv);
  }
  check_invariant(m5);
  EXPECT_TRUE(m5 == m2);
  EXPECT_FALSE(m5 == m);

  EXPECT_TRUE(m != m2);
  EXPECT_TRUE(m2 == m3);
  EXPECT_TRUE(m3 != m);
  m.swap(m3);
  check_invariant(m);
  check_invariant(m2);
  check_invariant(m3);
  EXPECT_TRUE(m3 != m2);
  EXPECT_TRUE(m3 != m);
  EXPECT_TRUE(m == m2);

  // Bad insert hint.
  m.insert(m.begin() + 3, std::make_pair(1 << 15, 1.0f));
  check_invariant(m);

  sorted_vector_map<int, float> m6 = {};
  m6.insert({{1, 1.0f}, {2, 2.0f}, {1, 2.0f}});
  EXPECT_EQ(m6.at(2), 2.0f);
}

TEST(SortedVectorTypes, TransparentMapTest) {
  using namespace folly::string_piece_literals;
  using Compare = folly::transparent<std::less<folly::StringPiece>>;

  constexpr auto buddy = "buddy"_sp;
  constexpr auto hello = "hello"_sp;
  constexpr auto stake = "stake"_sp;
  constexpr auto world = "world"_sp;
  constexpr auto zebra = "zebra"_sp;

  sorted_vector_map<std::string, float, Compare> const m(
      {{hello.str(), -1.}, {world.str(), +1.}});

  // find
  EXPECT_TRUE(m.end() == m.find(buddy));
  EXPECT_EQ(hello, m.find(hello)->first);
  EXPECT_TRUE(m.end() == m.find(stake));
  EXPECT_EQ(world, m.find(world)->first);
  EXPECT_TRUE(m.end() == m.find(zebra));

  // count
  EXPECT_EQ(0, m.count(buddy));
  EXPECT_EQ(1, m.count(hello));
  EXPECT_EQ(0, m.count(stake));
  EXPECT_EQ(1, m.count(world));
  EXPECT_EQ(0, m.count(zebra));

  // lower_bound
  EXPECT_TRUE(m.find(hello) == m.lower_bound(buddy));
  EXPECT_TRUE(m.find(hello) == m.lower_bound(hello));
  EXPECT_TRUE(m.find(world) == m.lower_bound(stake));
  EXPECT_TRUE(m.find(world) == m.lower_bound(world));
  EXPECT_TRUE(m.end() == m.lower_bound(zebra));

  // upper_bound
  EXPECT_TRUE(m.find(hello) == m.upper_bound(buddy));
  EXPECT_TRUE(m.find(world) == m.upper_bound(hello));
  EXPECT_TRUE(m.find(world) == m.upper_bound(stake));
  EXPECT_TRUE(m.end() == m.upper_bound(world));
  EXPECT_TRUE(m.end() == m.upper_bound(zebra));

  // equal_range
  for (auto value : {buddy, hello, stake, world, zebra}) {
    EXPECT_TRUE(
        std::make_pair(m.lower_bound(value), m.upper_bound(value)) ==
        m.equal_range(value))
        << value;
  }
}

TEST(SortedVectorTypes, Sizes) {
  EXPECT_EQ(sizeof(sorted_vector_set<int>), sizeof(std::vector<int>));
  EXPECT_EQ(
      sizeof(sorted_vector_map<int, int>),
      sizeof(std::vector<std::pair<int, int>>));

  typedef sorted_vector_set<
      int,
      std::less<int>,
      std::allocator<int>,
      OneAtATimePolicy>
      SetT;
  typedef sorted_vector_map<
      int,
      int,
      std::less<int>,
      std::allocator<std::pair<int, int>>,
      OneAtATimePolicy>
      MapT;

  EXPECT_EQ(sizeof(SetT), sizeof(std::vector<int>));
  EXPECT_EQ(sizeof(MapT), sizeof(std::vector<std::pair<int, int>>));
}

TEST(SortedVectorTypes, InitializerLists) {
  sorted_vector_set<int> empty_initialized_set{};
  EXPECT_TRUE(empty_initialized_set.empty());

  sorted_vector_set<int> singleton_initialized_set{1};
  EXPECT_EQ(1, singleton_initialized_set.size());
  EXPECT_EQ(1, *singleton_initialized_set.begin());

  sorted_vector_set<int> forward_initialized_set{1, 2};
  sorted_vector_set<int> backward_initialized_set{2, 1};
  EXPECT_EQ(2, forward_initialized_set.size());
  EXPECT_EQ(1, *forward_initialized_set.begin());
  EXPECT_EQ(2, *forward_initialized_set.rbegin());
  EXPECT_TRUE(forward_initialized_set == backward_initialized_set);

  sorted_vector_map<int, int> empty_initialized_map{};
  EXPECT_TRUE(empty_initialized_map.empty());

  sorted_vector_map<int, int> singleton_initialized_map{{1, 10}};
  EXPECT_EQ(1, singleton_initialized_map.size());
  EXPECT_EQ(10, singleton_initialized_map[1]);

  sorted_vector_map<int, int> forward_initialized_map{{1, 10}, {2, 20}};
  sorted_vector_map<int, int> backward_initialized_map{{2, 20}, {1, 10}};
  EXPECT_EQ(2, forward_initialized_map.size());
  EXPECT_EQ(10, forward_initialized_map[1]);
  EXPECT_EQ(20, forward_initialized_map[2]);
  EXPECT_TRUE(forward_initialized_map == backward_initialized_map);
}

TEST(SortedVectorTypes, CustomCompare) {
  sorted_vector_set<int, less_invert<int>> s;
  for (int i = 0; i < 200; ++i) {
    s.insert(i);
  }
  check_invariant(s);

  sorted_vector_map<int, float, less_invert<int>> m;
  for (int i = 0; i < 200; ++i) {
    m[i] = 12.0;
  }
  check_invariant(m);
}

TEST(SortedVectorTypes, GrowthPolicy) {
  typedef sorted_vector_set<
      CountCopyCtor,
      std::less<CountCopyCtor>,
      std::allocator<CountCopyCtor>,
      OneAtATimePolicy>
      SetT;

  SetT a;
  for (int i = 0; i < 20; ++i) {
    a.insert(CountCopyCtor(i));
  }
  check_invariant(a);
  SetT::iterator it = a.begin();
  EXPECT_FALSE(it == a.end());
  if (it != a.end()) {
    EXPECT_EQ(it->val_, 0);
    // 1 copy for the initial insertion, 19 more for reallocs on the
    // additional insertions.
    EXPECT_EQ(it->count_, 20);
  }

  std::list<CountCopyCtor> v;
  for (int i = 0; i < 20; ++i) {
    v.emplace_back(20 + i);
  }
  a.insert(v.begin(), v.end());
  check_invariant(a);

  it = a.begin();
  EXPECT_FALSE(it == a.end());
  if (it != a.end()) {
    EXPECT_EQ(it->val_, 0);
    // Should be only 1 more copy for inserting this above range.
    EXPECT_EQ(it->count_, 21);
  }
}

TEST(SortedVectorTest, EmptyTest) {
  sorted_vector_set<int> emptySet;
  EXPECT_TRUE(emptySet.lower_bound(10) == emptySet.end());
  EXPECT_TRUE(emptySet.find(10) == emptySet.end());

  sorted_vector_map<int, int> emptyMap;
  EXPECT_TRUE(emptyMap.lower_bound(10) == emptyMap.end());
  EXPECT_TRUE(emptyMap.find(10) == emptyMap.end());
  EXPECT_THROW(emptyMap.at(10), std::out_of_range);
}

TEST(SortedVectorTest, MoveTest) {
  sorted_vector_set<std::unique_ptr<int>> s;
  s.insert(std::make_unique<int>(5));
  s.insert(s.end(), std::make_unique<int>(10));
  EXPECT_EQ(s.size(), 2);

  for (const auto& p : s) {
    EXPECT_TRUE(*p == 5 || *p == 10);
  }

  sorted_vector_map<int, std::unique_ptr<int>> m;
  m.insert(std::make_pair(5, std::make_unique<int>(5)));
  m.insert(m.end(), std::make_pair(10, std::make_unique<int>(10)));

  EXPECT_EQ(*m[5], 5);
  EXPECT_EQ(*m[10], 10);
}

TEST(SortedVectorTest, ShrinkTest) {
  sorted_vector_set<int> s;
  int i = 0;
  // Hopefully your resize policy doubles when capacity is full, or this will
  // hang forever :(
  while (s.capacity() == s.size()) {
    s.insert(i++);
  }
  s.shrink_to_fit();
  // The standard does not actually enforce that this be true, but assume that
  // vector::shrink_to_fit respects the caller.
  EXPECT_EQ(s.capacity(), s.size());
}

TEST(SortedVectorTypes, EraseTest) {
  sorted_vector_set<int> s1;
  s1.insert(1);
  sorted_vector_set<int> s2(s1);
  EXPECT_EQ(0, s1.erase(0));
  EXPECT_EQ(s2, s1);
}

TEST(SortedVectorTypes, EraseTest2) {
  sorted_vector_set<int> s;
  for (int i = 0; i < 1000; ++i) {
    s.insert(i);
  }

  auto it = s.lower_bound(32);
  EXPECT_EQ(*it, 32);
  it = s.erase(it);
  EXPECT_NE(s.end(), it);
  EXPECT_EQ(*it, 33);
  it = s.erase(it, it + 5);
  EXPECT_EQ(*it, 38);

  it = s.begin();
  while (it != s.end()) {
    if (*it >= 5) {
      it = s.erase(it);
    } else {
      it++;
    }
  }
  EXPECT_EQ(it, s.end());
  EXPECT_EQ(s.size(), 5);

  sorted_vector_map<int, int> m;
  for (int i = 0; i < 1000; ++i) {
    m.insert(std::make_pair(i, i));
  }

  auto it2 = m.lower_bound(32);
  EXPECT_EQ(it2->first, 32);
  it2 = m.erase(it2);
  EXPECT_NE(m.end(), it2);
  EXPECT_EQ(it2->first, 33);
  it2 = m.erase(it2, it2 + 5);
  EXPECT_EQ(it2->first, 38);

  it2 = m.begin();
  while (it2 != m.end()) {
    if (it2->first >= 5) {
      it2 = m.erase(it2);
    } else {
      it2++;
    }
  }
  EXPECT_EQ(it2, m.end());
  EXPECT_EQ(m.size(), 5);
}

TEST(SortedVectorTypes, TestSetBulkInsertionSortMerge) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add an unsorted range that will have to be merged in.
  s = std::vector<int>({10, 7, 5, 1});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);

  EXPECT_THAT(vset, testing::ElementsAreArray({1, 2, 4, 5, 6, 7, 8, 10}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionMiddleValuesEqualDuplication) {
  auto s = std::vector<int>({4, 6, 8});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  s = std::vector<int>({8, 10, 12});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);

  EXPECT_THAT(vset, testing::ElementsAreArray({4, 6, 8, 10, 12}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionSortMergeDups) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add an unsorted range that will have to be merged in.
  s = std::vector<int>({10, 6, 5, 2});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({2, 4, 5, 6, 8, 10}));
}

TEST(SortedVectorTypes, TestSetInsertionDupsOneByOne) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add an unsorted range that will have to be merged in.
  s = std::vector<int>({10, 6, 5, 2});

  for (const auto& elem : s) {
    vset.insert(elem);
  }
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({2, 4, 5, 6, 8, 10}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionSortNoMerge) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add an unsorted range that will not have to be merged in.
  s = std::vector<int>({20, 15, 16, 13});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({2, 4, 6, 8, 13, 15, 16, 20}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionNoSortMerge) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add a sorted range that will have to be merged in.
  s = std::vector<int>({1, 3, 5, 9});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({1, 2, 3, 4, 5, 6, 8, 9}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionNoSortNoMerge) {
  auto s = std::vector<int>({6, 4, 8, 2});

  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  // Add a sorted range that will not have to be merged in.
  s = std::vector<int>({21, 22, 23, 24});

  vset.insert(s.begin(), s.end());
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({2, 4, 6, 8, 21, 22, 23, 24}));
}

TEST(SortedVectorTypes, TestSetBulkInsertionEmptyRange) {
  std::vector<int> s;
  EXPECT_TRUE(s.empty());

  // insertion of empty range into empty container.
  sorted_vector_set<int> vset(s.begin(), s.end());
  check_invariant(vset);

  s = std::vector<int>({6, 4, 8, 2});

  vset.insert(s.begin(), s.end());

  // insertion of empty range into non-empty container.
  s.clear();
  vset.insert(s.begin(), s.end());
  check_invariant(vset);

  EXPECT_THAT(vset, testing::ElementsAreArray({2, 4, 6, 8}));
}

// This is a test of compilation - the behavior has already been tested
// extensively above.
TEST(SortedVectorTypes, TestBulkInsertionUncopyableTypes) {
  std::vector<std::pair<int, std::unique_ptr<int>>> s;
  s.emplace_back(1, std::make_unique<int>(0));

  sorted_vector_map<int, std::unique_ptr<int>> vmap(
      std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));

  s.clear();
  s.emplace_back(3, std::make_unique<int>(0));
  vmap.insert(
      std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
}

// A moveable and copyable struct, which we use to make sure that no copy
// operations are performed during bulk insertion if moving is an option.
struct Movable {
  int x_;
  explicit Movable(int x) : x_(x) {}
  Movable(const Movable&) { ADD_FAILURE() << "Copy ctor should not be called"; }
  Movable& operator=(const Movable&) {
    ADD_FAILURE() << "Copy assignment should not be called";
    return *this;
  }

  Movable(Movable&&) = default;
  Movable& operator=(Movable&&) = default;
};

TEST(SortedVectorTypes, TestBulkInsertionMovableTypes) {
  std::vector<std::pair<int, Movable>> s;
  s.emplace_back(3, Movable(2));
  s.emplace_back(1, Movable(0));

  sorted_vector_map<int, Movable> vmap(
      std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));

  s.clear();
  s.emplace_back(4, Movable(3));
  s.emplace_back(2, Movable(1));
  vmap.insert(
      std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
}

TEST(SortedVectorTypes, TestSetCreationFromVector) {
  std::vector<int> vec = {3, 1, -1, 5, 0};
  sorted_vector_set<int> vset(std::move(vec));
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({-1, 0, 1, 3, 5}));
}

TEST(SortedVectorTypes, TestMapCreationFromVector) {
  std::vector<std::pair<int, int>> vec = {
      {3, 1}, {1, 5}, {-1, 2}, {5, 3}, {0, 3}};
  sorted_vector_map<int, int> vmap(std::move(vec));
  check_invariant(vmap);
  auto contents = std::vector<std::pair<int, int>>(vmap.begin(), vmap.end());
  auto expected_contents = std::vector<std::pair<int, int>>({
      {-1, 2},
      {0, 3},
      {1, 5},
      {3, 1},
      {5, 3},
  });
  EXPECT_EQ(contents, expected_contents);
}

TEST(SortedVectorTypes, TestSetCreationFromSmallVector) {
  using smvec = folly::small_vector<int, 5>;
  smvec vec = {3, 1, -1, 5, 0};
  sorted_vector_set<
      int,
      std::less<int>,
      std::allocator<std::pair<int, int>>,
      void,
      smvec>
      vset(std::move(vec));
  check_invariant(vset);
  EXPECT_THAT(vset, testing::ElementsAreArray({-1, 0, 1, 3, 5}));
}

TEST(SortedVectorTypes, TestMapCreationFromSmallVector) {
  using smvec = folly::small_vector<std::pair<int, int>, 5>;
  smvec vec = {{3, 1}, {1, 5}, {-1, 2}, {5, 3}, {0, 3}};
  sorted_vector_map<
      int,
      int,
      std::less<int>,
      std::allocator<std::pair<int, int>>,
      void,
      smvec>
      vmap(std::move(vec));
  check_invariant(vmap);
  auto contents = std::vector<std::pair<int, int>>(vmap.begin(), vmap.end());
  auto expected_contents = std::vector<std::pair<int, int>>({
      {-1, 2},
      {0, 3},
      {1, 5},
      {3, 1},
      {5, 3},
  });
  EXPECT_EQ(contents, expected_contents);
}

TEST(SortedVectorTypes, TestBulkInsertionWithDuplicatesIntoEmptySet) {
  sorted_vector_set<int> set;
  {
    std::vector<int> const vec = {0, 1, 0, 1};
    set.insert(vec.begin(), vec.end());
  }
  EXPECT_THAT(set, testing::ElementsAreArray({0, 1}));
}

TEST(SortedVectorTypes, TestDataPointsToFirstElement) {
  sorted_vector_set<int> set;
  sorted_vector_map<int, int> map;

  set.insert(0);
  map[0] = 0;
  EXPECT_EQ(set.data(), &*set.begin());
  EXPECT_EQ(map.data(), &*map.begin());

  set.insert(1);
  map[1] = 1;
  EXPECT_EQ(set.data(), &*set.begin());
  EXPECT_EQ(map.data(), &*map.begin());
}

TEST(SortedVectorTypes, TestEmplaceHint) {
  sorted_vector_set<std::pair<int, int>> set;
  sorted_vector_map<int, int> map;

  for (size_t i = 0; i < 4; ++i) {
    const std::pair<int, int> k00(0, 0);
    const std::pair<int, int> k0i(0, i % 2);
    const std::pair<int, int> k10(1, 0);
    const std::pair<int, int> k1i(1, i % 2);
    const std::pair<int, int> k20(2, 0);
    const std::pair<int, int> k2i(2, i % 2);

    EXPECT_EQ(*set.emplace_hint(set.begin(), 0, i % 2), k0i);
    EXPECT_EQ(*map.emplace_hint(map.begin(), 0, i % 2), k00);

    EXPECT_EQ(*set.emplace_hint(set.begin(), k1i), k1i);
    EXPECT_EQ(*map.emplace_hint(map.begin(), k1i), k10);

    EXPECT_EQ(*set.emplace_hint(set.begin(), folly::copy(k2i)), k2i);
    EXPECT_EQ(*map.emplace_hint(map.begin(), folly::copy(k2i)), k20);

    check_invariant(set);
    check_invariant(map);
  }
}

TEST(SortedVectorTypes, TestExceptionSafety) {
  std::initializer_list<KeyThatThrowsOnCopies> const sortedUnique = {
      0, 1, 4, 7, 9, 11, 15};
  sorted_vector_set<KeyThatThrowsOnCopies> set = {sortedUnique};
  EXPECT_EQ(set.size(), 7);

  // Verify that we successfully insert when no exceptions are thrown.
  KeyThatThrowsOnCopies key1(96, false);
  auto hint1 = set.find(96);
  set.insert(hint1, key1);
  EXPECT_EQ(set.size(), 8);

  // Verify that we don't add a key at the end if copying throws
  KeyThatThrowsOnCopies key2(99, true);
  auto hint2 = set.find(99);
  try {
    set.insert(hint2, key2);
  } catch (const KeyCopiedException&) {
    // swallow
  }
  EXPECT_EQ(set.size(), 8);

  // Verify that we don't add a key in the middle if copying throws
  KeyThatThrowsOnCopies key3(47, true);
  auto hint3 = set.find(47);
  try {
    set.insert(hint3, key3);
  } catch (const KeyCopiedException&) {
    // swallow
  }
  EXPECT_EQ(set.size(), 8);
}

#if FOLLY_HAS_MEMORY_RESOURCE

using folly::detail::std_pmr::memory_resource;
using folly::detail::std_pmr::new_delete_resource;
using folly::detail::std_pmr::null_memory_resource;
using folly::detail::std_pmr::polymorphic_allocator;

namespace {

struct test_resource : public memory_resource {
  void* do_allocate(size_t bytes, size_t /* alignment */) override {
    return folly::checkedMalloc(bytes);
  }

  void do_deallocate(
      void* p, size_t /* bytes */, size_t /* alignment */) noexcept override {
    free(p);
  }

  bool do_is_equal(const memory_resource& other) const noexcept override {
    return this == &other;
  }
};

} // namespace

TEST(SortedVectorTypes, TestPmrAllocatorSimple) {
  namespace pmr = folly::pmr;

  pmr::sorted_vector_set<std::pair<int, int>> s(null_memory_resource());
  EXPECT_THROW(s.emplace(42, 42), std::bad_alloc);

  pmr::sorted_vector_map<int, int> m(null_memory_resource());
  EXPECT_THROW(m.emplace(42, 42), std::bad_alloc);
}

TEST(SortedVectorTypes, TestPmrCopyConstructSameAlloc) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());

  test_resource r;
  polymorphic_allocator<std::byte> a1(&r), a2(&r);
  EXPECT_EQ(a1, a2);

  {
    pmr::sorted_vector_set<int> s1(a1);
    s1.emplace(42);

    pmr::sorted_vector_set<int> s2(s1, a2);
    EXPECT_EQ(s1.get_allocator(), s2.get_allocator());
    EXPECT_EQ(s2.count(42), 1);
  }

  {
    pmr::sorted_vector_map<int, int> m1(a1);
    m1.emplace(42, 42);

    pmr::sorted_vector_map<int, int> m2(m1, a2);
    EXPECT_EQ(m1.get_allocator(), m2.get_allocator());
    EXPECT_EQ(m2.at(42), 42);
  }
}

TEST(SortedVectorTypes, TestPmrCopyConstructDifferentAlloc) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());

  test_resource r1, r2;
  polymorphic_allocator<std::byte> a1(&r1), a2(&r2);
  EXPECT_NE(a1, a2);

  {
    pmr::sorted_vector_set<int> s1(a1);
    s1.emplace(42);

    pmr::sorted_vector_set<int> s2(s1, a2);
    EXPECT_NE(s1.get_allocator(), s2.get_allocator());
    EXPECT_EQ(s2.count(42), 1);
  }

  {
    pmr::sorted_vector_map<int, int> m1(a1);
    m1.emplace(42, 42);

    pmr::sorted_vector_map<int, int> m2(m1, a2);
    EXPECT_NE(m1.get_allocator(), m2.get_allocator());
    EXPECT_EQ(m2.at(42), 42);
  }
}

TEST(SortedVectorTypes, TestPmrMoveConstructSameAlloc) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());

  test_resource r;
  polymorphic_allocator<std::byte> a1(&r), a2(&r);
  EXPECT_EQ(a1, a2);

  {
    pmr::sorted_vector_set<int> s1(a1);
    s1.emplace(42);
    auto d = s1.data();

    pmr::sorted_vector_set<int> s2(std::move(s1), a2);
    EXPECT_EQ(s1.get_allocator(), s2.get_allocator());
    EXPECT_EQ(s2.data(), d);
    EXPECT_EQ(s2.count(42), 1);
  }

  {
    pmr::sorted_vector_map<int, int> m1(a1);
    m1.emplace(42, 42);
    auto d = m1.data();

    pmr::sorted_vector_map<int, int> m2(std::move(m1), a2);
    EXPECT_EQ(m1.get_allocator(), m2.get_allocator());
    EXPECT_EQ(m2.data(), d);
    EXPECT_EQ(m2.at(42), 42);
  }
}

TEST(SortedVectorTypes, TestPmrMoveConstructDifferentAlloc) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());

  test_resource r1, r2;
  polymorphic_allocator<std::byte> a1(&r1), a2(&r2);
  EXPECT_NE(a1, a2);

  {
    pmr::sorted_vector_set<int> s1(a1);
    s1.emplace(42);
    auto d = s1.data();

    pmr::sorted_vector_set<int> s2(std::move(s1), a2);
    EXPECT_NE(s1.get_allocator(), s2.get_allocator());
    EXPECT_NE(s2.data(), d);
    EXPECT_EQ(s2.count(42), 1);
  }

  {
    pmr::sorted_vector_map<int, int> m1(a1);
    m1.emplace(42, 42);
    auto d = m1.data();

    pmr::sorted_vector_map<int, int> m2(std::move(m1), a2);
    EXPECT_NE(m1.get_allocator(), m2.get_allocator());
    EXPECT_NE(m2.data(), d);
    EXPECT_EQ(m2.at(42), 42);
  }
}

template <typename T>
using pmr_vector =
    std::vector<T, folly::detail::std_pmr::polymorphic_allocator<T>>;

TEST(SortedVectorTypes, TestCreationFromPmrVector) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());
  test_resource r;
  polymorphic_allocator<std::byte> a(&r);

  {
    pmr_vector<int> c({1, 2, 3}, a);
    auto d = c.data();
    pmr::sorted_vector_set<int> s(std::move(c));
    EXPECT_EQ(s.get_allocator(), a);
    EXPECT_EQ(s.data(), d);
  }

  {
    pmr_vector<int> c({2, 1, 3}, a);
    auto d = c.data();
    pmr::sorted_vector_set<int> s(std::move(c));
    EXPECT_EQ(s.get_allocator(), a);
    EXPECT_EQ(s.data(), d);
  }

  {
    pmr_vector<std::pair<int, int>> c({{1, 1}, {2, 2}, {3, 3}}, a);
    auto d = c.data();
    pmr::sorted_vector_map<int, int> m(std::move(c));
    EXPECT_EQ(m.get_allocator(), a);
    EXPECT_EQ(m.data(), d);
  }

  {
    pmr_vector<std::pair<int, int>> c({{2, 2}, {1, 1}, {3, 3}}, a);
    auto d = c.data();
    pmr::sorted_vector_map<int, int> m(std::move(c));
    EXPECT_EQ(m.get_allocator(), a);
    EXPECT_EQ(m.data(), d);
  }
}

TEST(SortedVectorTypes, TestPmrAllocatorScoped) {
  namespace pmr = folly::pmr;

  set_default_resource(null_memory_resource());
  polymorphic_allocator<std::byte> alloc(new_delete_resource());

  {
    pmr::sorted_vector_set<pmr_vector<int>> s(alloc);
    s.emplace(1);
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_set<pmr_vector<int>> s(alloc);
    s.emplace_hint(s.begin(), 1);
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_map<int, pmr_vector<int>> m(alloc);
    m.emplace(1, 1);
    EXPECT_EQ(m.begin()->second.get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_map<int, pmr_vector<int>> m(alloc);
    m.emplace_hint(m.begin(), 1, 1);
    EXPECT_EQ(m.begin()->second.get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_set<pmr::sorted_vector_map<int, int>> s(alloc);
    s.emplace(std::initializer_list<std::pair<int, int>>{{42, 42}});
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_set<pmr::sorted_vector_map<int, int>> s(alloc);
    s.emplace_hint(
        s.begin(), std::initializer_list<std::pair<int, int>>{{42, 42}});
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_set<pmr::sorted_vector_set<int>> s(alloc);
    s.emplace(std::initializer_list<int>{1});
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_set<pmr::sorted_vector_set<int>> s(alloc);
    s.emplace_hint(s.begin(), std::initializer_list<int>{1});
    EXPECT_EQ(s.begin()->get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_map<int, pmr::sorted_vector_map<int, int>> m(alloc);
    m.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(42),
        std::forward_as_tuple(
            std::initializer_list<std::pair<int, int>>{{42, 42}}));
    EXPECT_EQ(m.begin()->second.get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_map<int, pmr::sorted_vector_map<int, int>> m(alloc);
    m.emplace_hint(
        m.begin(),
        std::piecewise_construct,
        std::forward_as_tuple(42),
        std::forward_as_tuple(
            std::initializer_list<std::pair<int, int>>{{42, 42}}));
    EXPECT_EQ(m.begin()->second.get_allocator(), alloc);
  }

  {
    pmr::sorted_vector_map<int, pmr::sorted_vector_map<int, int>> m(alloc);
    m[42][42] = 42;
    EXPECT_EQ(m.begin()->second.get_allocator(), alloc);
  }
}

#endif

TEST(SortedVectorTypes, TestInsertHintCopy) {
  sorted_vector_set<CountCopyCtor> set;
  sorted_vector_map<CountCopyCtor, int> map;
  CountCopyCtor skey;
  std::pair<CountCopyCtor, int> mkey;

  CountCopyCtor::gCount_ = 0;
  set.insert(set.end(), skey);
  map.insert(map.end(), mkey);
  EXPECT_EQ(CountCopyCtor::gCount_, 2);

  set.emplace(CountCopyCtor(1));
  map.emplace(CountCopyCtor(1), 1);

  CountCopyCtor::gCount_ = 0;
  for (size_t i = 0; i <= set.size(); ++i) {
    auto sit = set.begin();
    auto mit = map.begin();
    std::advance(sit, i);
    std::advance(mit, i);
    set.insert(sit, skey);
    map.insert(mit, mkey);
  }
  EXPECT_EQ(CountCopyCtor::gCount_, 0);
}
