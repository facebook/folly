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

#include <folly/memory/IoUringArena.h>

#include <folly/container/F14Map.h>

#include <folly/portability/GTest.h>

#include <array>
#include <thread>
#include <vector>

using iua = folly::IoUringArena;

static constexpr size_t kb(size_t kilos) {
  return kilos * 1024;
}

static constexpr size_t mb(size_t megs) {
  return kb(megs * 1024);
}

TEST(IoUringArenaTest, Basic) {
  EXPECT_FALSE(iua::initialized());

  // Allocation should work even if uninitialized
  auto ptr = iua::allocate(kb(1));
  EXPECT_NE(nullptr, ptr);
  iua::deallocate(ptr, kb(1));

  std::array<bool, 8> initResults{};
  std::vector<std::thread> initThreads;
  initThreads.reserve(initResults.size());
  for (size_t i = 0; i < initResults.size(); ++i) {
    initThreads.emplace_back([&, i] { initResults[i] = iua::init(mb(4)); });
  }
  for (auto& thread : initThreads) {
    thread.join();
  }
  const bool initialized = initResults.front();
  for (bool result : initResults) {
    EXPECT_EQ(result, initialized);
  }
  if (initialized) {
    EXPECT_NE(0, iua::freeSpace());
    EXPECT_NE(0, iua::arenaIndex());
#if defined(FOLLY_SANITIZE) && FOLLY_SANITIZE
    EXPECT_EQ(iua::kFixedRegionArenaIndex, iua::arenaIndex());
#endif
  }

  ptr = iua::allocate(kb(1));
  EXPECT_NE(nullptr, ptr);

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(ptr));
  }

  static_cast<char*>(ptr)[0] = 42;
  ptr = iua::reallocate(ptr, kb(2));
  ASSERT_NE(nullptr, ptr);
  EXPECT_EQ(42, static_cast<char*>(ptr)[0]);

  // Allocate some arrays on the arena
  auto array_of_arrays = new (ptr) std::array<int, 100>[5];

  if (initialized) {
    EXPECT_FALSE(iua::addressInArena(&array_of_arrays));
    EXPECT_TRUE(iua::addressInArena(&array_of_arrays[0]));
    EXPECT_TRUE(iua::addressInArena(&array_of_arrays[0][0]));
  }

  EXPECT_EQ(nullptr, iua::reallocate(ptr, 0));
}

TEST(IoUringArenaTest, LargeAllocations) {
  // Allocate before init - will not use arena
  void* ptr0 = iua::allocate(kb(1));

  // 4MB arena
  bool initialized = iua::init(mb(4));
  const bool usingFixedRegion =
      initialized && iua::arenaIndex() == iua::kFixedRegionArenaIndex;
  if (initialized) {
    EXPECT_NE(0, iua::freeSpace());
  }

  // This fits
  void* ptr1 = iua::allocate(mb(2));
  EXPECT_NE(nullptr, ptr1);

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(ptr1));
  }

  // This is too large to fit
  void* ptr2 = iua::allocate(mb(4));
  EXPECT_NE(nullptr, ptr2);

  EXPECT_FALSE(iua::addressInArena(ptr2));

  // Free and reuse allocations
  iua::deallocate(ptr2, mb(4));
  iua::deallocate(ptr0, kb(1));
  ptr2 = iua::allocate(kb(64));
  EXPECT_NE(nullptr, ptr2);
  if (!usingFixedRegion) {
    // No arena allocation was freed: ptr0 predates init and ptr2 did not fit.
    EXPECT_FALSE(iua::addressInArena(ptr2));
  }

  iua::deallocate(ptr1, mb(2));
  void* ptr3 = iua::allocate(mb(1) + kb(512));
  EXPECT_NE(nullptr, ptr3);

  if (initialized) {
    if (!usingFixedRegion) {
      EXPECT_EQ(ptr1, ptr3);
    }
    EXPECT_TRUE(iua::addressInArena(ptr3));
  }

  if (usingFixedRegion) {
    iua::deallocate(ptr3);
  } else {
    // jemalloc arena allocations can also be released with free().
    free(ptr3);
  }
  ptr3 = iua::allocate(mb(1) + kb(512));
  EXPECT_NE(nullptr, ptr3);

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(ptr3));
  }

  iua::deallocate(ptr2, kb(64));
  iua::deallocate(ptr3, mb(1) + kb(512));
}

