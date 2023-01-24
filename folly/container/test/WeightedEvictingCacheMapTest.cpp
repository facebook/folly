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

#include <folly/container/WeightedEvictingCacheMap.h>

#include <string>

#include <folly/portability/GTest.h>

using namespace folly;

struct ValueStringLength {
  std::size_t operator()(const std::string& /*key*/, const std::string& value) {
    return value.size();
  }
};

TEST(ImplicitlyWeightedEvictingCacheMap, Misc) {
  ImplicitlyWeightedEvictingCacheMap<
      std::string,
      std::string,
      ValueStringLength>
      map{42};

  auto end = map.end();

  EXPECT_EQ(0, map.size());
  EXPECT_EQ(0, map.getCurrentTotalWeight());
  EXPECT_EQ(42, map.getMaxTotalWeight());
  EXPECT_TRUE(map.empty());
  EXPECT_FALSE(map.exists("x"));
  EXPECT_EQ(map.find("x"), end);

  std::string four("four");
  map.set("x", four);

  EXPECT_EQ(1, map.size());
  EXPECT_EQ(4, map.getCurrentTotalWeight());
  EXPECT_FALSE(map.empty());

  EXPECT_EQ(map.get("x"), four);
  EXPECT_NE(map.find("x"), end);
  EXPECT_EQ(map.find("x")->second, four);

  EXPECT_TRUE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));

  std::string twenty(20, '2');
  map.set("y", twenty);

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(24, map.getCurrentTotalWeight());

  EXPECT_EQ(map.get("y"), twenty);
  EXPECT_NE(map.find("y"), end);
  EXPECT_EQ(map.find("y")->second, twenty);

  EXPECT_TRUE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));

  // This time use non-heterogeneous overloads.
  // Evicts x
  map.set(std::string("z"), twenty);

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(40, map.getCurrentTotalWeight());

  EXPECT_EQ(map.get(std::string("z")), twenty);
  EXPECT_NE(map.find(std::string("z")), end);
  EXPECT_EQ(map.find(std::string("z"))->second, twenty);

  EXPECT_FALSE(map.exists(std::string("x")));
  EXPECT_TRUE(map.exists(std::string("y")));
  EXPECT_TRUE(map.exists(std::string("z")));

  // And move semantics for value, with and without
  // heterogeneous key
  std::string one1("a");
  map.set(std::string("a"), std::move(one1));

  std::string one2("b");
  map.set("b", std::move(one2));

  EXPECT_EQ(4, map.size());
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Use y to move it up in LRU list
  EXPECT_EQ(map.get("y"), twenty);

  // Evicts z
  map.set("c", "c");
  EXPECT_EQ(4, map.size());
  EXPECT_EQ(23, map.getCurrentTotalWeight());

  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));

  // Can also overwrite a value with set()
  map.set("a", twenty);

  EXPECT_EQ(4, map.size());
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Which might evict (evicts y)
  map.set("b", twenty);

  EXPECT_EQ(3, map.size());
  EXPECT_EQ(41, map.getCurrentTotalWeight());

  // Can also overwrite with replace() (evicts a)
  map.replace(map.find("c"), twenty);

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(40, map.getCurrentTotalWeight());

  // Try erase
  map.erase("c");
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(20, map.getCurrentTotalWeight());
  EXPECT_FALSE(map.exists("c"));
  // Re-add
  map.set("c", twenty);

  // No effect if not present
  map.erase("a");
  EXPECT_EQ(2, map.size());
  EXPECT_EQ(40, map.getCurrentTotalWeight());

  // Can also evict by setting max total weight (evicts b)
  map.setMaxTotalWeight(39);
  EXPECT_EQ(1, map.size());
  map.setMaxTotalWeight(20);
  EXPECT_EQ(1, map.size());
  EXPECT_TRUE(map.exists("c"));

  // Check clear
  map.clear();

  EXPECT_EQ(0, map.size());
  EXPECT_EQ(0, map.getCurrentTotalWeight());
  EXPECT_EQ(20, map.getMaxTotalWeight());
  EXPECT_TRUE(map.empty());
  EXPECT_FALSE(map.exists("a"));
  EXPECT_FALSE(map.exists("b"));
  EXPECT_FALSE(map.exists("c"));
  EXPECT_FALSE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
}

