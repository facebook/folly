/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/Bits.h"

#include "folly/CpuId.h"

// None of this is necessary if we're compiling for a target that supports
// popcnt
#ifndef __POPCNT__

namespace {

int popcount_inst(unsigned int x) {
  int n;
  asm ("popcntl %1, %0" : "=r" (n) : "r" (x));
  return n;
}

int popcount_builtin(unsigned int x) {
  return __builtin_popcount(x);
}

int popcountll_inst(unsigned long long x) {
  unsigned long long n;
  asm ("popcntq %1, %0" : "=r" (n) : "r" (x));
  return n;
}

int popcountll_builtin(unsigned long long x) {
  return __builtin_popcountll(x);
}

typedef decltype(popcount_builtin) Type_popcount;
typedef decltype(popcountll_builtin) Type_popcountll;

}  // namespace

// This function is called on startup to resolve folly::detail::popcount
extern "C" Type_popcount* folly_popcount_ifunc() {
  return folly::CpuId().popcnt() ?  popcount_inst : popcount_builtin;
}

// This function is called on startup to resolve folly::detail::popcountll
extern "C" Type_popcountll* folly_popcountll_ifunc() {
  return folly::CpuId().popcnt() ?  popcountll_inst : popcountll_builtin;
}

namespace folly {
namespace detail {

// Call folly_popcount_ifunc on startup to resolve to either popcount_inst
// or popcount_builtin
int popcount(unsigned int x)
  __attribute__((ifunc("folly_popcount_ifunc")));

// Call folly_popcount_ifunc on startup to resolve to either popcountll_inst
// or popcountll_builtin
int popcountll(unsigned long long x)
  __attribute__((ifunc("folly_popcountll_ifunc")));

}  // namespace detail
}  // namespace folly

#endif  /* !__POPCNT__ */

