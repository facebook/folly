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

#include <folly/memory/IOBufArenaFactory.h>

#include <folly/memory/IoUringArena.h>
#include <folly/memory/JemallocHugePageAllocator.h>

#include <folly/portability/GTest.h>

using iua = folly::IoUringArena;
using jha = folly::JemallocHugePageAllocator;

static constexpr size_t kb(size_t kilos) {
  return kilos * 1024;
}

static constexpr size_t mb(size_t megs) {
  return kb(megs * 1024);
}

TEST(IOBufArenaFactoryTest, IoUringArena) {
  bool initialized = iua::init(mb(4));
  if (!initialized) {
    GTEST_SKIP() << "IoUringArena initialization not supported";
  }

  auto factory = folly::memory::makeIOBufArenaFactory<iua>();
  auto buf = factory(kb(4));

  ASSERT_NE(nullptr, buf);
  EXPECT_TRUE(iua::addressInArena(buf->writableBuffer()));
}

TEST(IOBufArenaFactoryTest, JemallocHugePageAllocator) {
  bool initialized = jha::init(2);
  if (!initialized) {
    GTEST_SKIP() << "JemallocHugePageAllocator initialization not supported";
  }

  auto factory = folly::memory::makeIOBufArenaFactory<jha>();
  auto buf = factory(kb(4));

  ASSERT_NE(nullptr, buf);
  EXPECT_TRUE(jha::addressInArena(buf->writableBuffer()));
}

TEST(IOBufArenaFactoryTest, ThrowsWhenNotInitialized) {
  struct UninitializedAllocator {
    static bool initialized() { return false; }
    static void* allocate(size_t) { return nullptr; }
  };

  auto factory = folly::memory::makeIOBufArenaFactory<UninitializedAllocator>();
  EXPECT_THROW(factory(kb(1)), std::runtime_error);
}

TEST(IOBufArenaFactoryTest, ThrowsOnNullAllocation) {
  struct NullAllocator {
    static bool initialized() { return true; }
    static void* allocate(size_t) { return nullptr; }
  };

  auto factory = folly::memory::makeIOBufArenaFactory<NullAllocator>();
  EXPECT_THROW(factory(kb(1)), std::bad_alloc);
}
