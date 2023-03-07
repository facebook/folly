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

#include <cstdint>

namespace folly {
namespace simd_detail {

// Based on
// https://github.com/jfalcou/eve/blob/5264e20c51aeca17675e67abf236ce1ead781c52/include/eve/module/algo/algo/for_each_iteration.hpp#L148
// eve has much more settings (like unrolling) but we don't need them for now.

struct ignore_extrema {
  int first = 0;
  int last = 0;
};

struct ignore_none {};

// Everything is ALWAYS_INLINE because we want to have one top level noinline
// function that does everything. Otherwise sometimes the compiler tends
// to mess that up.

template <typename T>
FOLLY_ALWAYS_INLINE T* alignPtr(T* ptr, int to) {
  std::uintptr_t uptr = reinterpret_cast<std::uintptr_t>(ptr);
  std::uintptr_t uto = static_cast<std::uintptr_t>(to);
  uptr &= ~(uto - 1);
  return reinterpret_cast<T*>(uptr);
}

// The main idea is that you can read from the memory within the page.
// This is how strlen works
// https://stackoverflow.com/questions/25566302/vectorized-strlen-getting-away-with-reading-unallocated-memory
//
// There have to be asan disablement to make that work.

// callback accepts a pointer + ignore.
// pointer is guaranteed to be alinged and safe to read <cardinal> bytes from.
// If ignore_extrema is passed in, you have to disable the asan.
//
// Unlike Stl accepting Callback as && because you often want to store state
// in it.
template <typename T, typename Callback>
FOLLY_ALWAYS_INLINE void simdForEachAligning(
    int cardinal, T* f, T* l, Callback&& callback) {
  if (f == l) {
    return;
  }

  T* af = alignPtr(f, cardinal);
  T* al = alignPtr(l, cardinal);

  ignore_extrema ignore{static_cast<int>(f - af), 0};
  if (af != al) {
    // first chunk
    if (callback(af, ignore)) {
      return;
    }
    ignore.first = 0;
    af += cardinal;

    while (af != al) {
      if (callback(af, ignore_none{})) {
        return;
      }
      af += cardinal;
    }

    // Here af might be exactly at the end of page.
    if (af == l) {
      return;
    }
  }

  ignore.last = static_cast<int>(af + cardinal - l);
  callback(af, ignore);
}

} // namespace simd_detail
} // namespace folly
