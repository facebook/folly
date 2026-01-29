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

#pragma once

#include <cstdint>
#include <cstdlib>

#include <folly/lang/Exception.h>
#include <folly/memory/Malloc.h>

namespace folly {
namespace detail {

class MallocAlloc {
 public:
  void* allocate(size_t size) {
    void* p = std::malloc(size);
    if (p == nullptr) {
      throw_exception<std::bad_alloc>();
    }
    return p;
  }

  void deallocate(void* p, size_t size) { sizedFree(p, size); }
};

template <typename Allocator>
struct GivesZeroFilledMemory : public std::false_type {};

/*
 * Example of how to specialize GivesZeroFilledMemory for custom allocator.
 *
 * template <>
 * struct GivesZeroFilledMemory<Alloc> : public std::true_type {};
 */

} // namespace detail
} // namespace folly