TEST(ImplicitlyWeightedEvictingCacheMap, ConstAndMove) {
  using MyMap = ImplicitlyWeightedEvictingCacheMap<
      std::string,
      const std::string,
      ValueStringLength>;
  MyMap map{10};

  std::string four("four");
  map.set("x", four);
  map.set("y", four);
  map.set("z", four);
  EXPECT_EQ(map.size(), 2);
  EXPECT_EQ(map.get("y"), four);
  EXPECT_EQ(map.get("z"), four);

  {
    MyMap map2{100};
    map2.set("xx", four);
    map2.set("yy", four);
    map2.set("zz", four);
    EXPECT_EQ(map2.size(), 3);

    map = std::move(map2);
    // Does not swap; moved-from is empty
    EXPECT_EQ(map2.size(), 0);
  }
  EXPECT_EQ(map.size(), 3);
  EXPECT_EQ(map.get("xx"), four);
  EXPECT_EQ(map.getMaxTotalWeight(), 100);
  // Verify prune hook after move
  map.erase("xx");
  EXPECT_EQ(map.size(), 2);

  // Move ctor
  MyMap map3{std::move(map)};
  EXPECT_EQ(map3.size(), 2);
  EXPECT_EQ(map.size(), 0);
}

TEST(ImplicitlyWeightedEvictingCacheMap, Scalars) {
  struct SumKV {
    size_t operator()(size_t a, size_t b) { return a + b; }
  };

  ImplicitlyWeightedEvictingCacheMap<size_t, size_t, SumKV> map{10000};
  map.set(1, 10);
  map.set(100, 1000);
  EXPECT_EQ(1111, map.getCurrentTotalWeight());
  EXPECT_EQ(map.get(100), 1000);
}

TEST(ImplicitlyWeightedEvictingCacheMap, NonCopyableAndIterator) {
  struct DerefUniqueWeight {
    size_t operator()(
        const std::string& /*key*/, const std::unique_ptr<size_t>& value) {
      return *value;
    }
  };
  ImplicitlyWeightedEvictingCacheMap<
      std::string,
      std::unique_ptr<size_t>,
      DerefUniqueWeight>
      map{6};

  map.set("x", std::make_unique<size_t>(1));
  map.set("y", std::make_unique<size_t>(2));
  map.set("z", std::make_unique<size_t>(3));

  auto it = map.begin();
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "z");
  EXPECT_EQ(*it->second, 3);
  ++it;
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "y");
  EXPECT_EQ(*it->second, 2);
  ++it;
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "x");
  EXPECT_EQ(*it->second, 1);
  ++it;
  EXPECT_EQ(it, map.end());

  it = map.findWithoutPromotion("x");
  EXPECT_EQ(*map.getWithoutPromotion("x"), 1);
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "x");
  EXPECT_EQ(*it->second, 1);
  ++it;
  EXPECT_EQ(it, map.end());

  it = map.find("x");
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "x");
  EXPECT_EQ(*it->second, 1);
  ++it;
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "z");
  EXPECT_EQ(*it->second, 3);
  ++it;
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "y");
  EXPECT_EQ(*it->second, 2);
  ++it;
  EXPECT_EQ(it, map.end());

  it = map.findWithoutPromotion("z");
  // Promote y to get it out of the way, ensure iterator to z still valid
  EXPECT_EQ(*map.get("y"), 2);
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "z");
  EXPECT_EQ(*it->second, 3);
  auto it2 = it;
  it2++;
  EXPECT_EQ(it2, map.end());

  map.erase(map.find("x"));
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "z");
  EXPECT_EQ(*it->second, 3);
  it2 = it;
  it2++;
  EXPECT_EQ(it2, map.end());

  EXPECT_EQ(*map.get("z"), 3);
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "z");
  EXPECT_EQ(*it->second, 3);
  ++it;
  ASSERT_NE(it, map.end());
  EXPECT_EQ(it->first, "y");
  EXPECT_EQ(*it->second, 2);
  ++it;
  EXPECT_EQ(it, map.end());

  auto rit = map.rbegin();
  ASSERT_NE(rit, map.rend());
  EXPECT_EQ(rit->first, "y");
  EXPECT_EQ(*rit->second, 2);
  ++rit;
  ASSERT_NE(rit, map.rend());
  EXPECT_EQ(rit->first, "z");
  EXPECT_EQ(*rit->second, 3);
  ++rit;
  EXPECT_EQ(rit, map.rend());

  it = map.find("z");
  // Temporarily over limit, evicts y
  map.replace(it, std::make_unique<size_t>(100));
  EXPECT_EQ(*it->second, 100);
  EXPECT_EQ(*map.get("z"), 100);
  EXPECT_EQ(map.size(), 1);

  // Any modification evicts the over limit z
  map.set("x", std::make_unique<size_t>(0));
  EXPECT_EQ(map.size(), 1);
  EXPECT_EQ(map.find("z"), map.end());
}

