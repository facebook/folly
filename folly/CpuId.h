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
#include <cstring>

#include <folly/Portability.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace folly {

/**
 * Identification of an Intel CPU.
 * Supports CPUID feature flags (EAX=1) and extended features (EAX=7, ECX=0).
 * Values from
 * http://www.intel.com/content/www/us/en/processors/processor-identification-cpuid-instruction-note.html
 */
class CpuId {
 public:
  // Always inline in order for this to be usable from a __ifunc__.
  // In shared library mode, a __ifunc__ runs at relocation time, while the
  // PLT hasn't been fully populated yet; thus, ifuncs cannot use symbols
  // with potentially external linkage. (This issue is less likely in opt
  // mode since inlining happens more likely, and it doesn't happen for
  // statically linked binaries which don't depend on the PLT)
  FOLLY_ALWAYS_INLINE CpuId() {
#if defined(_MSC_VER) && (FOLLY_X64 || defined(_M_IX86))
#if !defined(__clang__)
    int reg[4];
    __cpuid(static_cast<int*>(reg), 0);
    vendor_[0] = (uint32_t)reg[1];
    vendor_[1] = (uint32_t)reg[3];
    vendor_[2] = (uint32_t)reg[2];
    const int n = reg[0];
    if (n >= 1) {
      __cpuid(static_cast<int*>(reg), 1);
      f1c_ = uint32_t(reg[2]);
      f1d_ = uint32_t(reg[3]);
    }
    if (n >= 7) {
      __cpuidex(static_cast<int*>(reg), 7, 0);
      f7b_ = uint32_t(reg[1]);
      f7c_ = uint32_t(reg[2]);
      f7d_ = uint32_t(reg[3]);
    }
#else
    // Clang compiler has a bug (fixed in https://reviews.llvm.org/D101338) in
    // which the `__cpuid` intrinsic does not save and restore `rbx` as it needs
    // to due to being a reserved register. So in that case, do the `cpuid`
    // ourselves. Clang supports inline assembly anyway.
    uint32_t n;
    uint32_t v0b, v0d, v0c;
    __asm__(
        "pushq %%rbx\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "popq %%rbx\n\t"
        : "=a"(n), "=r"(v0b), "=d"(v0d), "=c"(v0c)
        : "a"(0));
    vendor_[0] = v0b;
    vendor_[1] = v0d;
    vendor_[2] = v0c;
    if (n >= 1) {
      uint32_t f1a;
      __asm__(
          "pushq %%rbx\n\t"
          "cpuid\n\t"
          "popq %%rbx\n\t"
          : "=a"(f1a), "=c"(f1c_), "=d"(f1d_)
          : "a"(1)
          :);
    }
    if (n >= 7) {
      __asm__(
          "pushq %%rbx\n\t"
          "cpuid\n\t"
          "movq %%rbx, %%rax\n\t"
          "popq %%rbx"
          : "=a"(f7b_), "=c"(f7c_), "=d"(f7d_)
          : "a"(7), "c"(0));
    }
#endif
#elif defined(__i386__) && defined(__PIC__) && !defined(__clang__) && \
    defined(__GNUC__)
    // The following block like the normal cpuid branch below, but gcc
    // reserves ebx for use of its pic register so we must specially
    // handle the save and restore to avoid clobbering the register
    uint32_t n;
    uint32_t v0b, v0d, v0c;
    __asm__(
        "pushl %%ebx\n\t"
        "cpuid\n\t"
        "movl %%ebx, %1\n\t"
        "popl %%ebx\n\t"
        : "=a"(n), "=r"(v0b), "=d"(v0d), "=c"(v0c)
        : "a"(0));
    vendor_[0] = v0b;
    vendor_[1] = v0d;
    vendor_[2] = v0c;
    if (n >= 1) {
      uint32_t f1a;
      __asm__(
          "pushl %%ebx\n\t"
          "cpuid\n\t"
          "popl %%ebx\n\t"
          : "=a"(f1a), "=c"(f1c_), "=d"(f1d_)
          : "a"(1)
          :);
    }
    if (n >= 7) {
      __asm__(
          "pushl %%ebx\n\t"
          "cpuid\n\t"
          "movl %%ebx, %%eax\n\t"
          "popl %%ebx"
          : "=a"(f7b_), "=c"(f7c_), "=d"(f7d_)
          : "a"(7), "c"(0));
    }
#elif FOLLY_X64 || defined(__i386__)
    uint32_t n;
    uint32_t v0b, v0d, v0c;
    __asm__("cpuid" : "=a"(n), "=b"(v0b), "=d"(v0d), "=c"(v0c) : "a"(0));
    vendor_[0] = v0b;
    vendor_[1] = v0d;
    vendor_[2] = v0c;
    if (n >= 1) {
      uint32_t f1a;
      __asm__("cpuid" : "=a"(f1a), "=c"(f1c_), "=d"(f1d_) : "a"(1) : "ebx");
    }
    if (n >= 7) {
      uint32_t f7a;
      __asm__("cpuid"
              : "=a"(f7a), "=b"(f7b_), "=c"(f7c_), "=d"(f7d_)
              : "a"(7), "c"(0));
    }
#endif
  }

#define FOLLY_DETAIL_CPUID_X(name, r, bit) \
  FOLLY_ALWAYS_INLINE bool name() const { return ((r) & (1U << bit)) != 0; }

// cpuid(1): Processor Info and Feature Bits.
#define FOLLY_DETAIL_CPUID_C(name, bit) FOLLY_DETAIL_CPUID_X(name, f1c_, bit)
  FOLLY_DETAIL_CPUID_C(sse3, 0)
  FOLLY_DETAIL_CPUID_C(pclmuldq, 1)
  FOLLY_DETAIL_CPUID_C(dtes64, 2)
  FOLLY_DETAIL_CPUID_C(monitor, 3)
  FOLLY_DETAIL_CPUID_C(dscpl, 4)
  FOLLY_DETAIL_CPUID_C(vmx, 5)
  FOLLY_DETAIL_CPUID_C(smx, 6)
  FOLLY_DETAIL_CPUID_C(eist, 7)
  FOLLY_DETAIL_CPUID_C(tm2, 8)
  FOLLY_DETAIL_CPUID_C(ssse3, 9)
  FOLLY_DETAIL_CPUID_C(cnxtid, 10)
  FOLLY_DETAIL_CPUID_C(fma, 12)
  FOLLY_DETAIL_CPUID_C(cx16, 13)
  FOLLY_DETAIL_CPUID_C(xtpr, 14)
  FOLLY_DETAIL_CPUID_C(pdcm, 15)
  FOLLY_DETAIL_CPUID_C(pcid, 17)
  FOLLY_DETAIL_CPUID_C(dca, 18)
  FOLLY_DETAIL_CPUID_C(sse41, 19)
  FOLLY_DETAIL_CPUID_C(sse42, 20)
  FOLLY_DETAIL_CPUID_C(x2apic, 21)
  FOLLY_DETAIL_CPUID_C(movbe, 22)
  FOLLY_DETAIL_CPUID_C(popcnt, 23)
  FOLLY_DETAIL_CPUID_C(tscdeadline, 24)
  FOLLY_DETAIL_CPUID_C(aes, 25)
  FOLLY_DETAIL_CPUID_C(xsave, 26)
  FOLLY_DETAIL_CPUID_C(osxsave, 27)
  FOLLY_DETAIL_CPUID_C(avx, 28)
  FOLLY_DETAIL_CPUID_C(f16c, 29)
  FOLLY_DETAIL_CPUID_C(rdrand, 30)
#undef FOLLY_DETAIL_CPUID_C
#define FOLLY_DETAIL_CPUID_D(name, bit) FOLLY_DETAIL_CPUID_X(name, f1d_, bit)
  FOLLY_DETAIL_CPUID_D(fpu, 0)
  FOLLY_DETAIL_CPUID_D(vme, 1)
  FOLLY_DETAIL_CPUID_D(de, 2)
  FOLLY_DETAIL_CPUID_D(pse, 3)
  FOLLY_DETAIL_CPUID_D(tsc, 4)
  FOLLY_DETAIL_CPUID_D(msr, 5)
  FOLLY_DETAIL_CPUID_D(pae, 6)
  FOLLY_DETAIL_CPUID_D(mce, 7)
  FOLLY_DETAIL_CPUID_D(cx8, 8)
  FOLLY_DETAIL_CPUID_D(apic, 9)
  FOLLY_DETAIL_CPUID_D(sep, 11)
  FOLLY_DETAIL_CPUID_D(mtrr, 12)
  FOLLY_DETAIL_CPUID_D(pge, 13)
  FOLLY_DETAIL_CPUID_D(mca, 14)
  FOLLY_DETAIL_CPUID_D(cmov, 15)
  FOLLY_DETAIL_CPUID_D(pat, 16)
  FOLLY_DETAIL_CPUID_D(pse36, 17)
  FOLLY_DETAIL_CPUID_D(psn, 18)
  FOLLY_DETAIL_CPUID_D(clfsh, 19)
  FOLLY_DETAIL_CPUID_D(ds, 21)
  FOLLY_DETAIL_CPUID_D(acpi, 22)
  FOLLY_DETAIL_CPUID_D(mmx, 23)
  FOLLY_DETAIL_CPUID_D(fxsr, 24)
  FOLLY_DETAIL_CPUID_D(sse, 25)
  FOLLY_DETAIL_CPUID_D(sse2, 26)
  FOLLY_DETAIL_CPUID_D(ss, 27)
  FOLLY_DETAIL_CPUID_D(htt, 28)
  FOLLY_DETAIL_CPUID_D(tm, 29)
  FOLLY_DETAIL_CPUID_D(pbe, 31)
#undef FOLLY_DETAIL_CPUID_D

