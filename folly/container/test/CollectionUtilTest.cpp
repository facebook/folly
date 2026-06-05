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

#include <folly/container/CollectionUtil.h>

#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <gtest/gtest.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/json/dynamic.h>
#include <folly/small_vector.h>
#include <folly/sorted_vector_types.h>

using namespace folly;

TEST(CollectionUtilTest, simpleContains) {
  auto map_test = [](auto&& m) {
    m["key"] = "value";
    EXPECT_TRUE(folly::contains(m, "key"));
    EXPECT_FALSE(folly::contains(m, "value"));
  };
  map_test(std::map<std::string, std::string>{});
  map_test(std::unordered_map<std::string, std::string>{});
  map_test(folly::F14FastMap<std::string, std::string>{});
  map_test(folly::sorted_vector_map<std::string, std::string>{});

  // test set types
  auto set_test = [](auto&& s) {
    s.insert("key");
    EXPECT_TRUE(folly::contains(s, "key"));
    EXPECT_FALSE(folly::contains(s, "value"));
  };
  set_test(std::set<std::string>{});
  set_test(std::unordered_set<std::string>{});
  set_test(folly::F14FastSet<std::string>{});
  set_test(folly::sorted_vector_set<std::string>{});
}

TEST(CollectionUtilTest, hasContains) {
  static_assert(detail::HasContains<std::map<int, int>, int>);
  static_assert(detail::HasContains<std::unordered_map<int, int>, int>);
  static_assert(detail::HasContains<folly::F14FastMap<int, int>, int>);
  static_assert(detail::HasContains<folly::sorted_vector_map<int, int>, int>);
  static_assert(detail::HasContains<std::set<int>, int>);
  static_assert(detail::HasContains<std::unordered_set<int>, int>);
  static_assert(detail::HasContains<folly::F14FastSet<int>, int>);
  static_assert(detail::HasContains<folly::sorted_vector_set<int>, int>);

  static_assert(
      !detail::HasContains<std::vector<int>, int> &&
      !detail::HasFind<std::vector<int>, int>);
  static_assert(
      !detail::HasContains<folly::small_vector<int>, int> &&
      !detail::HasFind<folly::small_vector<int>, int>);
  static_assert(
      !detail::HasContains<folly::small_vector<int>, int> &&
      !detail::HasFind<folly::small_vector<int>, int>);
  static_assert(
      !detail::HasContains<std::list<int>, int> &&
      !detail::HasFind<std::list<int>, int>);
  static_assert(
      !detail::HasContains<std::deque<int>, int> &&
      !detail::HasFind<std::deque<int>, int>);
}

// Container with key_type but no .contains() or .find().
// Exercises the fallback path in the key_type overload of contains().
template <typename K>
class HasKeyTypeOnly {
 public:
  using key_type = K;
  using value_type = K;
  using const_iterator = typename std::vector<K>::const_iterator;
  const_iterator begin() const { return data_.begin(); }
  const_iterator end() const { return data_.end(); }
  void push_back(const K& v) { data_.push_back(v); }

 private:
  std::vector<K> data_;
};

template <typename K, typename V>
class OnlyHasFind {
 public:
  using const_iterator = typename std::map<K, V>::const_iterator;
  void insert(K key, V value) {
    map_.emplace(std::move(key), std::move(value));
  }

  const_iterator find(const K& key) const { return map_.find(key); }
  const_iterator end() const { return map_.end(); }

 private:
  std::map<K, V> map_;
};

TEST(CollectionUtilTest, hasFind) {
  static_assert(
      detail::HasFind<OnlyHasFind<int, int>, int> &&
      !detail::HasContains<OnlyHasFind<int, int>, int>);

  OnlyHasFind<int, int> myMap;
  myMap.insert(1, 2);
  EXPECT_TRUE(folly::contains(myMap, 1));
  EXPECT_FALSE(folly::contains(myMap, 2));

  OnlyHasFind<int, int> myMap2;
  EXPECT_FALSE(folly::contains(myMap, 0));
}

TEST(CollectionUtilTest, dynamicContains) {
  // folly::dynamic has unusual semantics. Test contains() explicitly with
  // dynamic maps and arrays.

  dynamic obj = dynamic::object("a", 1)("b", 2)("c", 3);
  EXPECT_TRUE(contains(obj, "b"));
  EXPECT_FALSE(contains(obj, "d"));

  dynamic arr = dynamic::array(1, 3, 5, 7);
  EXPECT_TRUE(contains(arr, 5));
  EXPECT_FALSE(contains(arr, 6));
}
