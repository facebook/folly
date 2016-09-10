/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Memory.h>
#include <folly/Arena.h>
#include <folly/String.h>
#include <folly/portability/GTest.h>

#include <glog/logging.h>

#include <type_traits>

using namespace folly;

TEST(make_unique, compatible_with_std_make_unique) {
  //  HACK: To enforce that `folly::` is imported here.
  to_shared_ptr(std::unique_ptr<std::string>());

  using namespace std;
  make_unique<string>("hello, world");
}

TEST(allocate_sys_buffer, compiles) {
  auto buf = allocate_sys_buffer(256);
  //  Freed at the end of the scope.
}

template <std::size_t> struct T {};
template <std::size_t> struct S {};
template <std::size_t> struct P {};

TEST(as_stl_allocator, sanity_check) {
  typedef StlAllocator<SysArena, int> stl_arena_alloc;

  EXPECT_TRUE((std::is_same<
    as_stl_allocator<int, SysArena>::type,
    stl_arena_alloc
  >::value));

  EXPECT_TRUE((std::is_same<
    as_stl_allocator<int, stl_arena_alloc>::type,
    stl_arena_alloc
  >::value));
}

TEST(StlAllocator, void_allocator) {
  typedef StlAllocator<SysArena, void> void_allocator;
  SysArena arena;
  void_allocator valloc(&arena);

  typedef void_allocator::rebind<int>::other int_allocator;
  int_allocator ialloc(valloc);

  auto i = std::allocate_shared<int>(ialloc, 10);
  ASSERT_NE(nullptr, i.get());
  EXPECT_EQ(10, *i);
  i.reset();
  ASSERT_EQ(nullptr, i.get());
}

TEST(rebind_allocator, sanity_check) {
  std::allocator<long> alloc;

  auto i = std::allocate_shared<int>(
    rebind_allocator<int, decltype(alloc)>(alloc), 10
  );
  ASSERT_NE(nullptr, i.get());
  EXPECT_EQ(10, *i);
  i.reset();
  ASSERT_EQ(nullptr, i.get());

  auto d = std::allocate_shared<double>(
    rebind_allocator<double>(alloc), 5.6
  );
  ASSERT_NE(nullptr, d.get());
  EXPECT_EQ(5.6, *d);
  d.reset();
  ASSERT_EQ(nullptr, d.get());

  auto s = std::allocate_shared<std::string>(
    rebind_allocator<std::string>(alloc), "HELLO, WORLD"
  );
  ASSERT_NE(nullptr, s.get());
  EXPECT_EQ("HELLO, WORLD", *s);
  s.reset();
  ASSERT_EQ(nullptr, s.get());
}
