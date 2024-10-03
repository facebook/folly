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
#include <folly/Preprocessor.h>

#if defined(__linux__) && !FOLLY_MOBILE
#include <sys/auxv.h> // @manual
#endif

namespace folly {

/**
 * Identification of hardware capabilities via the ELF auxiliary vector.
 *
 * Exposes the AT_HWCAP vector and, where available, the AT_HWCAP2 vector.
 *
 * DO NOT USE THIS CLASS ON X86_64 MACHINES
 *
 * Glibc massages the values in AT_HWCAP on x86_64 machines in a way that
 * makes little sense to consumers. Use folly::CpuId on x86_64 platforms.
 *
 * For aarch64 the class exposes methods with names derived from the defines
 * found in the Linux source tree (https://fburl.com/wl8qfh30).
 *
 * When compiled on anything other than Linux all methods will return false.
 *
 * The default constructor will call getauxval to retrieve the required
 * auxiliary vector entries. If you wish to use the class from an ifunc
 * resolver you should pass the hwcap and hwcap2 values received by your
 * resolver to the constructor.
 */
class ElfHwCaps {
 public:
  FOLLY_ALWAYS_INLINE ElfHwCaps(uint64_t hwcap, uint64_t hwcap2)
      : hwcap_(hwcap), hwcap2_(hwcap2) {}

  FOLLY_ALWAYS_INLINE ElfHwCaps() {
#if defined(__linux__) && !FOLLY_MOBILE
    hwcap_ = getauxval(AT_HWCAP);
#if defined(AT_HWCAP2)
    hwcap2_ = getauxval(AT_HWCAP2);
#endif
#endif
  }

#define FOLLY_DETAIL_HWCAP_X(arch, name, r, bit)                \
  FOLLY_ALWAYS_INLINE bool FB_CONCATENATE(arch, name)() const { \
    return ((r) & (1UL << (bit))) != 0;                         \
  }
#define FOLLY_DETAIL_HWCAP_NOIMPL_X(arch, name) \
  FOLLY_ALWAYS_INLINE bool FB_CONCATENATE(arch, name)() const { return false; }

#if FOLLY_AARCH64
#define FOLLY_DETAIL_HWCAP_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_X(aarch64_, name, hwcap_, (bit))
#define FOLLY_DETAIL_HWCAP2_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_X(aarch64_, name, hwcap2_, (bit))
#else
#define FOLLY_DETAIL_HWCAP_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_NOIMPL_X(aarch64_, name)
#define FOLLY_DETAIL_HWCAP2_AARCH64(name, bit) \
  FOLLY_DETAIL_HWCAP_NOIMPL_X(aarch64_, name)
#endif

  FOLLY_DETAIL_HWCAP_AARCH64(fp, 0)
  FOLLY_DETAIL_HWCAP_AARCH64(asimd, 1)
  FOLLY_DETAIL_HWCAP_AARCH64(evtstrm, 2)
  FOLLY_DETAIL_HWCAP_AARCH64(aes, 3)
  FOLLY_DETAIL_HWCAP_AARCH64(pmull, 4)
  FOLLY_DETAIL_HWCAP_AARCH64(sha1, 5)
  FOLLY_DETAIL_HWCAP_AARCH64(sha2, 6)
  FOLLY_DETAIL_HWCAP_AARCH64(crc32, 7)
  FOLLY_DETAIL_HWCAP_AARCH64(atomics, 8)
  FOLLY_DETAIL_HWCAP_AARCH64(fphp, 9)
  FOLLY_DETAIL_HWCAP_AARCH64(asimdhp, 10)
  FOLLY_DETAIL_HWCAP_AARCH64(cpuid, 11)
  FOLLY_DETAIL_HWCAP_AARCH64(asimdrdm, 12)
  FOLLY_DETAIL_HWCAP_AARCH64(jscvt, 13)
  FOLLY_DETAIL_HWCAP_AARCH64(fcma, 14)
  FOLLY_DETAIL_HWCAP_AARCH64(lrcpc, 15)
  FOLLY_DETAIL_HWCAP_AARCH64(dcpop, 16)
  FOLLY_DETAIL_HWCAP_AARCH64(sha3, 17)
  FOLLY_DETAIL_HWCAP_AARCH64(sm3, 18)
  FOLLY_DETAIL_HWCAP_AARCH64(sm4, 19)
  FOLLY_DETAIL_HWCAP_AARCH64(asimddp, 20)
  FOLLY_DETAIL_HWCAP_AARCH64(sha512, 21)
  FOLLY_DETAIL_HWCAP_AARCH64(sve, 22)
  FOLLY_DETAIL_HWCAP_AARCH64(asimdfhm, 23)
  FOLLY_DETAIL_HWCAP_AARCH64(dit, 24)
  FOLLY_DETAIL_HWCAP_AARCH64(uscat, 25)
  FOLLY_DETAIL_HWCAP_AARCH64(ilrcpc, 26)
  FOLLY_DETAIL_HWCAP_AARCH64(flagm, 27)
  FOLLY_DETAIL_HWCAP_AARCH64(ssbs, 28)
  FOLLY_DETAIL_HWCAP_AARCH64(sb, 29)
  FOLLY_DETAIL_HWCAP_AARCH64(paca, 30)
  FOLLY_DETAIL_HWCAP_AARCH64(pacg, 31)

