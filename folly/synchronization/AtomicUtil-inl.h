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
#include <cassert>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include <folly/ConstexprMath.h>

#ifdef _WIN32
#include <intrin.h>
#endif

namespace folly {

template <typename T>
class atomic_ref;

namespace detail {

constexpr std::memory_order atomic_compare_exchange_succ(
    bool cond, std::memory_order succ, std::memory_order fail) {
  constexpr auto const relaxed = std::memory_order_relaxed;
  constexpr auto const release = std::memory_order_release;
  constexpr auto const acq_rel = std::memory_order_acq_rel;

  assert(fail != release);
  assert(fail != acq_rel);

  auto const bump = succ == release ? acq_rel : succ;
  auto const high = fail < bump ? bump : fail;
  return !cond || fail == relaxed ? succ : high;
}

//  atomic_compare_exchange_succ
//
//  Return a success order with, conditionally, the failure order mixed in.
//
//  Until C++17, atomic compare-exchange operations require the success order to
//  be at least as strong as the failure order. C++17 drops this requirement. As
//  an implication, were this rule in force, an implementation is free to ignore
//  the explicit failure order and to infer one from the success order.
//
//    https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
//
//  The libstdc++ library with assertions enabled enforces this rule, including
//  under C++17, but only until and not since libstdc++12.
//
//    https://github.com/gcc-mirror/gcc/commit/dba1ab212292839572fda60df00965e094a11252
//
//  Clang TSAN ignores the passed failure order and infers failure order from
//  success order in atomic compare-exchange operations, which is broken for
//  cases like success-release/failure-acquire.
//
//    https://github.com/google/sanitizers/issues/970
//
//  Handle all of these cases.
constexpr std::memory_order atomic_compare_exchange_succ(
    std::memory_order succ, std::memory_order fail) {
  constexpr auto const cond = false //
      || (FOLLY_CPLUSPLUS < 201702L) //
      || (kGlibcxxVer && kGlibcxxVer < 12 && kGlibcxxAssertions) //
      || (kIsSanitizeThread && kIsClang);
  return atomic_compare_exchange_succ(cond, succ, fail);
}

} // namespace detail

template <template <typename> class Atom, typename T>
bool atomic_compare_exchange_weak_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail) {
  succ = detail::atomic_compare_exchange_succ(succ, fail);
  return obj->compare_exchange_weak(*expected, desired, succ, fail);
}

template <template <typename> class Atom, typename T>
bool atomic_compare_exchange_strong_explicit(
    Atom<T>* obj,
    type_t<T>* expected,
    type_t<T> desired,
    std::memory_order succ,
    std::memory_order fail) {
  succ = detail::atomic_compare_exchange_succ(succ, fail);
  return obj->compare_exchange_strong(*expected, desired, succ, fail);
}