  // cpuid(7): Extended Features.
#define FOLLY_DETAIL_CPUID_B(name, bit) FOLLY_DETAIL_CPUID_X(name, f7b_, bit)
  FOLLY_DETAIL_CPUID_B(bmi1, 3)
  FOLLY_DETAIL_CPUID_B(hle, 4)
  FOLLY_DETAIL_CPUID_B(avx2, 5)
  FOLLY_DETAIL_CPUID_B(smep, 7)
  FOLLY_DETAIL_CPUID_B(bmi2, 8)
  FOLLY_DETAIL_CPUID_B(erms, 9)
  FOLLY_DETAIL_CPUID_B(invpcid, 10)
  FOLLY_DETAIL_CPUID_B(rtm, 11)
  FOLLY_DETAIL_CPUID_B(mpx, 14)
  FOLLY_DETAIL_CPUID_B(avx512f, 16)
  FOLLY_DETAIL_CPUID_B(avx512dq, 17)
  FOLLY_DETAIL_CPUID_B(rdseed, 18)
  FOLLY_DETAIL_CPUID_B(adx, 19)
  FOLLY_DETAIL_CPUID_B(smap, 20)
  FOLLY_DETAIL_CPUID_B(avx512ifma, 21)
  FOLLY_DETAIL_CPUID_B(pcommit, 22)
  FOLLY_DETAIL_CPUID_B(clflushopt, 23)
  FOLLY_DETAIL_CPUID_B(clwb, 24)
  FOLLY_DETAIL_CPUID_B(avx512pf, 26)
  FOLLY_DETAIL_CPUID_B(avx512er, 27)
  FOLLY_DETAIL_CPUID_B(avx512cd, 28)
  FOLLY_DETAIL_CPUID_B(sha, 29)
  FOLLY_DETAIL_CPUID_B(avx512bw, 30)
  FOLLY_DETAIL_CPUID_B(avx512vl, 31)
#undef FOLLY_DETAIL_CPUID_B
#define FOLLY_DETAIL_CPUID_C(name, bit) FOLLY_DETAIL_CPUID_X(name, f7c_, bit)
  FOLLY_DETAIL_CPUID_C(prefetchwt1, 0)
  FOLLY_DETAIL_CPUID_C(avx512vbmi, 1)
  FOLLY_DETAIL_CPUID_C(avx512vbmi2, 6)
  FOLLY_DETAIL_CPUID_C(vaes, 9)
  FOLLY_DETAIL_CPUID_C(vpclmulqdq, 10)
  FOLLY_DETAIL_CPUID_C(avx512vnni, 11)
  FOLLY_DETAIL_CPUID_C(avx512bitalg, 12)
  FOLLY_DETAIL_CPUID_C(avx512vpopcntdq, 14)
  FOLLY_DETAIL_CPUID_C(rdpid, 22)
#undef FOLLY_DETAIL_CPUID_C
#define FOLLY_DETAIL_CPUID_D(name, bit) FOLLY_DETAIL_CPUID_X(name, f7d_, bit)
  FOLLY_DETAIL_CPUID_D(avx5124vnniw, 2)
  FOLLY_DETAIL_CPUID_D(avx5124fmaps, 3)
  FOLLY_DETAIL_CPUID_D(avx512vp2intersect, 8)
  FOLLY_DETAIL_CPUID_D(amxbf16, 22)
  FOLLY_DETAIL_CPUID_D(avx512fp16, 23)
  FOLLY_DETAIL_CPUID_D(amxtile, 24)
  FOLLY_DETAIL_CPUID_D(amxint8, 25)
#undef FOLLY_DETAIL_CPUID_D

#undef FOLLY_DETAIL_CPUID_X

#define FOLLY_DETAIL_VENDOR(name, str)                         \
  FOLLY_ALWAYS_INLINE bool vendor_##name() const {             \
    /* Size of str should be 12 + NUL terminator. */           \
    static_assert(sizeof(str) == 13, "Bad CPU Vendor string"); \
    /* Just as with the main CpuId call above, this can also   \
    still be in an __ifunc__, so no function calls :( */       \
    return memcmp(&vendor_[0], &str[0], 12) == 0;              \
  }

  FOLLY_DETAIL_VENDOR(intel, "GenuineIntel")
  FOLLY_DETAIL_VENDOR(amd, "AuthenticAMD")

#undef FOLLY_DETAIL_VENDOR

 private:
  uint32_t vendor_[3] = {0};
  uint32_t f1c_ = 0;
  uint32_t f1d_ = 0;
  uint32_t f7b_ = 0;
  uint32_t f7c_ = 0;
  uint32_t f7d_ = 0;
};

} // namespace folly
