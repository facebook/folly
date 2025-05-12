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

#include <glog/logging.h>

#include <string_view>

#include <folly/CpuId.h>
#include <folly/Portability.h>
#include <folly/lang/Assume.h>
#include <folly/portability/Builtins.h>

#if FOLLY_X64 || defined(__i386__)
#include <immintrin.h>
#endif

namespace folly {
namespace compression {
namespace instructions {

// NOTE: It's recommended to compile EF coding with -msse4.2, starting
// with Nehalem, Intel CPUs support POPCNT instruction and gcc will emit
// it for __builtin_popcountll intrinsic.
// But we provide an alternative way for the client code: it can switch to
// the appropriate version of EliasFanoReader<> at runtime (client should
// implement this switching logic itself) by specifying instruction set to
// use explicitly.

struct Default {
  static std::string_view name() noexcept { return "Default"; }
  static bool supported(const folly::CpuId& /* cpuId */ = {}) { return true; }
  static FOLLY_ALWAYS_INLINE uint64_t popcount(uint64_t value) {
    return uint64_t(__builtin_popcountll(value));
  }
  static FOLLY_ALWAYS_INLINE int ctz(uint64_t value) {
    DCHECK_GT(value, 0u);
    return __builtin_ctzll(value);
  }
  static FOLLY_ALWAYS_INLINE int clz(uint64_t value) {
    DCHECK_GT(value, 0u);
    return __builtin_clzll(value);
  }
  static FOLLY_ALWAYS_INLINE uint64_t blsr(uint64_t value) {
    return value & (value - 1);
  }

  // Extract `length` bits starting from `start` from value. Only bits [0:63]
  // will be extracted. All higher order bits in the
  // result will be zeroed. If no bits are extracted, return 0.
  static FOLLY_ALWAYS_INLINE uint64_t
  bextr(uint64_t value, uint32_t start, uint32_t length) {
    if (start > 63) {
      return 0ULL;
    }
    if (start + length > 64) {
      length = 64 - start;
    }

    return (value >> start) &
        ((length == 64) ? (~0ULL) : ((1ULL << length) - 1ULL));
  }

  // Clear high bits starting at position index.
  static FOLLY_ALWAYS_INLINE uint64_t bzhi(uint64_t value, uint32_t index) {
    if (index > 63) {
      return 0;
    }
    return value & ((uint64_t(1) << index) - 1);
  }
};

#if FOLLY_X64 || defined(__i386__)
struct Nehalem : public Default {
  static std::string_view name() noexcept { return "Nehalem"; }

  static bool supported(const folly::CpuId& cpuId = {}) {
    return cpuId.popcnt();
  }

  static FOLLY_ALWAYS_INLINE uint64_t popcount(uint64_t value) {
    // POPCNT is supported starting with Intel Nehalem, AMD K10.
    return uint64_t(_mm_popcnt_u64(value));
  }
};

struct Haswell : public Nehalem {
  static std::string_view name() noexcept { return "Haswell"; }

  static bool supported(const folly::CpuId& cpuId = {}) {
    return Nehalem::supported(cpuId) && cpuId.bmi1() && cpuId.bmi2();
  }

  static FOLLY_ALWAYS_INLINE uint64_t blsr(uint64_t value) {
    // BMI1 is supported starting with Intel Haswell, AMD Piledriver.
    // BLSR combines two instructions into one and reduces register pressure.
    return _blsr_u64(value);
  }

  static FOLLY_ALWAYS_INLINE uint64_t
  bextr(uint64_t value, uint32_t start, uint32_t length) {
    return _bextr_u64(value, start, length);
  }

  static FOLLY_ALWAYS_INLINE uint64_t bzhi(uint64_t value, uint32_t index) {
    return _bzhi_u64(value, index);
  }
};
#endif

enum class Type {
  DEFAULT,
  NEHALEM,
  HASWELL,
};

inline Type detect() {
  const static Type type = [] {
#if FOLLY_X64 || defined(__i386)
    if (instructions::Haswell::supported()) {
      VLOG(2) << "Will use folly::compression::instructions::Haswell";
      return Type::HASWELL;
    } else if (instructions::Nehalem::supported()) {
      VLOG(2) << "Will use folly::compression::instructions::Nehalem";
      return Type::NEHALEM;
    } else {
      VLOG(2) << "Will use folly::compression::instructions::Default";
      return Type::DEFAULT;
    }
#else
    return Type::DEFAULT;
#endif
  }();
  return type;
}

template <class F>
auto dispatch(Type type, F&& f) -> decltype(f(std::declval<Default>())) {
#if FOLLY_X64 || defined(__i386)
  switch (type) {
    case Type::HASWELL:
      return f(Haswell());
    case Type::NEHALEM:
      return f(Nehalem());
    case Type::DEFAULT:
      return f(Default());
  }
#else
  (void)type;
  return f(Default());
#endif

  assume_unreachable();
}

template <class F>
auto dispatch(F&& f) -> decltype(f(std::declval<Default>())) {
  return dispatch(detect(), std::forward<F>(f));
}

} // namespace instructions
} // namespace compression
} // namespace folly
