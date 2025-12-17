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

#include <cassert>
#include <cstdint>
#include <cstring>

#include <folly/Portability.h>

#if FOLLY_X86 || FOLLY_X64
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace folly {

/// x86_cpuid
///
/// Wrapper around x86 instruction cpuid.
/// * Some platforms have intrinsics but not all do.
/// * The instruction is tricky to handle in some cases.
///
/// The instruction takes its arguments in eax, ecx and produces its results in
/// eax, ebx, ecx, edx. So it is rather straightforward to wrap. But, a wrinkle:
/// x86 PIC code uses ebx as the PIC register, so it must be preserved around
/// the cpuid instruction.
///
/// This function might be used in __ifunc__ code, which runs concurrently with
/// relocations. So using symbols with potentially external linkage, such as
/// calling functions that might be relocated, is forbidden. Address this by
/// by marking as [[always_inline]].
///
/// multi-thread-safe
/// async-signal-safe
/// reentrancy-safe
/// ifunc-safe
FOLLY_ALWAYS_INLINE void x86_cpuid(
    unsigned int info[4],
    [[maybe_unused]] unsigned int leaf,
    [[maybe_unused]] unsigned int tag = 0) {
#if FOLLY_X86 || FOLLY_X64
#if defined(_MSC_VER)
  __cpuidex(reinterpret_cast<int*>(info), leaf, tag); // no inline asm
#else
  asm volatile(
#if defined(__pic__) && defined(__i386__)
      // ebx is PIC register - must preserve ebx around cpuid
      R"(
        mov %%ebx, %[tmp]
        cpuid
        xchg %%ebx, %[tmp]
      )"
#else
      // ebx is not special
      R"(
        cpuid
      )"
#endif
      : // outputs
      "=a"(info[0]),
#if defined(__pic__) && defined(__i386__)
      [tmp] "=&r"(info[1]), // ebx out was xchg'd to tmp; early clobber
#else
      "=b"(info[1]), // ebx out is in ebx
#endif
      "=c"(info[2]),
      "=d"(info[3])
      : // inputs
      "a"(leaf),
      "c"(tag)
      : // clobbers
  );
#endif
#else
  info[0] = info[1] = info[2] = info[3] = 0;
#endif
}

FOLLY_ALWAYS_INLINE unsigned int x86_cpuid_max( //
    unsigned int leaf,
    unsigned int* sig) {
  unsigned int info[4];
  x86_cpuid(info, leaf);
  if (sig) {
    *sig = info[1]; // ebx
  }
  return info[0]; // eax
}

} // namespace folly
