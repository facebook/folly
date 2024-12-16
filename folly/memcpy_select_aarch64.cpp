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
 * We rely on a handful of functions from the Arm Optimised Routines
 * project. Specifically, there are implementations for common operations
 * like memcpy for various combinations of aarch64 features.
 *
 * The minor challenge we have is that older compilers don't support newer
 * extensions at all, so we try to detect known-broken cases in the same way
 * as the Optimised Routines library does.
 *
 * That aside, each of the optimised routines implementations turns on the
 * architecture extensions it needs in the source file, which is how we get
 * SVE support without having requested it on our command-line.
 *
 * At runtime we use a GNU IFUNC with a resolver to choose the most performant
 * implementation based on the CPU features presented to us in the aux data
 * by the kernel.
 *
 * Finally, we alias memcpy and memmove to our implementations when instructed
 * to do so.
 *
 * In future we can add the mops (memory operations) extension implementation,
 * but that's very new and doesn't yet have broad compiler or auxval support.
 *
 * A note on the ifunc resolver ABI: This is almost entirely undocumented. On
 * aarch64 the value of AUX_HWCAP is passed as the first argument to the
 * function. There's supposed to be an extension that passes AUX_HWWCAP2 as
 * the second argument, but I'm struggling to track down when that was
 * implemented or how to detect it. This is all important because we cannot
 * call any library functions (like getauxval) in a resolver function, and
 * it's unsafe to query ISAR registers without having checked AUX_HWCAP to
 * see if those are callable by userspace.
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

void* __folly_memcpy_aarch64(void* dst, const void* src, std::size_t size);
void* __folly_memcpy_aarch64_mops(void* dst, const void* src, std::size_t size);
void* __folly_memcpy_aarch64_simd(void* dst, const void* src, std::size_t size);
void* __folly_memcpy_aarch64_sve(void* dst, const void* src, std::size_t size);

void* __folly_memmove_aarch64(void* dst, const void* src, std::size_t len);
void* __folly_memmove_aarch64_mops(void* dst, const void* src, std::size_t len);
void* __folly_memmove_aarch64_simd(void* dst, const void* src, std::size_t len);
void* __folly_memmove_aarch64_sve(void* dst, const void* src, std::size_t len);

[[gnu::no_sanitize_address]]
decltype(&__folly_memcpy_aarch64) __folly_detail_memcpy_resolve(
    uint64_t hwcaps, const void* arg2) {
#if defined(_IFUNC_ARG_HWCAP)
  if (hwcaps & _IFUNC_ARG_HWCAP && arg2 != nullptr) {
    const __ifunc_arg_t* args = reinterpret_cast<const __ifunc_arg_t*>(arg2);
    if (args->_hwcap2 & HWCAP2_MOPS) {
      return __folly_memcpy_aarch64_mops;
    }
  }
#endif

  if (hwcaps & HWCAP_SVE) {
    return __folly_memcpy_aarch64_sve;
  }

  if (hwcaps & HWCAP_ASIMD) {
    return __folly_memcpy_aarch64_simd;
  }

  return __folly_memcpy_aarch64;
}

[[gnu::no_sanitize_address]]
decltype(&__folly_memmove_aarch64) __folly_detail_memmove_resolve(
    uint64_t hwcaps, const void* arg2) {
#if defined(_IFUNC_ARG_HWCAP)
  if (hwcaps & _IFUNC_ARG_HWCAP && arg2 != nullptr) {
    const __ifunc_arg_t* args = reinterpret_cast<const __ifunc_arg_t*>(arg2);
    if (args->_hwcap2 & HWCAP2_MOPS) {
      return __folly_memmove_aarch64_mops;
    }
  }
#endif

  if (hwcaps & HWCAP_SVE) {
    return __folly_memmove_aarch64_sve;
  }

  if (hwcaps & HWCAP_ASIMD) {
    return __folly_memmove_aarch64_simd;
  }

  return __folly_memmove_aarch64;
}

[[gnu::ifunc("__folly_detail_memcpy_resolve")]]
void* __folly_memcpy(void* dst, const void* src, std::size_t size);

[[gnu::ifunc("__folly_detail_memmove_resolve")]]
void* __folly_memmove(void* dst, const void* src, std::size_t size);

#ifdef FOLLY_MEMCPY_IS_MEMCPY

[[gnu::weak, gnu::alias("__folly_memcpy")]]
void* memcpy(void* dst, const void* src, std::size_t size);

[[gnu::weak, gnu::alias("__folly_memmove")]]
void* memmove(void* dst, const void* src, std::size_t size);

#endif

} // extern "C"

#endif // defined(__linux__) && defined(__aarch64__)