namespace detail {

// TODO: Remove the non-default implementations when both gcc and clang
// can recognize single bit set/reset patterns and compile them down to locked
// bts and btr instructions.
//
// Currently, at the time of writing it seems like gcc7 and greater can make
// this optimization and clang cannot - https://gcc.godbolt.org/z/Q83rxX

struct atomic_fetch_set_fallback_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const {
    using Integer = decltype(atomic.load());
    auto mask = Integer(Integer{0b1} << bit);
    return (atomic.fetch_or(mask, order) & mask);
  }
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_set_fallback_fn
    atomic_fetch_set_fallback{};

struct atomic_fetch_reset_fallback_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const {
    using Integer = decltype(atomic.load());
    auto mask = Integer(Integer{0b1} << bit);
    return (atomic.fetch_and(Integer(~mask), order) & mask);
  }
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_reset_fallback_fn
    atomic_fetch_reset_fallback{};

struct atomic_fetch_flip_fallback_fn {
  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const {
    using Integer = decltype(atomic.load());
    auto mask = Integer(Integer{0b1} << bit);
    return (atomic.fetch_xor(mask, order) & mask);
  }
};
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_flip_fallback_fn
    atomic_fetch_flip_fallback{};

/**
 * A simple trait to determine if the given type is an instantiation of
 * std::atomic
 */
template <typename T>
constexpr auto is_atomic = false;
template <typename Integer>
constexpr auto is_atomic<std::atomic<Integer>> = true;

#if FOLLY_X64

#if defined(_MSC_VER)

template <typename Integer>
inline bool atomic_fetch_set_native(
    std::atomic<Integer>& atomic, std::size_t bit, std::memory_order order) {
  static_assert(alignof(std::atomic<Integer>) == alignof(Integer), "");
  static_assert(sizeof(std::atomic<Integer>) == sizeof(Integer), "");
  assert(atomic.is_lock_free());

  if /* constexpr */ (sizeof(Integer) == 4) {
    return _interlockedbittestandset(
        reinterpret_cast<volatile long*>(&atomic), static_cast<long>(bit));
  } else if /* constexpr */ (sizeof(Integer) == 8) {
    return _interlockedbittestandset64(
        reinterpret_cast<volatile long long*>(&atomic),
        static_cast<long long>(bit));
  } else {
    assert(sizeof(Integer) != 4 && sizeof(Integer) != 8);
    return atomic_fetch_set_fallback(atomic, bit, order);
  }
}

template <typename Atomic>
inline bool atomic_fetch_set_native(
    Atomic& atomic, std::size_t bit, std::memory_order order) {
  static_assert(!std::is_same<Atomic, std::atomic<std::uint32_t>>{}, "");
  static_assert(!std::is_same<Atomic, std::atomic<std::uint64_t>>{}, "");
  return atomic_fetch_set_fallback(atomic, bit, order);
}

template <typename Integer>
inline bool atomic_fetch_reset_native(
    std::atomic<Integer>& atomic, std::size_t bit, std::memory_order order) {
  static_assert(alignof(std::atomic<Integer>) == alignof(Integer), "");
  static_assert(sizeof(std::atomic<Integer>) == sizeof(Integer), "");
  assert(atomic.is_lock_free());

  if /* constexpr */ (sizeof(Integer) == 4) {
    return _interlockedbittestandreset(
        reinterpret_cast<volatile long*>(&atomic), static_cast<long>(bit));
  } else if /* constexpr */ (sizeof(Integer) == 8) {
    return _interlockedbittestandreset64(
        reinterpret_cast<volatile long long*>(&atomic),
        static_cast<long long>(bit));
  } else {
    assert(sizeof(Integer) != 4 && sizeof(Integer) != 8);
    return atomic_fetch_reset_fallback(atomic, bit, order);
  }
}

template <typename Atomic>
inline bool atomic_fetch_reset_native(
    Atomic& atomic, std::size_t bit, std::memory_order mo) {
  static_assert(!std::is_same<Atomic, std::atomic<std::uint32_t>>{}, "");
  static_assert(!std::is_same<Atomic, std::atomic<std::uint64_t>>{}, "");
  return atomic_fetch_reset_fallback(atomic, bit, mo);
}

template <typename Atomic>
inline bool atomic_fetch_flip_native(
    Atomic& atomic, std::size_t bit, std::memory_order mo) {
  return atomic_fetch_flip_fallback(atomic, bit, mo);
}

#else

#define FOLLY_DETAIL_ATOMIC_BIT_OP_DEFINE(instr)                          \
  struct atomic_fetch_bit_op_native_##instr##_fn {                        \
    template <typename Int>                                               \
    FOLLY_ERASE bool operator()(                                          \
        Int* ptr, Int bit, std::memory_order order) const {               \
      bool out = false;                                                   \
      if (order == std::memory_order_relaxed) {                           \
        if (sizeof(Int) == 2) {                                           \
          asm("lock " #instr "w %2, %1"                                   \
              : "=@ccc"(out), "+m"(*ptr)                                  \
              : "ri"(bit));                                               \
        }                                                                 \
        if (sizeof(Int) == 4) {                                           \
          asm("lock " #instr "l %2, %1"                                   \
              : "=@ccc"(out), "+m"(*ptr)                                  \
              : "ri"(bit));                                               \
        }                                                                 \
        if (sizeof(Int) == 8) {                                           \
          asm("lock " #instr "q %2, %1"                                   \
              : "=@ccc"(out), "+m"(*ptr)                                  \
              : "ri"(bit));                                               \
        }                                                                 \
      } else {                                                            \
        if (sizeof(Int) == 2) {                                           \
          asm volatile("lock " #instr "w %1, (%2)"                        \
                       : "=@ccc"(out)                                     \
                       : "ri"(bit), "r"(ptr)                              \
                       : "memory");                                       \
        }                                                                 \
        if (sizeof(Int) == 4) {                                           \
          asm volatile("lock " #instr "l %1, (%2)"                        \
                       : "=@ccc"(out)                                     \
                       : "ri"(bit), "r"(ptr)                              \
                       : "memory");                                       \
        }                                                                 \
        if (sizeof(Int) == 8) {                                           \
          asm volatile("lock " #instr "q %1, (%2)"                        \
                       : "=@ccc"(out)                                     \
                       : "ri"(bit), "r"(ptr)                              \
                       : "memory");                                       \
        }                                                                 \
      }                                                                   \
      return out;                                                         \
    }                                                                     \
  };                                                                      \
  FOLLY_INLINE_VARIABLE constexpr atomic_fetch_bit_op_native_##instr##_fn \
      atomic_fetch_bit_op_native_##instr

