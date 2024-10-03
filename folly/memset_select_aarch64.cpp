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

/*
 * How on earth does this work?
 *
 * See memcpy_select_aarch64.cpp for a full discussion.
 */

#include <cstddef>
#include <cstdint>

#if defined(__linux__) && defined(__aarch64__)

#include <asm/hwcap.h> // @manual

#if defined(__has_include)
#if __has_include(<sys/ifunc.h>)
#include <sys/ifunc.h>
#endif
#endif

#if !defined(HWCAP2_MOPS)
#define HWCAP2_MOPS (1UL << 43)
#endif

extern "C" {

void* __folly_memset_aarch64_mops(void* dest, int ch, std::size_t count);
void* __folly_memset_aarch64_simd(void* dest, int ch, std::size_t count);

[[gnu::no_sanitize_address]]
decltype(&__folly_memset_aarch64_simd) __folly_detail_memset_resolve(
    uint64_t hwcaps, const void* arg2) {
#if defined(_IFUNC_ARG_HWCAP)
  if (hwcaps & _IFUNC_ARG_HWCAP && arg2 != nullptr) {
    const __ifunc_arg_t* args = reinterpret_cast<const __ifunc_arg_t*>(arg2);
    if (args->_hwcap2 & HWCAP2_MOPS) {
      return __folly_memset_aarch64_mops;
    }
  }
#endif

  return __folly_memset_aarch64_simd;
}

[[gnu::ifunc("__folly_detail_memset_resolve")]]
void* __folly_memset(void* dest, int ch, std::size_t count);

#ifdef FOLLY_MEMSET_IS_MEMSET

[[gnu::weak, gnu::alias("__folly_memset")]]
void* memset(void* dest, int ch, std::size_t count);

#endif

} // extern "C"

#endif // defined(__linux__) && defined(__aarch64__)
