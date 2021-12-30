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

#include <folly/Portability.h>

namespace folly {

namespace detail {

using asan_mark_memory_region_t = void(void const volatile*, std::size_t);
using asan_region_is_poisoned_t = void*(void* ptr, std::size_t len);
using asan_address_is_poisoned_t = int(void const volatile*);
using sanitizer_start_switch_fiber_t = void(void**, void const*, std::size_t);
using sanitizer_finish_switch_fiber_t = void(void*, void const**, std::size_t*);

extern asan_mark_memory_region_t* const asan_poison_memory_region_v;
extern asan_mark_memory_region_t* const asan_unpoison_memory_region_v;
extern asan_region_is_poisoned_t* const asan_region_is_poisoned_v;
extern asan_address_is_poisoned_t* const asan_address_is_poisoned_v;
extern sanitizer_start_switch_fiber_t* const sanitizer_start_switch_fiber_v;
extern sanitizer_finish_switch_fiber_t* const sanitizer_finish_switch_fiber_v;

} // namespace detail

//  asan_poison_memory_region
//
//  Marks the memory region as poisoned.
//
//  When Address Sanitizer is in force and a memory region is marked poisoned,
//  accesses to any part of the memory region will trap.
//
//  mimic: __asan_poison_memory_region, llvm compiler-rt
FOLLY_ALWAYS_INLINE static void asan_poison_memory_region(
    void const volatile* const addr, std::size_t const size) {
  auto fun = detail::asan_poison_memory_region_v;
  return kIsSanitizeAddress && fun ? fun(addr, size) : void();
}

//  asan_unpoison_memory_region
//
//  Marks the memory region as not poisoned anymore.
//
//  When Address Sanitizer is in force and a memory region is marked poisoned,
//  accesses to any part of the memory region will trap.
//
//  mimic: __asan_poison_memory_region, llvm compiler-rt
FOLLY_ALWAYS_INLINE static void asan_unpoison_memory_region(
    void const volatile* const addr, std::size_t const size) {
  auto fun = detail::asan_unpoison_memory_region_v;
  return kIsSanitizeAddress && fun ? fun(addr, size) : void();
}

//  asan_region_is_poisoned
//
//  Returns the address of the first byte in the region known to be poisoned,
//  or nullptr if there is no such byte. If Address Sanitizer is unavailable,
//  always returns nullptr.
//
//  mimic: __asan_region_is_poisoned, llvm compiler-rt
FOLLY_ALWAYS_INLINE static void* asan_region_is_poisoned(
    void* const addr, std::size_t const size) {
  auto fun = detail::asan_region_is_poisoned_v;
  return kIsSanitizeAddress && fun ? fun(addr, size) : nullptr;
}

//  asan_address_is_poisoned
//
//  Returns whether the address is known to be poisoned. If Address Sanitizer is
//  unavailable, always returns 0.
//
//  mimic: __asan_address_is_poisoned, llvm compiler-rt
FOLLY_ALWAYS_INLINE static int asan_address_is_poisoned(
    void const volatile* const addr) {
  auto fun = detail::asan_address_is_poisoned_v;
  return kIsSanitizeAddress && fun ? fun(addr) : 0;
}

//  sanitizer_start_switch_fiber
//
//  Notifies Address Sanitizer, if available, that a switch to a different stack
//  is imminent.
//
//  mimic: __sanitizer_start_switch_fiber, llvm compiler-rt
FOLLY_ALWAYS_INLINE static void sanitizer_start_switch_fiber(
    void** const save, void const* const bottom, std::size_t const size) {
  auto fun = detail::sanitizer_start_switch_fiber_v;
  return kIsSanitizeAddress && fun ? fun(save, bottom, size) : void();
}

//  sanitizer_finish_switch_fiber
//
//  Notifies Address Sanitizer, if available, that a switch to a different stack
//  is complete.
//
//  mimic: __sanitizer_finish_switch_fiber, llvm compiler-rt
FOLLY_ALWAYS_INLINE static void sanitizer_finish_switch_fiber(
    void* const save, void const** const bottom, std::size_t* const size) {
  auto fun = detail::sanitizer_finish_switch_fiber_v;
  return kIsSanitizeAddress && fun ? fun(save, bottom, size) : void();
}

} // namespace folly
