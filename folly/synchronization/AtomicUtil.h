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

#include <atomic>
#include <cstdint>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

//  atomic_compare_exchange_weak_explicit
//
//  Fix TSAN bug in std::atomic_compare_exchange_weak_explicit.
//  Workaround for https://github.com/google/sanitizers/issues/970.
//
//  mimic: std::atomic_compare_exchange_weak
template <template <typename> class Atom = std::atomic, typename T>
bool atomic_compare_exchange_weak_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail);

//  atomic_compare_exchange_strong_explicit
//
//  Fix TSAN bug in std::atomic_compare_exchange_strong_explicit.
//  Workaround for https://github.com/google/sanitizers/issues/970.
//
//  mimic: std::atomic_compare_exchange_strong
template <template <typename> class Atom = std::atomic, typename T>
bool atomic_compare_exchange_strong_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail);

//  atomic_fetch_set
//
//  Sets the bit at the given index in the binary representation of the integer
//  to 1. Returns the previous value of the bit, which is equivalent to whether
//  that bit is unchanged.
//
//  Equivalent to Atomic::fetch_or with a mask. For example, if the bit
//  argument to this function is 1, the mask passed to the corresponding
//  Atomic::fetch_or would be 0b10.
//
//  Uses an optimized implementation when available, otherwise falling back to
//  Atomic::fetch_or with mask. The optimization is currently available for
//  std::atomic on x86, using the bts instruction.
struct atomic_fetch_set_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_set_fn atomic_fetch_set{};

//  atomic_fetch_reset
//
//  Resets the bit at the given index in the binary representation of the
//  integer to 0. Returns the previous value of the bit, which is equivalent to
//  whether that bit is changed.
//
//  Equivalent to Atomic::fetch_and with a mask. For example, if the bit
//  argument to this function is 1, the mask passed to the corresponding
//  Atomic::fetch_and would be ~0b10.
//
//  Uses an optimized implementation when available, otherwise falling back to
//  Atomic::fetch_and with mask. The optimization is currently available for
//  std::atomic on x86, using the btr instruction.
struct atomic_fetch_reset_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_reset_fn atomic_fetch_reset{};

//  atomic_fetch_flip
//
//  Flips the bit at the given index in the binary representation of the integer
//  from 1 to 0 or from 0 to 1. Returns the previous value of the bit.
//
//  Equivalent to Atomic::fetch_xor with a mask. For example, if the bit
//  argument to this function is 1, the mask passed to the corresponding
//  Atomic::fetch_xor would be 0b1.
//
//  Uses an optimized implementation when available, otherwise falling back to
//  Atomic::fetch_xor with mask. The optimization is currently available for
//  std::atomic on x86, using the btc instruction.
struct atomic_fetch_flip_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic,
      std::size_t bit,
      std::memory_order order = std::memory_order_seq_cst) const;
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_flip_fn atomic_fetch_flip{};

} // namespace folly

#include <folly/synchronization/AtomicUtil-inl.h>
