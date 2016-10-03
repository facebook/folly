/*
 * Copyright 2016 Facebook, Inc.
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

#pragma once

#include <glog/logging.h>

#include <folly/CpuId.h>
#include <folly/Portability.h>
#include <folly/portability/Builtins.h>

#if defined(__GNUC__) || defined(__clang__)
// For compilers supporting AT&T assembly syntax.
#define FOLLY_INSTRUCTIONS_SUPPORTED 1
#else
#define FOLLY_INSTRUCTIONS_SUPPORTED 0
#endif

namespace folly { namespace compression { namespace instructions {

// NOTE: It's recommended to compile EF coding with -msse4.2, starting
// with Nehalem, Intel CPUs support POPCNT instruction and gcc will emit
// it for __builtin_popcountll intrinsic.
// But we provide an alternative way for the client code: it can switch to
// the appropriate version of EliasFanoReader<> in realtime (client should
// implement this switching logic itself) by specifying instruction set to
// use explicitly.

struct Default {
  static bool supported(const folly::CpuId& /* cpuId */ = {}) {
    return true;
  }
  static FOLLY_ALWAYS_INLINE uint64_t popcount(uint64_t value) {
    return __builtin_popcountll(value);
  }
  static FOLLY_ALWAYS_INLINE int ctz(uint64_t value) {
    DCHECK_GT(value, 0);
    return __builtin_ctzll(value);
  }
  static FOLLY_ALWAYS_INLINE int clz(uint64_t value) {
    DCHECK_GT(value, 0);
    return __builtin_clzll(value);
  }
  static FOLLY_ALWAYS_INLINE uint64_t blsr(uint64_t value) {
    return value & (value - 1);
  }
};

#if FOLLY_INSTRUCTIONS_SUPPORTED

struct Nehalem : public Default {
  static bool supported(const folly::CpuId& cpuId = {}) {
    return cpuId.popcnt();
  }

  static FOLLY_ALWAYS_INLINE uint64_t popcount(uint64_t value) {
    // POPCNT is supported starting with Intel Nehalem, AMD K10.
    uint64_t result;
    asm ("popcntq %1, %0" : "=r" (result) : "r" (value));
    return result;
  }
};

struct Haswell : public Nehalem {
  static bool supported(const folly::CpuId& cpuId = {}) {
    return Nehalem::supported(cpuId) && cpuId.bmi1();
  }

  static FOLLY_ALWAYS_INLINE uint64_t blsr(uint64_t value) {
    // BMI1 is supported starting with Intel Haswell, AMD Piledriver.
    // BLSR combines two instuctions into one and reduces register pressure.
    uint64_t result;
    asm ("blsrq %1, %0" : "=r" (result) : "r" (value));
    return result;
  }
};

#else // FOLLY_INSTRUCTIONS_SUPPORTED

struct Nehalem : public Default {};
struct Haswell : public Nehalem {};

#endif // FOLLY_INSTRUCTIONS_SUPPORTED

}}} // namespaces
