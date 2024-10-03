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

#include <folly/CPortability.h>
#include <folly/Traits.h>
#include <folly/algorithm/simd/Ignore.h>
#include <folly/algorithm/simd/detail/UnrollUtils.h>
#include <folly/lang/Align.h>

#include <array>
#include <cstdint>
#include <type_traits>

namespace folly {
namespace simd::detail {

// Based on
// https://github.com/jfalcou/eve/blob/5264e20c51aeca17675e67abf236ce1ead781c52/include/eve/module/algo/algo/for_each_iteration.hpp#L148
//
// Everything is ALWAYS_INLINE because we want to have one top level noinline
// function that does everything. Otherwise sometimes the compiler tends
// to mess that up.
//

/**
 * simdForEachAligning<unrolling>(cardinal, f, l, delegate);
 *
 * The main idea is that you can read from the memory within the page.
 * The beginning and end of the page are aligned to 4KB.
 * That means, the previous aligned address is always safe to read from
 * (requires asan disablement). This is how strlen works
 * https://stackoverflow.com/questions/25566302/vectorized-strlen-getting-away-with-reading-unallocated-memory
 *
 * The interface parameters are:
 * - unrolling: by how much do you want to unroll the main loop. 4 is a good
 * default for simple operations.
 * - cardinal: how big is your register in elements.
 * - f, l: [first, last) range
 * - delegate:
 *     conceptually a callback but has 2 different operations.
 *     - bool step(T*, ignore): to process one register.
 *       Is called for tails and gets ignore_none/ignore_extrema.
 *     - bool unrolledStep(std::array<T*, unrolling>) -
 *       to process the unrolled part. unrolledStep is not called
 *       if unrolling == 1.
 *   Both step and unrolledStep should return true if they would like to break.
 *   Delegate is passed by reference so that the caller can store state in it.
 */
template <int unrolling, typename T, typename Delegate>
FOLLY_ALWAYS_INLINE void simdForEachAligning(
    int cardinal, T* f, T* l, Delegate& delegate);

/**
 * previousAlignedAddress
 *
 * Given a pointer returns a closest pointer aligned to a given size
 * (in elements).
 */
template <typename T>
FOLLY_ALWAYS_INLINE T* previousAlignedAddress(T* ptr, int to) {
  return align_floor(ptr, sizeof(T) * to);
}

/**
 * SimdForEachMainLoop
 *
 * Implementaiton detail of simdForEach
 *
 * Regardless of how you chose to handle tails, the middle will be the same.
 * The operator() returns true if the delegate returned to break.
 *
 * There are two variations:
 * - no unrolling (unroll<1>)
 * - unrolling > 1
 *
 * For explanation of parameters see simdForEachAligning
 */
struct SimdForEachMainLoop {
  template <typename T, typename Delegate>
  FOLLY_ALWAYS_INLINE bool operator()(
      int cardinal, T*& f, T* l, Delegate& delegate, index_constant<1>) const {
    while (f != l) {
      if (delegate.step(f, ignore_none{}, index_constant<0>{}))
        return true;
      f += cardinal;
    }

    return false;
  }

  template <typename T, typename Delegate>
  struct SmallStepsLambda {
    bool& shouldBreak;
    int cardinal;
    T*& f;
    T* l;
    Delegate& delegate;

    template <std::size_t i>
    FOLLY_ALWAYS_INLINE bool operator()(index_constant<i> unrollI) {
      if (f == l)
        return true;

      shouldBreak = delegate.step(f, ignore_none{}, unrollI);
      f += cardinal;
      return shouldBreak;
    }
  };

  template <typename T, typename Delegate, std::size_t unrolling>
  FOLLY_ALWAYS_INLINE bool operator()(
      int cardinal, T*& f, T* l, Delegate& delegate, index_constant<unrolling>)
      const {
    // Not enough to fully unroll explanation.
    //
    // There are a few approaches to handle this:
    // 1. Duff's device. Is not good for simd algorithms, we are not that
    // pressed for space and we'd like the code to work differently when
    // we have many registers to process.
    // 2. Traditional: do unrolled steps first, then single steps.
    // 3. What we do here: put single steps before the unrolled ones to also do
    //    them in the beginning.
    //
    // The reason to prefer 3 over 2 is that unrolled part often would need more
    // set up, and this allows us to avoid it for small arrays.

    // Jump to this while true will be performed at most once, to do final
    // single steps.
    while (true) {
      // Delegate said we should break. We might also stop because we reached
      // the end.
      bool shouldBreak = false;

      // single steps
      if (UnrollUtils::unrollUntil<unrolling>(SmallStepsLambda<T, Delegate>{
              shouldBreak, cardinal, f, l, delegate})) {
        return shouldBreak;
      }

      for (std::ptrdiff_t bigStepsCount = (l - f) / (cardinal * unrolling);
           bigStepsCount != 0;
           --bigStepsCount) {
        std::array<T*, unrolling> arr;
        // Since there is no callback, we can rely on the inlining
        UnrollUtils::unrollUntil<unrolling>([&](auto idx) {
          arr[idx()] = f;
          f += cardinal;
          return false;
        });
        if (delegate.unrolledStep(arr)) {
          return true;
        }
      }
    }
  }
};

// Comment is at the top of the file.
template <int unrolling, typename T, typename Delegate>
FOLLY_ALWAYS_INLINE void simdForEachAligning(
    int cardinal, T* f, T* l, Delegate& delegate) {
  if (f == l) {
    return;
  }

  T* af = previousAlignedAddress(f, cardinal);
  T* al = previousAlignedAddress(l, cardinal);

  ignore_extrema ignore{static_cast<int>(f - af), 0};
  if (af != al) {
    // first chunk
    if (delegate.step(af, ignore, index_constant<0>{})) {
      return;
    }
    ignore.first = 0;
    af += cardinal;

    if (SimdForEachMainLoop{}(
            cardinal, af, al, delegate, index_constant<unrolling>{})) {
      return;
    }

    // Here af might be exactly at the end of page.
    if (af == l) {
      return;
    }
  }

  ignore.last = static_cast<int>(af + cardinal - l);
  delegate.step(af, ignore, index_constant<0>{});
}

} // namespace simd::detail
} // namespace folly
