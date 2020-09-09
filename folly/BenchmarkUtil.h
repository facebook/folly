/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <type_traits>

#include <folly/Traits.h>

namespace folly {

/**
 * Call doNotOptimizeAway(var) to ensure that var will be computed even
 * post-optimization.  Use it for variables that are computed during
 * benchmarking but otherwise are useless. The compiler tends to do a
 * good job at eliminating unused variables, and this function fools it
 * into thinking var is in fact needed.
 *
 * Call makeUnpredictable(var) when you don't want the optimizer to use
 * its knowledge of var to shape the following code.  This is useful
 * when constant propagation or power reduction is possible during your
 * benchmark but not in real use cases.
 */

#if defined(_MSC_VER) && !defined(__clang__)

#pragma optimize("", off)

inline void doNotOptimizeDependencySink(const void*) {}

#pragma optimize("", on)

template <class T>
void doNotOptimizeAway(const T& datum) {
  doNotOptimizeDependencySink(&datum);
}

template <typename T>
void makeUnpredictable(T& datum) {
  doNotOptimizeDependencySink(&datum);
}

#else

namespace detail {

template <typename T>
struct DoNotOptimizeAwayNeedsIndirect {
  using Decayed = typename std::decay<T>::type;

  // First two constraints ensure it can be an "r" operand.
  // std::is_pointer check is because callers seem to expect that
  // doNotOptimizeAway(&x) is equivalent to doNotOptimizeAway(x).
  constexpr static bool value = !folly::is_trivially_copyable<Decayed>::value ||
      sizeof(Decayed) > sizeof(long) || std::is_pointer<Decayed>::value;
};

} // namespace detail

template <typename T>
auto doNotOptimizeAway(const T& datum) -> typename std::enable_if<
    !detail::DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
  // The "r" constraint forces the compiler to make datum available
  // in a register to the asm block, which means that it must have
  // computed/loaded it.  We use this path for things that are <=
  // sizeof(long) (they have to fit), trivial (otherwise the compiler
  // doesn't want to put them in a register), and not a pointer (because
  // doNotOptimizeAway(&foo) would otherwise be a foot gun that didn't
  // necessarily compute foo).
  //
  // An earlier version of this method had a more permissive input operand
  // constraint, but that caused unnecessary variation between clang and
  // gcc benchmarks.
  asm volatile("" ::"r"(datum));
}

template <typename T>
auto doNotOptimizeAway(const T& datum) -> typename std::enable_if<
    detail::DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
  // This version of doNotOptimizeAway tells the compiler that the asm
  // block will read datum from memory, and that in addition it might read
  // or write from any memory location.  If the memory clobber could be
  // separated into input and output that would be preferrable.
  asm volatile("" ::"m"(datum) : "memory");
}

template <typename T>
auto makeUnpredictable(T& datum) -> typename std::enable_if<
    !detail::DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
  asm volatile("" : "+r"(datum));
}

template <typename T>
auto makeUnpredictable(T& datum) -> typename std::enable_if<
    detail::DoNotOptimizeAwayNeedsIndirect<T>::value>::type {
  asm volatile("" ::"m"(datum) : "memory");
}

#endif

} // namespace folly