TEST(WeightedEvictingCacheMap, Misc) {
  WeightedEvictingCacheMap<std::string, std::string> map{42};
  auto end = map.end();

  EXPECT_EQ(0, map.size());
  EXPECT_EQ(0, map.getCurrentTotalWeight());
  EXPECT_EQ(42, map.getMaxTotalWeight());
  EXPECT_TRUE(map.empty());
  EXPECT_FALSE(map.exists("x"));
  EXPECT_EQ(map.find("x"), end);

  map.set("x", "X", 4);

  EXPECT_EQ(1, map.size());
  EXPECT_EQ(4, map.getCurrentTotalWeight());
  EXPECT_FALSE(map.empty());

  EXPECT_EQ(map.get("x"), "X");
  EXPECT_NE(map.find("x"), end);
  EXPECT_EQ(map.find("x")->second.value, "X");

  EXPECT_TRUE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));

  map.set("y", "Y", 20);

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(24, map.getCurrentTotalWeight());

  EXPECT_EQ(map.get("y"), "Y");
  EXPECT_NE(map.find("y"), end);
  EXPECT_EQ(map.find("y")->second.value, "Y");

  EXPECT_TRUE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));

  // This time use non-heterogeneous overloads.
  // Evicts x
  map.set(std::string("z"), "Z", 20);

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(40, map.getCurrentTotalWeight());

  EXPECT_EQ(map.get(std::string("z")), "Z");
  EXPECT_NE(map.find(std::string("z")), end);
  EXPECT_EQ(map.find(std::string("z"))->second.value, "Z");

  EXPECT_FALSE(map.exists(std::string("x")));
  EXPECT_TRUE(map.exists(std::string("y")));
  EXPECT_TRUE(map.exists(std::string("z")));

  // And move semantics for value, with and without
  // heterogeneous key
  map.set(std::string("a"), "A", 1);

  std::string b_val = "B";
  map.set("b", std::move(b_val), 1);

  EXPECT_EQ(4, map.size());
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Use y to move it up in LRU list
  EXPECT_EQ(map.get("y"), "Y");

  // Evicts z
  map.set("c", "C", 1);
  EXPECT_EQ(4, map.size());
  EXPECT_EQ(23, map.getCurrentTotalWeight());

  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));

  // Can also overwrite a value
  map.set("a", "AA", 20);

  EXPECT_EQ(4, map.size());
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Which might evict (evicts y)
  map.set("b", "BB", 20);

  EXPECT_EQ(3, map.size());
  EXPECT_EQ(41, map.getCurrentTotalWeight());

  // Can use get to change a value
  map.get("c") = "CC";

  // Change weight in place (no eviction yet)
  map.updateWeight(map.find("c"), 2);
  EXPECT_EQ(3, map.size());
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Change weight in place, non-heterogeneous key (evicts a)
  map.updateWeight(map.find(std::string("c")), 20);
  EXPECT_EQ(2, map.size());
  EXPECT_EQ(40, map.getCurrentTotalWeight());

  // Can also evict by setting max total weight (evicts b)
  map.setMaxTotalWeight(39);
  EXPECT_EQ(1, map.size());
  map.setMaxTotalWeight(20);
  EXPECT_EQ(1, map.size());
  EXPECT_TRUE(map.exists("c"));

  // Check clear
  map.clear();

  EXPECT_EQ(0, map.size());
  EXPECT_EQ(0, map.getCurrentTotalWeight());
  EXPECT_EQ(20, map.getMaxTotalWeight());
  EXPECT_TRUE(map.empty());
  EXPECT_FALSE(map.exists("a"));
  EXPECT_FALSE(map.exists("b"));
  EXPECT_FALSE(map.exists("c"));
  EXPECT_FALSE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
}

