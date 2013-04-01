/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/Memory.h"
#include "folly/Arena.h"
#include "folly/String.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <type_traits>

using namespace folly;

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

int main(int argc, char **argv) {
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
