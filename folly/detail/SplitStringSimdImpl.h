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

#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/detail/SimdCharPlatform.h>
#include <folly/detail/SimdForEach.h>
#include <folly/lang/Bits.h>

#if FOLLY_X64
#include <immintrin.h>
#endif

#if FOLLY_AARCH64
#include <arm_neon.h>
#endif

// This file is not supposed to be included by users.
// It should be included in CPP file which exposes apis.
// It is a header file to test different platforms.

// All funcitons are force inline because they are merged into big hiddend
// noinline functions
namespace folly {
namespace detail {

template <bool ignoreEmpty, typename Container>
void splitByCharScalar(char sep, folly::StringPiece what, Container& res) {
  const char* prev = what.data();
  const char* f = prev;
  const char* l = what.data() + what.size();

  auto emplaceBack = [&](const char* sf, const char* sl) mutable {
    if (ignoreEmpty && sf == sl) {
      return;
    }
    res.emplace_back(sf, sl - sf);
  };

  while (f != l) {
    const char* next = f + 1;
    if (*f == sep) {
      if (!ignoreEmpty || (prev != f)) {
        emplaceBack(prev, f);
      }
      prev = next;
    }
    f = next;
  }
  emplaceBack(prev, f);
}

template <typename Platform, bool ignoreEmpty>
struct PlatformSimdSplitByChar {
  using reg_t = typename Platform::reg_t;

  template <typename Container>
  FOLLY_ALWAYS_INLINE void emplaceBack(
      Container& res, const char* f, const char* l) const {
    if (ignoreEmpty && f == l) {
      return;
    }
    res.emplace_back(f, l - f);
  }

  template <typename Uint, typename Container>
  FOLLY_ALWAYS_INLINE void outputStringsFoMmask(
      Uint mmask,
      const char* pos,
      const char*& prev,
      Container& res) const { // reserve was not beneficial on benchmarks.
    while (mmask) {
      auto counted = folly::findFirstSet(mmask) - 1;
      mmask >>= counted;
      mmask >>= Platform::kMmaskBitsPerElement;
      auto firstSet = counted / Platform::kMmaskBitsPerElement;

      const char* split = pos + firstSet;
      pos = split + 1;
      emplaceBack(res, prev, split);
      prev = pos;
    }
  }

  template <typename Container>
  struct ForEachDelegate {
    const PlatformSimdSplitByChar& self;
    char sep;
    const char*& prev;
    Container& res;

    template <typename Ignore, typename UnrollIndex>
    FOLLY_ALWAYS_INLINE bool step(
        const char* ptr, Ignore ignore, UnrollIndex) const {
      reg_t loaded = Platform::loada(ptr, ignore);
      auto mmask = Platform::movemask(Platform::equal(loaded, sep));
      mmask = Platform::clear(mmask, ignore);
      self.outputStringsFoMmask(mmask, ptr, prev, res);
      return false;
    }
  };

  template <typename Container>
  FOLLY_ALWAYS_INLINE void operator()(
      char sep, folly::StringPiece what, Container& res) const {
    const char* prev = what.data();
    ForEachDelegate<Container> delegate{*this, sep, prev, res};
    simd_detail::simdForEachAligning</*unrolling*/ 1>(
        Platform::kCardinal, what.data(), what.data() + what.size(), delegate);
    emplaceBack(res, prev, what.data() + what.size());
  }
};

template <bool ignoreEmpty>
struct PlatformSimdSplitByChar<void, ignoreEmpty> {
  template <typename Container>
  FOLLY_ALWAYS_INLINE void operator()(
      char sep, folly::StringPiece what, Container& res) const {
    return splitByCharScalar<ignoreEmpty>(sep, what, res);
  }
};

} // namespace detail
} // namespace folly