  FOLLY_DETAIL_HWCAP2_AARCH64(dcpodp, 0)
  FOLLY_DETAIL_HWCAP2_AARCH64(sve2, 1)
  FOLLY_DETAIL_HWCAP2_AARCH64(sveaes, 2)
  FOLLY_DETAIL_HWCAP2_AARCH64(svepmull, 3)
  FOLLY_DETAIL_HWCAP2_AARCH64(svebitperm, 4)
  FOLLY_DETAIL_HWCAP2_AARCH64(svesha3, 5)
  FOLLY_DETAIL_HWCAP2_AARCH64(svesm4, 6)
  FOLLY_DETAIL_HWCAP2_AARCH64(flagm2, 7)
  FOLLY_DETAIL_HWCAP2_AARCH64(frint, 8)
  FOLLY_DETAIL_HWCAP2_AARCH64(svei8mm, 9)
  FOLLY_DETAIL_HWCAP2_AARCH64(svef32mm, 10)
  FOLLY_DETAIL_HWCAP2_AARCH64(svef64mm, 11)
  FOLLY_DETAIL_HWCAP2_AARCH64(svebf16, 12)
  FOLLY_DETAIL_HWCAP2_AARCH64(i8mm, 13)
  FOLLY_DETAIL_HWCAP2_AARCH64(bf16, 14)
  FOLLY_DETAIL_HWCAP2_AARCH64(dgh, 15)
  FOLLY_DETAIL_HWCAP2_AARCH64(rng, 16)
  FOLLY_DETAIL_HWCAP2_AARCH64(bti, 17)
  FOLLY_DETAIL_HWCAP2_AARCH64(mte, 18)
  FOLLY_DETAIL_HWCAP2_AARCH64(ecv, 19)
  FOLLY_DETAIL_HWCAP2_AARCH64(afp, 20)
  FOLLY_DETAIL_HWCAP2_AARCH64(rpres, 21)
  FOLLY_DETAIL_HWCAP2_AARCH64(mte3, 22)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme, 23)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i16i64, 24)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f64f64, 25)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i8i32, 26)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f16f32, 27)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_b16f32, 28)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f32f32, 29)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_fa64, 30)
  FOLLY_DETAIL_HWCAP2_AARCH64(wfxt, 31)
  FOLLY_DETAIL_HWCAP2_AARCH64(ebf16, 32)
  FOLLY_DETAIL_HWCAP2_AARCH64(sve_ebf16, 33)
  FOLLY_DETAIL_HWCAP2_AARCH64(cssc, 34)
  FOLLY_DETAIL_HWCAP2_AARCH64(rprfm, 35)
  FOLLY_DETAIL_HWCAP2_AARCH64(sve2p1, 36)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme2, 37)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme2p1, 38)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_i16i32, 39)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_bi32i32, 40)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_b16b16, 41)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f16f16, 42)
  FOLLY_DETAIL_HWCAP2_AARCH64(mops, 43)
  FOLLY_DETAIL_HWCAP2_AARCH64(hbc, 44)
  FOLLY_DETAIL_HWCAP2_AARCH64(sve_b16b16, 45)
  FOLLY_DETAIL_HWCAP2_AARCH64(lrcpc3, 46)
  FOLLY_DETAIL_HWCAP2_AARCH64(lse128, 47)
  FOLLY_DETAIL_HWCAP2_AARCH64(fpmr, 48)
  FOLLY_DETAIL_HWCAP2_AARCH64(lut, 49)
  FOLLY_DETAIL_HWCAP2_AARCH64(faminmax, 50)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8cvt, 51)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8fma, 52)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8dp4, 53)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8dp2, 54)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8e4m3, 55)
  FOLLY_DETAIL_HWCAP2_AARCH64(f8e5m2, 56)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_lutv2, 57)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f8f16, 58)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_f8f32, 59)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8fma, 60)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8dp4, 61)
  FOLLY_DETAIL_HWCAP2_AARCH64(sme_sf8dp2, 62)

#undef FOLLY_DETAIL_HWCAP2_AARCH64
#undef FOLLY_DETAIL_HWCAP_AARCH64
#undef FOLLY_DETAIL_HWCAP_NOIMPL_X
#undef FOLLY_DETAIL_HWCAP_X

 private:
  // GCC would not warn about maybe unused here, but will warn
  // about the ignored attribute.
#if defined(__clang__)
  uint64_t hwcap_ [[maybe_unused]] = 0;
  uint64_t hwcap2_ [[maybe_unused]] = 0;
#else
  uint64_t hwcap_ = 0;
  uint64_t hwcap2_ = 0;
#endif
};

} // namespace folly
