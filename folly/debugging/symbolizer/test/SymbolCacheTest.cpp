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

#include <folly/debugging/symbolizer/SymbolCache.h>

#include <folly/portability/GTest.h>

using namespace folly::symbolizer;

namespace {

CachedSymbolizedFrames makeFrames(uintptr_t address, size_t count = 1) {
  CachedSymbolizedFrames frames;
  frames.resize(count);
  for (size_t i = 0; i < count; ++i) {
    frames[i].found = true;
    frames[i].addr = address + i;
  }
  return frames;
}

} // namespace

TEST(SymbolCacheTest, LruMissThenHit) {
  auto cache = makeLruSymbolCache(128);
  CachedSymbolizedFrames frames;

  EXPECT_FALSE(cache->find(0xabc, frames));
  EXPECT_TRUE(frames.empty())
      << "misses should not modify the output parameter";

  cache->insert(0xabc, makeFrames(0xabc));

  ASSERT_TRUE(cache->find(0xabc, frames));
  ASSERT_EQ(1, frames.size());
  EXPECT_EQ(0xabc, frames[0].addr);
  EXPECT_TRUE(frames[0].found);
}

// An entry can hold inline frames alongside the non-inlined call, and they must
// survive the round trip.
TEST(SymbolCacheTest, LruRetainsInlineFrames) {
  auto cache = makeLruSymbolCache(128);
  cache->insert(0xabc, makeFrames(0x1000, 3));

  CachedSymbolizedFrames frames;
  ASSERT_TRUE(cache->find(0xabc, frames));
  ASSERT_EQ(3, frames.size());
  EXPECT_EQ(0x1002, frames[2].addr);
}

TEST(SymbolCacheTest, LruEvictsBeyondCapacity) {
  constexpr size_t kCapacity = 16;
  auto cache = makeLruSymbolCache(kCapacity);

  for (uintptr_t i = 0; i < 4 * kCapacity; ++i) {
    cache->insert(i, makeFrames(i));
  }

  CachedSymbolizedFrames frames;
  EXPECT_FALSE(cache->find(0, frames));
  EXPECT_TRUE(cache->find(4 * kCapacity - 1, frames));
}

TEST(SymbolCacheTest, GenerationalMissThenHit) {
  auto cache = makeGenerationalSymbolCache(128);
  CachedSymbolizedFrames frames;

  EXPECT_FALSE(cache->find(0xabc, frames));
  EXPECT_TRUE(frames.empty())
      << "misses should not modify the output parameter";

  cache->insert(0xabc, makeFrames(0x1000, 3));

  ASSERT_TRUE(cache->find(0xabc, frames));
  ASSERT_EQ(3, frames.size());
  EXPECT_EQ(0x1002, frames[2].addr);
}

TEST(SymbolCacheTest, NullCacheStoresNothing) {
  auto cache = makeNullSymbolCache();
  CachedSymbolizedFrames frames;

  cache->insert(0xabc, makeFrames(0xabc));
  EXPECT_FALSE(cache->find(0xabc, frames));
}

// Entries are keyed by address alone, so modes must not share a cache: a
// DISABLED entry carries no file and line information where a FAST one does.
TEST(SymbolCacheTest, SharedCachesAreDistinctPerMode) {
  auto& disabled = getSharedSymbolCache(LocationInfoMode::DISABLED);
  auto& fast = getSharedSymbolCache(LocationInfoMode::FAST);

  EXPECT_NE(&disabled, &fast);
  EXPECT_EQ(&disabled, &getSharedSymbolCache(LocationInfoMode::DISABLED));

  constexpr uintptr_t kAddress = 0x5eed;
  disabled.insert(kAddress, makeFrames(kAddress));

  CachedSymbolizedFrames frames;
  EXPECT_TRUE(disabled.find(kAddress, frames));
  EXPECT_FALSE(fast.find(kAddress, frames));
}