TEST(WeightedEvictingCacheMap, OverMaxWeight) {
  struct Unit {};
  WeightedEvictingCacheMap<std::string, Unit> map{42};

  map.set("x", {}, 5);
  map.set("y", {}, 5);
  map.set("z", {}, 5);

  // Setting weight too high evicts the entry (if it's not most recently used)
  // and anything used less recently
  map.updateWeight(map.findWithoutPromotion("y"), 100);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));
  EXPECT_TRUE(map.exists("z"));
  EXPECT_EQ(1, map.size());

  map.set("x", {}, 5);
  map.set("y", {}, 5);
  map.set("z", {}, 5);

  // If it's most recently used, it is protected from immediate eviction
  map.updateWeight(map.findWithoutPromotion("z"), 100);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));
  EXPECT_TRUE(map.exists("z"));
  EXPECT_EQ(1, map.size());

  map.set("x", {}, 5);
  EXPECT_EQ(1, map.size()); // over-weight entry evicted
  map.set("y", {}, 5);
  map.set("z", {}, 5);

  // If it's most recently used, it is protected from immediate eviction
  map.updateWeight(map.find("y"), 100);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
  EXPECT_EQ(1, map.size());

  // Forces the entry (beyond capacity)
  map.set("z", {}, 100);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_FALSE(map.exists("y"));
  EXPECT_TRUE(map.exists("z"));
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(42, map.getMaxTotalWeight());

  // It is not protected from eviction in subsequent write ops
  map.setMaxTotalWeight(42); // Same as before
  EXPECT_EQ(0, map.size());

  // Another force set
  map.set("z", {}, 100);

  // Can be evicted by a zero weight insert
  map.set("x", {}, 0);

  EXPECT_TRUE(map.exists("x"));
  EXPECT_FALSE(map.exists("z"));
  EXPECT_EQ(0, map.getCurrentTotalWeight());

  // Zero weight not evicted spuriously
  map.set("z", {}, 42);

  EXPECT_TRUE(map.exists("x"));
  EXPECT_TRUE(map.exists("z"));

  // But is evicted in LRU order
  map.set("y", {}, 5);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));

  map.set("x", {}, 5);

  EXPECT_TRUE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
  EXPECT_EQ(10, map.getCurrentTotalWeight());

  // Another force set
  map.set("y", {}, 100);

  EXPECT_FALSE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
  EXPECT_EQ(100, map.getCurrentTotalWeight());

  // Can decrease weight (within max) without eviction
  {
    auto it = map.find("y");
    map.updateWeight(it, 42);
    // Iterator known valid
    EXPECT_EQ(42, it->second.weight);
  }

  EXPECT_FALSE(map.exists("x"));
  EXPECT_TRUE(map.exists("y"));
  EXPECT_FALSE(map.exists("z"));
  EXPECT_EQ(42, map.getCurrentTotalWeight());

  // Reset
  map.set("x", {}, 5);
  map.set("y", {}, 5);
  EXPECT_EQ(10, map.getCurrentTotalWeight());

  // Try erase
  map.erase("y");
  EXPECT_EQ(1, map.size());
  EXPECT_EQ(5, map.getCurrentTotalWeight());
  // Re-add
  map.set("y", {}, 5);
  EXPECT_EQ(2, map.size());
  EXPECT_EQ(10, map.getCurrentTotalWeight());
}

TEST(WeightedEvictingCacheMap, ConstAndIterator) {
  using MyMap = WeightedEvictingCacheMap<std::string, const std::string>;
  MyMap map{10};

  map.set("x", "X", 3);
  map.set("y", "Y", 4);
  map.set("z", "Z", 5); // evicts x
  EXPECT_EQ(map.size(), 2);
  EXPECT_FALSE(map.exists("x"));
  EXPECT_EQ(map.get("y"), "Y");
  EXPECT_EQ(map.find("z")->second.value, "Z");

  const MyMap& cmap = map;
  {
    MyMap::iterator it = map.begin();
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, map.end());
  }
  {
    MyMap::const_iterator it = cmap.begin();
    ASSERT_NE(it, cmap.end());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, cmap.end());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, cmap.end());
  }
  {
    MyMap::const_iterator it = map.cbegin();
    ASSERT_NE(it, map.cend());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, map.cend());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, map.cend());
  }
  map.get("y"); // swap entry order, reverse iterator sees old order
  {
    MyMap::reverse_iterator it = map.rbegin();
    ASSERT_NE(it, map.rend());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, map.rend());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, map.rend());
  }
  {
    MyMap::const_reverse_iterator it = cmap.rbegin();
    ASSERT_NE(it, cmap.rend());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, cmap.rend());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, cmap.rend());
  }
  {
    MyMap::const_reverse_iterator it = map.crbegin();
    ASSERT_NE(it, map.crend());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(it->second.value, "Z");
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    ASSERT_NE(it, map.crend());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(it->second.value, "Y");
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, map.crend());
  }
}

