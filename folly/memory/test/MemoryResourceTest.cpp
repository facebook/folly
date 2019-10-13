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

#include <folly/memory/MemoryResource.h>

#include <folly/portability/GTest.h>

#if FOLLY_HAS_MEMORY_RESOURCE

TEST(MemoryResource, simple) {
  using Alloc = folly::detail::std_pmr::polymorphic_allocator<char>;

  std::vector<char, Alloc> v{
      Alloc{folly::detail::std_pmr::null_memory_resource()}};
  EXPECT_THROW(v.push_back('x'), std::bad_alloc);
}

#endif // FOLLY_HAS_MEMORY_RESOURCE