TEST(IoUringArenaTest, MemoryUsageTest) {
  bool initialized = iua::init(mb(160));
  if (initialized) {
    if (iua::arenaIndex() == iua::kFixedRegionArenaIndex) {
      EXPECT_GT(iua::freeSpace(), 0);
    } else {
      EXPECT_GE(iua::freeSpace(), mb(160));
    }
  }

  struct c32 {
    char val[32];
  };
  using Vec32 = std::vector<c32, folly::CxxIoUringAllocator<c32>>;
  Vec32 vec32;
  for (int i = 0; i < 10; i++) {
    vec32.push_back({});
  }
  void* ptr1 = iua::allocate(32);
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(158));
  }
  struct c320 {
    char val[320];
  };
  using Vec320 = std::vector<c320, folly::CxxIoUringAllocator<c320>>;
  Vec320 vec320;
  for (int i = 0; i < 10; i++) {
    vec320.push_back({});
  }
  void* ptr2 = iua::allocate(320);
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(158));
  }

  // Helper to ensure all allocations are freed at the end
  auto deleter = [](void* data) { iua::deallocate(data); };
  std::vector<std::unique_ptr<void, decltype(deleter)>> ptrVec;
  auto alloc = [&ptrVec, &deleter](size_t size) {
    ptrVec.emplace_back(iua::allocate(size), deleter);
  };

  for (int i = 0; i < 10; i++) {
    alloc(kb(1));
  }
  void* ptr3 = iua::allocate(kb(1));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(158));
  }
  for (int i = 0; i < 10; i++) {
    alloc(kb(4));
  }
  void* ptr4 = iua::allocate(kb(4));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(158));
  }
  for (int i = 0; i < 10; i++) {
    alloc(kb(10));
  }
  void* ptr5 = iua::allocate(kb(10));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(158));
  }
  alloc(kb(512));
  alloc(mb(1));
  void* ptr6 = iua::allocate(mb(1));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(156));
  }
  alloc(mb(2));
  alloc(mb(4));
  void* ptr7 = iua::allocate(mb(4));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(146));
  }
  alloc(kb(512));
  alloc(kb(512));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(145));
  }
  void* ptr8 = iua::allocate(mb(64));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(80));
  }
  alloc(mb(64));
  if (initialized) {
    EXPECT_GE(iua::freeSpace(), mb(16));
  }
  alloc(mb(256));
  alloc(mb(256));
  alloc(mb(256));

  // Now free a bunch of objects and then reallocate
  // the same size objects again.
  // This should not result in usage of free space.
  size_t freeSpaceBefore = iua::freeSpace();
  iua::deallocate(ptr1);
  iua::deallocate(ptr2);
  iua::deallocate(ptr3);
  iua::deallocate(ptr4);
  iua::deallocate(ptr5);
  iua::deallocate(ptr6);
  iua::deallocate(ptr7);
  iua::deallocate(ptr8);
  alloc(32);
  alloc(320);
  alloc(kb(1));
  alloc(kb(4));
  alloc(kb(10));
  alloc(mb(1));
  alloc(mb(4));
  alloc(mb(64));

  if (initialized) {
    EXPECT_EQ(freeSpaceBefore, iua::freeSpace());
  }
}

TEST(IoUringArenaTest, STLAllocator) {
  using MyVecAllocator = folly::CxxIoUringAllocator<int>;
  using MyVec = std::vector<int, MyVecAllocator>;

  using MyMapAllocator =
      folly::CxxIoUringAllocator<folly::f14::detail::MapValueType<int, MyVec>>;
  using MyMap = folly::F14FastMap<
      int,
      MyVec,
      folly::f14::DefaultHasher<int>,
      folly::f14::DefaultKeyEqual<int>,
      MyMapAllocator>;

  MyVec vec;
  // This should work, just won't use arena since
  // init hasn't been called yet
  vec.resize(100);
  EXPECT_NE(nullptr, vec.data());

  // Reserve & initialize, not in arena
  MyVec vec2(100);
  EXPECT_NE(nullptr, vec.data());

  // F14 maps need quite a lot of memory by default
  bool initialized = iua::init(mb(8));
  if (initialized) {
    EXPECT_NE(0, iua::freeSpace());
  }

  // Reallocate, this time in arena
  vec.resize(200);
  EXPECT_NE(nullptr, vec.data());

  MyMap map1;
  map1[0] = {1, 2, 3};
  auto map2_ptr = std::make_unique<MyMap>();
  MyMap& map2 = *map2_ptr;
  map2[0] = {1, 2, 3};

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(vec.data()));
    EXPECT_TRUE(iua::addressInArena(&map1[0]));
    EXPECT_TRUE(iua::addressInArena(&map1[0][0]));
    EXPECT_TRUE(iua::addressInArena(&map2[0]));
    EXPECT_TRUE(iua::addressInArena(&map2[0][0]));
  }

  // This will be in the arena
  map1[0] = std::move(vec);

  // But not this, since vec2 content was allocated before init
  map1[1] = std::move(vec2);

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(&map1[0]));
    EXPECT_TRUE(iua::addressInArena(&map1[1]));
    EXPECT_TRUE(iua::addressInArena(&map1[0][0]));
    EXPECT_FALSE(iua::addressInArena(&map1[1][0]));
  }

  // realloc in arena
  map1[1].reserve(200);

  if (initialized) {
    EXPECT_TRUE(iua::addressInArena(&map1[1][0]));
  }
}