TEST(WeightedEvictingCacheMap, NonCopyableAndIteratorMutation) {
  using MyMap = WeightedEvictingCacheMap<std::string, std::unique_ptr<int>>;
  MyMap map{10};

  map.set("x", std::make_unique<int>(1), 3);
  map.set("y", std::make_unique<int>(2), 4);
  map.set("z", std::make_unique<int>(3), 5);
  EXPECT_EQ(map.size(), 2);
  EXPECT_FALSE(map.exists("x"));
  EXPECT_EQ(*map.get("y"), 2);
  EXPECT_EQ(*map.find("z")->second.value, 3);
  EXPECT_EQ(map.find("z")->second.weight, 5);

  {
    MyMap::iterator it = map.begin();
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(*it->second.value, 3);
    EXPECT_EQ(it->second.weight, 5);
    // Now mutate with move semantics
    std::unique_ptr<int> tmp{new int(42)};
    std::swap(tmp, it->second.value);
    EXPECT_EQ(*it->second.value, 42);
    EXPECT_EQ(*tmp, 3);
    EXPECT_EQ(*map.get("z"), 42);
    ++it;
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(*it->second.value, 2);
    EXPECT_EQ(it->second.weight, 4);
    ++it;
    EXPECT_EQ(it, map.end());
  }
  {
    MyMap::reverse_iterator it = map.rbegin();
    ASSERT_NE(it, map.rend());
    EXPECT_EQ(it->first, "y");
    EXPECT_EQ(*it->second.value, 2);
    EXPECT_EQ(it->second.weight, 4);
    // Now mutate with move semantics
    it->second.value = std::make_unique<int>(43);
    EXPECT_EQ(*map.getWithoutPromotion("y"), 43);
    ++it;
    ASSERT_NE(it, map.rend());
    EXPECT_EQ(it->first, "z");
    EXPECT_EQ(*it->second.value, 42);
    EXPECT_EQ(it->second.weight, 5);
    ++it;
    EXPECT_EQ(it, map.rend());
  }
}

TEST(WeightedEvictingCacheMap, Scalars) {
  WeightedEvictingCacheMap<size_t, size_t> map{10};
  map.set(1, 2, 3);
  map.set(4, 5, 6);
  EXPECT_EQ(9, map.getCurrentTotalWeight());
  EXPECT_EQ(map.get(1), 2U);
  EXPECT_EQ(map.get(4), 5U);
  // Evicts others
  map.set(6, 7, 8);
  EXPECT_EQ(8, map.getCurrentTotalWeight());
  EXPECT_EQ(map.find(1), map.end());
  EXPECT_FALSE(map.exists(4));
  EXPECT_EQ(map.find(6)->second.value, 7U);
}

TEST(WeightedEvictingCacheMap, ApproximateEntryMemUsage) {
  // See test EvictingCacheMap::ApproximateEntryMemUsage
  EXPECT_LE(
      (ImplicitlyWeightedEvictingCacheMap<
          uint64_t,
          std::unique_ptr<char[]>,
          ValueStringLength>::kApproximateEntryMemUsage),
      48U);

  // Add explicit weight
  EXPECT_LE(
      (WeightedEvictingCacheMap<uint64_t, std::unique_ptr<char[]>>::
           kApproximateEntryMemUsage),
      54U);

  // Example with weight = approximate memory usage
  size_t kValueSize = 40;
  WeightedEvictingCacheMap<uint64_t, std::unique_ptr<char[]>> map{100};
  std::unique_ptr<char[]> value(new char[kValueSize]{});
  size_t weight = kValueSize + map.kApproximateEntryMemUsage;
  map.set(42U, std::move(value), weight);
}
