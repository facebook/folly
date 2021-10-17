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

#pragma once

#include <folly/Portability.h>

namespace folly {

namespace detail {

using asan_region_is_poisoned_t = void*(void* ptr, std::size_t len);
extern asan_region_is_poisoned_t* const asan_region_is_poisoned_v;

} // namespace detail

//  asan_region_is_poisoned
//
//  mimic: __asan_region_is_poisoned, llvm compiler-rt
//
//  Returns the address of the first byte in the region known to be poisoned,
//  or nullptr if there is no such byte. If Address Sanitizer is unavailable,
//  always returns nullptr.
FOLLY_ALWAYS_INLINE static void* asan_region_is_poisoned(
    void* const ptr, std::size_t const len) {
  auto fun = detail::asan_region_is_poisoned_v;
  return kIsSanitizeAddress ? fun(ptr, len) : nullptr;
}

} // namespace folly
