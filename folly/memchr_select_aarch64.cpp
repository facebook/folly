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

#include <string.h>
#include <cstddef>
#include <cstdint>

#if defined(__linux__) && defined(__aarch64__)

#include <asm/hwcap.h> // @manual

#if defined(__has_include)
#if __has_include(<sys/ifunc.h>)
#include <sys/ifunc.h>
#endif
#endif

extern "C" {

void* __folly_memchr_long_aarch64(void* dst, int c, std::size_t size);
void* __folly_memchr_long_aarch64_simd(void* dst, int c, std::size_t size);

decltype(&__folly_memchr_long_aarch64) __folly_detail_memchr_long_resolve(
    uint64_t hwcaps, const void* arg2) {
#if defined(_IFUNC_ARG_HWCAP)
    if (hwcaps & HWCAP_SHA3) {
        return __folly_memchr_long_aarch64_simd;
    }
#endif

  return __folly_memchr_long_aarch64;
}

[[gnu::ifunc("__folly_detail_memchr_long_resolve")]]
void* memchr_long(void* dst, const int c, std::size_t size);

} // extern "C"

#endif // defined(__linux__) && defined(__aarch64__)
