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

#include <folly/container/vector_bool.h>

#include <folly/Memory.h>
#include <folly/container/detail/BoolWrapper.h>
#include <folly/memory/MemoryResource.h>
#include <folly/portability/GTest.h>

namespace folly {

TEST(VectorBoolTest, Example) {
  folly::vector_bool<> vec = {false, true};
  EXPECT_FALSE(vec[0]);
  EXPECT_TRUE(vec[1]);
}

TEST(VectorBoolTest, CustomAllocator) {
  folly::vector_bool<SysAllocator> vec = {false, true};
  EXPECT_FALSE(vec[0]);
  EXPECT_TRUE(vec[1]);
}

#if FOLLY_HAS_MEMORY_RESOURCE
TEST(VectorBoolTest, PmrAllocator) {
  folly::pmr::vector_bool vec = {false, true};
  EXPECT_FALSE(vec[0]);
  EXPECT_TRUE(vec[1]);
}
#endif

} // namespace folly
