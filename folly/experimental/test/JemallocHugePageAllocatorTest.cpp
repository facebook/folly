/*
 * Copyright 2016-present Facebook, Inc.
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

#include <folly/experimental/JemallocHugePageAllocator.h>
#include <folly/container/F14Map.h>

#include <folly/memory/Malloc.h>
#include <folly/portability/GTest.h>

#include <vector>

using jha = folly::JemallocHugePageAllocator;

TEST(JemallocHugePageAllocatorTest, Basic) {
  EXPECT_FALSE(jha::initialized());

  // Allocation should work even if uninitialized
  auto ptr = jha::allocate(1024);
  EXPECT_NE(nullptr, ptr);
  jha::deallocate(ptr);

  bool initialized = jha::init(1);
  if (initialized) {
    EXPECT_NE(0, jha::freeSpace());
  }

  ptr = jha::allocate(1024);
  EXPECT_NE(nullptr, ptr);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(ptr));
  }

  // Allocate some arrays on huge page
  auto array_of_arrays = new (ptr) std::array<int, 100>[5];

  if (initialized) {
    EXPECT_FALSE(jha::addressInArena(&array_of_arrays));
    EXPECT_TRUE(jha::addressInArena(&array_of_arrays[0]));
    EXPECT_TRUE(jha::addressInArena(&array_of_arrays[0][0]));
  }

  jha::deallocate(ptr);
}

TEST(JemallocHugePageAllocatorTest, LargeAllocations) {
  // Allocate before init - will not use huge pages
  void* ptr0 = jha::allocate(3 * 1024 * 512);

  // One 2MB huge page
  bool initialized = jha::init(1);
  if (initialized) {
    EXPECT_NE(0, jha::freeSpace());
  }

  // This fits
  void* ptr1 = jha::allocate(3 * 1024 * 512);
  EXPECT_NE(nullptr, ptr1);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(ptr1));
  }

  // This is too large to fit
  void* ptr2 = jha::allocate(4 * 1024 * 1024);
  EXPECT_NE(nullptr, ptr2);

  EXPECT_FALSE(jha::addressInArena(ptr2));

  // Free and reuse huge page area
  jha::deallocate(ptr2);
  jha::deallocate(ptr0);
  ptr2 = jha::allocate(1024 * 1024);

  // No memory in the huge page arena was freed - ptr0 was allocated
  // before init and ptr2 didn't fit
  EXPECT_FALSE(jha::addressInArena(ptr2));

  jha::deallocate(ptr1);
  ptr1 = jha::allocate(3 * 1024 * 512);
  EXPECT_NE(nullptr, ptr1);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(ptr1));
  }

  // Just using free works equally well
  free(ptr1);
  ptr1 = jha::allocate(3 * 1024 * 512);
  EXPECT_NE(nullptr, ptr1);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(ptr1));
  }

  jha::deallocate(ptr1);
  jha::deallocate(ptr2);
}

TEST(JemallocHugePageAllocatorTest, STLAllocator) {
  using MyVecAllocator = folly::CxxHugePageAllocator<int>;
  using MyVec = std::vector<int, MyVecAllocator>;

  using MyMapAllocator =
      folly::CxxHugePageAllocator<folly::f14::detail::MapValueType<int, MyVec>>;
  using MyMap = folly::F14FastMap<
      int,
      MyVec,
      folly::f14::DefaultHasher<int>,
      folly::f14::DefaultKeyEqual<int>,
      MyMapAllocator>;

  MyVec vec;
  // This should work, just won't get huge pages since
  // init hasn't been called yet
  vec.reserve(100);
  EXPECT_NE(nullptr, &vec[0]);

  // Reserve & initialize, not on huge pages
  MyVec vec2(100);
  EXPECT_NE(nullptr, &vec[0]);

  // F14 maps need quite a lot of memory by default
  bool initialized = jha::init(4);
  if (initialized) {
    EXPECT_NE(0, jha::freeSpace());
  }

  // Reallocate, this time on huge pages
  vec.reserve(200);
  EXPECT_NE(nullptr, &vec[0]);

  MyMap map1;
  map1[0] = {1, 2, 3};
  auto map2_ptr = std::make_unique<MyMap>();
  MyMap& map2 = *map2_ptr;
  map2[0] = {1, 2, 3};

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(&vec[0]));
    EXPECT_TRUE(jha::addressInArena(&map1[0]));
    EXPECT_TRUE(jha::addressInArena(&map1[0][0]));
    EXPECT_TRUE(jha::addressInArena(&map2[0]));
    EXPECT_TRUE(jha::addressInArena(&map2[0][0]));
  }

  // This will be on the huge page arena
  map1[0] = std::move(vec);

  // But not this, since vec2 content was allocated before init
  map1[1] = std::move(vec2);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(&map1[0]));
    EXPECT_TRUE(jha::addressInArena(&map1[1]));
    EXPECT_TRUE(jha::addressInArena(&map1[0][0]));
    EXPECT_FALSE(jha::addressInArena(&map1[1][0]));
  }

  // realloc on huge pages
  map1[1].reserve(200);

  if (initialized) {
    EXPECT_TRUE(jha::addressInArena(&map1[1][0]));
  }
}
