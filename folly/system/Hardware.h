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

struct x86_cpuid_cache_info {
  static inline constexpr unsigned int id_count_max = 32;

  unsigned int eax, ebx, ecx;

  size_t cache_type() const noexcept { return eax & 0x1F; }
  bool cache_type_data() const noexcept { return cache_type() & 1; }
  bool cache_type_inst() const noexcept { return cache_type() & 2; }
  bool cache_type_null() const noexcept { return !cache_type(); }

  size_t level() const noexcept { return (eax >> 5) & 0x7; }
  size_t line_size() const noexcept { return (ebx & 0xFFF) + 1; }
  size_t partitions() const noexcept { return ((ebx >> 12) & 0x3FF) + 1; }
  size_t ways() const noexcept { return ((ebx >> 22) & 0x3FF) + 1; }
  size_t sets() const noexcept { return ecx + 1; }
  size_t cache_size() const noexcept {
    return ways() * partitions() * line_size() * sets();
  }
};

enum class x86_cpuid_vendor { unknown, intel, amd };

union x86_cpuid_vendor_name {
  char const str[13];
  unsigned int words[3];
};
inline constexpr x86_cpuid_vendor_name x86_cpuid_vendor_names[3] = {
    {},
    {"GenuineIntel"},
    {"AuthenticAMD"},
};

FOLLY_ALWAYS_INLINE x86_cpuid_vendor x86_cpuid_get_vendor() {
  if constexpr (kIsArchX86 || kIsArchAmd64) {
    unsigned int info[4];
    x86_cpuid(info, 0);
    constexpr auto num_names =
        sizeof(x86_cpuid_vendor_names) / sizeof(x86_cpuid_vendor_name);
    for (unsigned int i = 1; i < num_names; ++i) {
      auto& name = x86_cpuid_vendor_names[i].words;
      if (info[1] == name[0] && info[2] == name[2] && info[3] == name[1]) {
        return static_cast<x86_cpuid_vendor>(i);
      }
    }
  }
  return x86_cpuid_vendor::unknown;
}

FOLLY_ALWAYS_INLINE x86_cpuid_cache_info
x86_cpuid_get_cache_info(x86_cpuid_vendor vend, unsigned int id) {
  if constexpr (kIsArchX86 || kIsArchAmd64) {
    unsigned int leaf = 0;
    switch (vend) {
      case x86_cpuid_vendor::intel:
        leaf = 4;
        break;
      case x86_cpuid_vendor::amd:
        leaf = 0x8000001D;
        break;
      case x86_cpuid_vendor::unknown:
      default:
        assert(0 && "unsupported x86 vendor");
    }
    unsigned int info[4];
    x86_cpuid(info, leaf, /* tag = */ id + 1);
    return x86_cpuid_cache_info{info[0], info[1], info[2]};
  }
  return {};
}

FOLLY_ALWAYS_INLINE x86_cpuid_cache_info
x86_cpuid_get_llc_cache_info(x86_cpuid_vendor vend) {
  x86_cpuid_cache_info cache_info{};
  if constexpr (kIsArchX86 || kIsArchAmd64) {
    for (unsigned int i = 0; i < x86_cpuid_cache_info::id_count_max; ++i) {
      auto const info = x86_cpuid_get_cache_info(vend, i);
      if (info.cache_type_data()) {
        cache_info = info;
      }
      if (info.cache_type_null()) {
        break;
      }
    }
  }
  return cache_info;
}

FOLLY_ALWAYS_INLINE x86_cpuid_cache_info x86_cpuid_get_llc_cache_info() {
  auto const vend = x86_cpuid_get_vendor();
  return x86_cpuid_get_llc_cache_info(vend);
}

} // namespace folly
