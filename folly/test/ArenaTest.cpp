/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "folly/Arena.h"
#include "folly/StlAllocator.h"

#include <set>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

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
  maximum_size += goodMallocSize(requestedBlockSize + 1);
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
  maximum_size += 10 * goodMallocSize(requestedBlockSize + 1);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Insert something huge
  ptr = static_cast<size_t*>(arena.allocate(10 * requestedBlockSize));
  allocatedItems.insert(ptr);
  minimum_size += 10 * requestedBlockSize;
  maximum_size += goodMallocSize(10 * requestedBlockSize + 1);
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;

  // Nuke 'em all
  for (const auto& item : allocatedItems) {
    arena.deallocate(item);
  }
  //The total size should be the same
  EXPECT_TRUE(arena.totalSize() >= minimum_size);
  EXPECT_TRUE(arena.totalSize() <= maximum_size);
  VLOG(4) << minimum_size << " < " << arena.totalSize() << " < "
          << maximum_size;
}

TEST(Arena, Vector) {
  static const size_t requestedBlockSize = 64;
  SysArena arena(requestedBlockSize);

  EXPECT_EQ(arena.totalSize(), sizeof(SysArena));

  std::vector<size_t, StlAllocator<SysArena, size_t>>
    vec { {}, StlAllocator<SysArena, size_t>(&arena) };

  for (size_t i = 0; i < 1000; i++) {
    vec.push_back(i);
  }

  for (size_t i = 0; i < 1000; i++) {
    EXPECT_EQ(i, vec[i]);
  }
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  return ret;
}