FOLLY_DETAIL_ATOMIC_BIT_OP_DEFINE(bts);
FOLLY_DETAIL_ATOMIC_BIT_OP_DEFINE(btr);
FOLLY_DETAIL_ATOMIC_BIT_OP_DEFINE(btc);

#undef FOLLY_DETAIL_ATOMIC_BIT_OP_DEFINE

template <typename Integer, typename Op, typename Fb>
FOLLY_ERASE bool atomic_fetch_bit_op_native_(
    std::atomic<Integer>& atomic,
    std::size_t bit,
    std::memory_order order,
    Op op,
    Fb fb) {
  constexpr auto atomic_size = sizeof(Integer);
  constexpr auto lo_size = std::size_t(2);
  constexpr auto hi_size = std::size_t(8);
  // some versions of TSAN do not properly instrument the inline assembly
  if (atomic_size > hi_size || folly::kIsSanitize) {
    return fb(atomic, bit, order);
  }
  auto address = reinterpret_cast<std::uintptr_t>(&atomic);
  // there is a minimum word size - if too small, enlarge
  constexpr auto word_size = constexpr_clamp(atomic_size, lo_size, hi_size);
  using word_type = uint_bits_t<word_size * 8>;
  auto adjust = std::size_t(address % lo_size);
  address -= adjust;
  bit += 8 * adjust;
  return op(reinterpret_cast<word_type*>(address), word_type(bit), order);
}

#if __cpp_lib_atomic_ref >= 201806L
template <typename Integer, typename Op, typename Fb>
FOLLY_ERASE bool atomic_fetch_bit_op_native_(
    std::atomic_ref<Integer>& atomic,
    std::size_t bit,
    std::memory_order order,
    Op op,
    Fb fb) {
  if constexpr (!std::atomic_ref<Integer>::is_always_lock_free) {
    return fb(atomic, bit, order);
  }
  auto& ref = **reinterpret_cast<std::atomic<Integer>**>(&atomic);
  return atomic_fetch_bit_op_native_(ref, bit, order, op, fb);
}
#endif

template <typename Integer>
inline bool atomic_fetch_set_native(
    std::atomic<Integer>& atomic, std::size_t bit, std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_bts;
  auto fb = atomic_fetch_set_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}

template <typename Integer>
inline bool atomic_fetch_set_native(
    atomic_ref<Integer>& atomic, std::size_t bit, std::memory_order order) {
  return atomic_fetch_set_native(atomic.atomic(), bit, order);
}

#if __cpp_lib_atomic_ref >= 201806L
template <typename Integer>
inline bool atomic_fetch_set_native(
    std::atomic_ref<Integer>& atomic,
    std::size_t bit,
    std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_bts;
  auto fb = atomic_fetch_set_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}
#endif

template <typename Atomic>
inline bool atomic_fetch_set_native(
    Atomic& atomic, std::size_t bit, std::memory_order order) {
  static_assert(!is_atomic<Atomic>, "");
  return atomic_fetch_set_fallback(atomic, bit, order);
}

