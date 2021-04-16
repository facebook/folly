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

#include <folly/memory/SanitizeAddress.h>

#include <folly/lang/Extern.h>

//  Address Sanitizer interface may be found at:
//    https://github.com/llvm-mirror/compiler-rt/blob/master/include/sanitizer/asan_interface.h
extern "C" void* __asan_region_is_poisoned(void*, std::size_t);

namespace folly {
namespace detail {

FOLLY_CREATE_EXTERN_ACCESSOR(
    asan_region_is_poisoned_access_v, __asan_region_is_poisoned);

void* asan_region_is_poisoned_(void* const ptr, std::size_t len) {
  constexpr auto fun =
      asan_region_is_poisoned_access_v<kIsLibrarySanitizeAddress>;
  return fun ? fun(ptr, len) : nullptr;
}

} // namespace detail
} // namespace folly
