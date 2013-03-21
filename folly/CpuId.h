/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_CPUID_H_
#define FOLLY_CPUID_H_

#include <cstdint>

namespace folly {

/**
 * Identification of an Intel CPU.
 * Supports CPUID (EAX=1) feature flags.
 * Values from http://www.intel.com/content/www/us/en/processors/processor-identification-cpuid-instruction-note.html
 */
class CpuId {
 public:
  CpuId() {
#if defined(__x86_64__) || defined(__i386__)
    __asm__("cpuid" : "=c"(c_), "=d"(d_) : "a"(1) : "ebx");
#else
    // On non-Intel, none of these features exist; at least not in the same form
    // as they do on Intel
    c_ = 0;
    d_ = 0;
#endif
  }
#define X(name, r, bit) bool name() const { return r & (1U << bit); }
#define C(name, bit) X(name, c_, bit)
#define D(name, bit) X(name, d_, bit)
  C(sse3, 0)
  C(pclmuldq, 1)
  C(dtes64, 2)
  C(monitor, 3)
  C(dscpl, 4)
  C(vmx, 5)
  C(smx, 6)
  C(eist, 7)
  C(tm2, 8)
  C(ssse3, 9)
  C(cnxtid, 10)
  // 11 is reserved
  C(fma, 12)
  C(cx16, 13)
  C(xtpr, 14)
  C(pdcm, 15)
  // 16 is reserved
  C(pcid, 17)
  C(dca, 18)
  C(sse41, 19)
  C(sse42, 20)
  C(x2apic, 21)
  C(movbe, 22)
  C(popcnt, 23)
  C(tscdeadline, 24)
  C(aes, 25)
  C(xsave, 26)
  C(osxsave, 27)
  C(avx, 28)
  C(f16c, 29)
  C(rdrand, 30)
  // 31 is not used
  D(fpu, 0)
  D(vme, 1)
  D(de, 2)
  D(pse, 3)
  D(tsc, 4)
  D(msr, 5)
  D(pae, 6)
  D(mce, 7)
  D(cx8, 8)
  D(apic, 9)
  // 10 is reserved
  D(sep, 11)
  D(mtrr, 12)
  D(pge, 13)
  D(mca, 14)
  D(cmov, 15)
  D(pat, 16)
  D(pse36, 17)
  D(psn, 18)
  D(clfsh, 19)
  // 20 is reserved
  D(ds, 21)
  D(acpi, 22)
  D(mmx, 23)
  D(fxsr, 24)
  D(sse, 25)
  D(sse2, 26)
  D(ss, 27)
  D(htt, 28)
  D(tm, 29)
  // 30 is reserved
  D(pbe, 31)
#undef D
#undef C
#undef X
 private:
  uint32_t c_;  // ECX
  uint32_t d_;  // EDX
};

}  // namespace folly

#endif /* FOLLY_CPUID_H_ */