template <typename Integer>
inline bool atomic_fetch_reset_native(
    std::atomic<Integer>& atomic, std::size_t bit, std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_btr;
  auto fb = atomic_fetch_reset_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}

template <typename Integer>
inline bool atomic_fetch_reset_native(
    atomic_ref<Integer>& atomic, std::size_t bit, std::memory_order order) {
  return atomic_fetch_reset_native(atomic.atomic(), bit, order);
}

#if __cpp_lib_atomic_ref >= 201806L
template <typename Integer>
inline bool atomic_fetch_reset_native(
    std::atomic_ref<Integer>& atomic,
    std::size_t bit,
    std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_btr;
  auto fb = atomic_fetch_reset_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}
#endif

template <typename Atomic>
bool atomic_fetch_reset_native(
    Atomic& atomic, std::size_t bit, std::memory_order order) {
  static_assert(!is_atomic<Atomic>, "");
  return atomic_fetch_reset_fallback(atomic, bit, order);
}

template <typename Integer>
inline bool atomic_fetch_flip_native(
    std::atomic<Integer>& atomic, std::size_t bit, std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_btc;
  auto fb = atomic_fetch_flip_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}

template <typename Integer>
inline bool atomic_fetch_flip_native(
    atomic_ref<Integer>& atomic, std::size_t bit, std::memory_order order) {
  return atomic_fetch_flip_native(atomic.atomic(), bit, order);
}

#if __cpp_lib_atomic_ref >= 201806L
template <typename Integer>
inline bool atomic_fetch_flip_native(
    std::atomic_ref<Integer>& atomic,
    std::size_t bit,
    std::memory_order order) {
  auto op = atomic_fetch_bit_op_native_btc;
  auto fb = atomic_fetch_flip_fallback;
  return atomic_fetch_bit_op_native_(atomic, bit, order, op, fb);
}
#endif

template <typename Atomic>
bool atomic_fetch_flip_native(
    Atomic& atomic, std::size_t bit, std::memory_order order) {
  static_assert(!is_atomic<Atomic>, "");
  return atomic_fetch_flip_fallback(atomic, bit, order);
}

#endif

#else

using atomic_fetch_set_native_fn = detail::atomic_fetch_set_fallback_fn;
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_set_native_fn
    atomic_fetch_set_native{};

using atomic_fetch_reset_native_fn = detail::atomic_fetch_reset_fallback_fn;
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_reset_native_fn
    atomic_fetch_reset_native{};

using atomic_fetch_flip_native_fn = detail::atomic_fetch_flip_fallback_fn;
FOLLY_INLINE_VARIABLE constexpr atomic_fetch_flip_native_fn
    atomic_fetch_flip_native{};

#endif

template <typename Atomic>
void atomic_fetch_bit_op_check_(Atomic& atomic, std::size_t bit) {
  using Integer = decltype(atomic.load());
  static_assert(std::is_unsigned<Integer>{}, "");
  static_assert(!std::is_const<Atomic>{}, "");
  assert(bit < (sizeof(Integer) * 8));
  (void)bit;
}

} // namespace detail

template <typename Atomic>
bool atomic_fetch_set_fn::operator()(
    Atomic& atomic, std::size_t bit, std::memory_order mo) const {
  detail::atomic_fetch_bit_op_check_(atomic, bit);
  return detail::atomic_fetch_set_native(atomic, bit, mo);
}

template <typename Atomic>
bool atomic_fetch_reset_fn::operator()(
    Atomic& atomic, std::size_t bit, std::memory_order mo) const {
  detail::atomic_fetch_bit_op_check_(atomic, bit);
  return detail::atomic_fetch_reset_native(atomic, bit, mo);
}

template <typename Atomic>
bool atomic_fetch_flip_fn::operator()(
    Atomic& atomic, std::size_t bit, std::memory_order mo) const {
  detail::atomic_fetch_bit_op_check_(atomic, bit);
  return detail::atomic_fetch_flip_native(atomic, bit, mo);
}

} // namespace folly
