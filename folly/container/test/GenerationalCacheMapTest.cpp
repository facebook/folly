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

#include <folly/container/GenerationalCacheMap.h>

#include <string>
#include <thread>
#include <vector>

#include <folly/Conv.h>
#include <folly/portability/GTest.h>

using folly::GenerationalCacheMap;

namespace {

using Map = GenerationalCacheMap<uint64_t, uint64_t>;

// Look up a key and insert it on a miss, the way a cache in front of an
// expensive computation would. Returns whether it was a hit.
bool lookup(Map& map, uint64_t key) {
  if (auto found = map.find(key)) {
    // A hit must return what was stored for this key.
    return *found == key * 2;
  }
  map.set(key, key * 2);
  return false;
}

} // namespace

TEST(GenerationalCacheMapTest, MissThenHit) {
  Map map(128);

  EXPECT_FALSE(lookup(map, 1));
  EXPECT_TRUE(lookup(map, 1));
  EXPECT_FALSE(lookup(map, 2));

  EXPECT_EQ(2, map.size());
  EXPECT_EQ(0, map.rotations());
}

TEST(GenerationalCacheMapTest, SetReplacesExistingValue) {
  Map map(128);

  map.set(1, 10);
  EXPECT_EQ(10, *map.find(1));

  map.set(1, 20);
  EXPECT_EQ(20, *map.find(1));
  EXPECT_EQ(1, map.size()) << "replacing must not add an entry";
}

TEST(GenerationalCacheMapTest, FindReturnsEmptyRefForAbsentKey) {
  Map map(128);

  EXPECT_FALSE(static_cast<bool>(map.find(42)));
  EXPECT_EQ(nullptr, map.find(42).get());
}

// A key that keeps being used must survive rotation indefinitely. This is what
// promotion buys, and what a fixed-capacity map that never evicts also gets
// right.
TEST(GenerationalCacheMapTest, KeyInUseSurvivesRotations) {
  constexpr uint64_t kCapacity = 128;
  constexpr uint64_t kHot = 0xbeef;
  Map map(kCapacity);

  lookup(map, kHot);
  for (uint64_t i = 0; i < 20 * kCapacity; ++i) {
    lookup(map, 0x100000 + i);
    ASSERT_TRUE(lookup(map, kHot)) << "evicted after " << i << " insertions";
  }

  EXPECT_GT(map.rotations(), 2);
}

// A key that falls out of use must be dropped, so its space is recovered for
// whatever the caller is doing now.
TEST(GenerationalCacheMapTest, UnusedKeyIsDroppedWithinTwoRotations) {
  constexpr uint64_t kCapacity = 128;
  constexpr uint64_t kCold = 0xdead;
  Map map(kCapacity);

  lookup(map, kCold);
  ASSERT_TRUE(lookup(map, kCold));

  uint64_t next = 0x100000;
  while (map.rotations() < 2) {
    lookup(map, next++);
  }

  EXPECT_FALSE(lookup(map, kCold));
}

// The motivating case for rotating rather than capping: a burst of distinct
// keys must not lock the map into a useless state. A fixed-capacity map that
// never evicts fails this: the burst fills it permanently, and the working set
// discovered afterwards misses forever.
TEST(GenerationalCacheMapTest, BurstDoesNotPoisonTheCache) {
  constexpr uint64_t kCapacity = 512;
  constexpr uint64_t kWorkingSet = 64;
  Map map(kCapacity);

  for (uint64_t i = 0; i < 10 * kCapacity; ++i) {
    lookup(map, 0x1000000 + i); // Never seen again.
  }

  for (int round = 0; round < 20; ++round) {
    for (uint64_t i = 0; i < kWorkingSet; ++i) {
      lookup(map, 0x2000000 + i);
    }
  }

  size_t hits = 0;
  for (uint64_t i = 0; i < kWorkingSet; ++i) {
    hits += lookup(map, 0x2000000 + i) ? 1 : 0;
  }
  EXPECT_EQ(kWorkingSet, hits);
}

TEST(GenerationalCacheMapTest, SizeStaysNearCapacity) {
  constexpr uint64_t kCapacity = 512;
  Map map(kCapacity);

  for (uint64_t i = 0; i < 100 * kCapacity; ++i) {
    lookup(map, i);
  }

  EXPECT_GT(map.rotations(), 0);
  EXPECT_LE(map.size(), kCapacity);
}

TEST(GenerationalCacheMapTest, NonTrivialValueType) {
  GenerationalCacheMap<std::string, std::string> map(8);

  map.set("key", "value");
  EXPECT_EQ("value", *map.find("key"));

  // Force rotations, so entries are promoted (copied) and whole generations are
  // destroyed, exercising the value type's copy and destructor.
  for (int i = 0; i < 200; ++i) {
    map.find("key");
    map.set(folly::to<std::string>(i), std::string(64, 'x'));
  }

  auto found = map.find("key");
  ASSERT_TRUE(static_cast<bool>(found));
  EXPECT_EQ("value", *found);
}

// Exercises readers holding a version across rotations performed by other
// threads. Mostly valuable under ASAN and TSAN.
TEST(GenerationalCacheMapTest, ConcurrentUse) {
  constexpr uint64_t kCapacity = 1024;
  constexpr size_t kThreads = 8;
  constexpr uint64_t kIterations = 20000;
  Map map(kCapacity);

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (size_t t = 0; t < kThreads; ++t) {
    threads.emplace_back([&map, t] {
      for (uint64_t i = 0; i < kIterations; ++i) {
        // A shared hot set mixed with per-thread churn, so rotations keep
        // happening while other threads hold versions.
        lookup(map, (i % 8 == 0) ? (i % 64) : ((uint64_t(t + 1) << 32) + i));
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_GT(map.rotations(), 0);
  // Inserters racing a rotation overshoot, but only until the map reaches twice
  // the rotation threshold, past which they wait for the rotation instead. Each
  // generation is therefore bounded by kCapacity plus at most one insertion per
  // thread in flight when the waiting kicks in.
  EXPECT_LE(map.size(), 2 * kCapacity + 16 * kThreads);
}
