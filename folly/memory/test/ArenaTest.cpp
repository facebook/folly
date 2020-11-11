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

#include <folly/memory/Arena.h>
#include <folly/Memory.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

#include <set>
#include <vector>

#include <glog/logging.h>

using namespace folly;

static_assert(AllocatorHasTrivialDeallocate<SysArena>::value, "");

TEST(Arena, SizeSanity) {
  std::set<size_t*> allocatedItems;

  static const size_t requestedBlockSize = 64;
  SysArena arena(requestedBlockSize);
  size_t minimum_size = sizeof(SysArena), maximum_size = minimum_size;
  EXPECT_EQ(arena.totalSize(), minimum_size);

  // Insert a single small element to get a new block
  size_t* ptr = static_cast<size_t*>(arena.allocate(sizeof(long)));
  allocatedItems.insert(ptr);
  minimum_size += requestedBlockSize;
  maximum_size += goodMallocSize(requestedBlockSize + SysArena::kBlockOverhead);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Insert a larger element, size should be the same
  ptr = static_cast<size_t*>(arena.allocate(requestedBlockSize / 2));
  allocatedItems.insert(ptr);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Insert 10 full block sizes to get 10 new blocks
  for (int i = 0; i < 10; i++) {
    ptr = static_cast<size_t*>(arena.allocate(requestedBlockSize));
    allocatedItems.insert(ptr);
  }
  minimum_size += 10 * requestedBlockSize;
  maximum_size +=
      10 * goodMallocSize(requestedBlockSize + SysArena::kBlockOverhead);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Insert something huge
  ptr = static_cast<size_t*>(arena.allocate(10 * requestedBlockSize));
  allocatedItems.insert(ptr);
  minimum_size += 10 * requestedBlockSize;
  maximum_size +=
      goodMallocSize(10 * requestedBlockSize + SysArena::kBlockOverhead);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Nuke 'em all
  for (const auto& item : allocatedItems) {
    arena.deallocate(item, 0 /* unused */);
  }
  // The total size should be the same
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;
}

TEST(Arena, BytesUsedSanity) {
  static const size_t smallChunkSize = 1024;
  static const size_t blockSize = goodMallocSize(16 * smallChunkSize);
  const size_t bigChunkSize = blockSize - 4 * smallChunkSize;

  size_t bytesUsed = 0;

  SysArena arena(blockSize);
  EXPECT_EQ(arena.bytesUsed(), bytesUsed);

  // Insert 2 small chunks
  arena.allocate(smallChunkSize);
  arena.allocate(smallChunkSize);
  bytesUsed += 2 * smallChunkSize;
  EXPECT_EQ(arena.bytesUsed(), bytesUsed);
  EXPECT_TRUE(arena.totalSize() >= blockSize);
  EXPECT_TRUE(arena.totalSize() <= 2 * blockSize);

  // Insert big chunk, should still fit in one block
  arena.allocate(bigChunkSize);
  bytesUsed += bigChunkSize;
  EXPECT_EQ(arena.bytesUsed(), bytesUsed);
  EXPECT_TRUE(arena.totalSize() >= blockSize);
  EXPECT_TRUE(arena.totalSize() <= 2 * blockSize);

  // Insert big chunk once more, should trigger new block allocation
  arena.allocate(bigChunkSize);
  bytesUsed += bigChunkSize;
  EXPECT_EQ(arena.bytesUsed(), bytesUsed);
  EXPECT_TRUE(arena.totalSize() >= 2 * blockSize);
  EXPECT_TRUE(arena.totalSize() <= 3 * blockSize);

  // Test that bytesUsed() accounts for alignment
  static const size_t tinyChunkSize = 7;
  arena.allocate(tinyChunkSize);
  EXPECT_TRUE(arena.bytesUsed() >= bytesUsed + tinyChunkSize);
  size_t delta = arena.bytesUsed() - bytesUsed;
  EXPECT_EQ(delta & (delta - 1), 0);
}

TEST(Arena, Vector) {
  static const size_t requestedBlockSize = 64;
  SysArena arena(requestedBlockSize);

  EXPECT_EQ(arena.totalSize(), sizeof(SysArena));

  std::vector<size_t, SysArenaAllocator<size_t>> vec{
      {}, SysArenaAllocator<size_t>(arena)};

  for (size_t i = 0; i < 1000; i++) {
    vec.push_back(i);
  }

  for (size_t i = 0; i < 1000; i++) {
    EXPECT_EQ(i, vec[i]);
  }
}

TEST(Arena, DefaultConstructible) {
  std::vector<size_t, FallbackSysArenaAllocator<size_t>> vec;
  EXPECT_NO_THROW(vec.push_back(42));
}

TEST(Arena, Compare) {
  SysArena arena1;
  SysArenaAllocator<size_t> alloc1(arena1);
  SysArenaAllocator<size_t> alloc2(arena1);

  EXPECT_EQ(alloc1, alloc2);

  SysArena arena2;
  SysArenaAllocator<size_t> alloc3(arena2);

  EXPECT_NE(alloc1, alloc3);
}

TEST(Arena, SizeLimit) {
  static const size_t requestedBlockSize = sizeof(size_t);
  static const size_t maxSize = 10 * requestedBlockSize;

  SysArena arena(requestedBlockSize, maxSize);

  void* a = arena.allocate(sizeof(size_t));
  EXPECT_TRUE(a != nullptr);
  EXPECT_THROW(arena.allocate(maxSize + 1), std::bad_alloc);
}

TEST(Arena, ExtremeSize) {
  static const size_t requestedBlockSize = sizeof(size_t);

  SysArena arena(requestedBlockSize);

  void* a = arena.allocate(sizeof(size_t));
  EXPECT_TRUE(a != nullptr);
  EXPECT_THROW(arena.allocate(SIZE_MAX - 2), std::bad_alloc);
}

TEST(Arena, Clear) {
  static const size_t blockSize = 1024;
  SysArena arena(blockSize);

  for (int i = 0; i < 10; ++i) {
    std::vector<size_t> sizes(1000);
    std::generate(sizes.begin(), sizes.end(), []() {
      return std::rand() % blockSize * 2;
    });

    std::vector<void*> addresses;
    for (auto s : sizes) {
      addresses.push_back(arena.allocate(s));
    }

    const size_t totalSize = arena.totalSize();
    const size_t bytesUsed = arena.bytesUsed();

    arena.clear();

    int j = 0;
    for (auto s : sizes) {
      auto addr = arena.allocate(s);
      if (s <= blockSize) {
        EXPECT_EQ(addr, addresses[j]);
      }
      j++;
    }

    EXPECT_EQ(arena.bytesUsed(), bytesUsed);
    EXPECT_GT(arena.totalSize(), totalSize);

    arena.clear();
  }
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  return ret;
}
