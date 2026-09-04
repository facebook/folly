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
#include <utility>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

namespace detail {
struct atomic_value_type_alias_ {
  template <typename Atomic>
  using apply = typename Atomic::value_type;
};
struct atomic_value_type_load_ {
  template <typename Atomic>
  using apply = decltype(std::declval<Atomic const&>().load());
};
} // namespace detail

//  atomic_value_type_t
//  atomic_value_type
//
//  A trait type alias and type giving the effective value-type of a type which
//  are atomic-like. Either member type alias value_type or the return type of
//  member function load.
template <typename Atomic>
using atomic_value_type_t = typename conditional_t<
    is_detected_v<detail::atomic_value_type_alias_::apply, Atomic>,
    detail::atomic_value_type_alias_,
    detail::atomic_value_type_load_>::template apply<Atomic>;
template <typename Atomic>
struct atomic_value_type {
  using type = atomic_value_type_t<Atomic>;
};

namespace detail {
template <typename Atomic>
using detect_atomic_load_mo =
    decltype(std::declval<Atomic&>().load(std::memory_order_relaxed));
} // namespace detail

/// atomic_accepts_memory_order_v
///
/// A trait giving whether the operations of a type which is atomic-like accept
/// a memory order. Types with a fixed memory order, such as relaxed_atomic,
/// omit the parameter from their operations and so give false.
///
/// Detected via member load, under the convention that a type which is atomic-
/// like accepts a memory order either on all of its operations or on none.
///
/// The operations in this header which accept a memory order do not
/// participate in overload resolution for types which give false, since for
/// such types there would be no memory order to apply.
template <typename Atomic>
inline constexpr bool atomic_accepts_memory_order_v =
    is_detected_v<detail::detect_atomic_load_mo, Atomic>;

/// memory_order_load
///
/// The load part of a possibly-composite memory-order.
constexpr std::memory_order memory_order_load( //
    std::memory_order order) noexcept;

/// memory_order_store
///
/// The store part of a possibly-composite memory-order.
constexpr std::memory_order memory_order_store(
    std::memory_order order) noexcept;

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
//
//  Is a read-modify-write: stores unconditionally, even when the bit is already
//  set. Compare atomic_fetch_set_cond, which elides the store in that case but
//  which is correspondingly not always a read-modify-write.
struct atomic_fetch_set_fn {
  template <typename Atomic>
  bool operator()(Atomic& atomic, std::size_t bit) const;

  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_set_fn atomic_fetch_set{};

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
//
//  Is a read-modify-write: stores unconditionally, even when the bit is already
//  reset. Compare atomic_fetch_reset_cond, which elides the store in that case
//  but which is correspondingly not always a read-modify-write.
struct atomic_fetch_reset_fn {
  template <typename Atomic>
  bool operator()(Atomic& atomic, std::size_t bit) const;

  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_reset_fn atomic_fetch_reset{};

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
  bool operator()(Atomic& atomic, std::size_t bit) const;

  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_flip_fn atomic_fetch_flip{};

//  There is deliberately no atomic_fetch_flip_cond. A flip always changes the
//  bit, so there is no converged state against which to elide the store.

/// atomic_fetch_set_cond
///
/// As atomic_fetch_set, but elides the store when the bit is already set.
///
/// Is not necessarily a read-modify-write. When the bit is already set, the
/// operation is a plain load and no store takes place, so it may not be relied
/// on to carry a release edge or to act as a seq_cst synchronization point.
///
/// The memory order is applied as for atomic_fetch_max_cond: the trial load
/// takes only the load part, per memory_order_load, and the store, when it
/// happens, takes the memory order in full.
///
/// The elision is best-effort: the bit may be set concurrently between the load
/// and the store, in which case the store takes place anyway.
struct atomic_fetch_set_cond_fn {
  template <typename Atomic>
  bool operator()(Atomic& atomic, std::size_t bit) const;

  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_set_cond_fn atomic_fetch_set_cond{};

/// atomic_fetch_reset_cond
///
/// As atomic_fetch_reset, but elides the store when the bit is already reset.
///
/// Is not necessarily a read-modify-write. When the bit is already reset, the
/// operation is a plain load and no store takes place, so it may not be relied
/// on to carry a release edge or to act as a seq_cst synchronization point.
///
/// The memory order is applied as for atomic_fetch_min_cond: the trial load
/// takes only the load part, per memory_order_load, and the store, when it
/// happens, takes the memory order in full.
///
/// The elision is best-effort: the bit may be reset concurrently between the
/// load and the store, in which case the store takes place anyway.
struct atomic_fetch_reset_cond_fn {
  template <typename Atomic>
  bool operator()(Atomic& atomic, std::size_t bit) const;

  template <typename Atomic>
  bool operator()(
      Atomic& atomic, std::size_t bit, std::memory_order order) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_reset_cond_fn atomic_fetch_reset_cond{};

//  atomic_fetch_modify
//
//  Atomically modifies the value in the atomic by loading the value, passing it
//  to the operation op, and storing the result, but without risk of race from
//  interleaving accesses.
//
//  Example:
//
//    int atomic_fetch_nonnegative(std::atomic<int>& atomic) {
//      auto const op = [](int value) { return std::max(value, 0); };
//      return folly::atomic_fetch_modify(x, op);
//    }
//
//  The implementation is just a c/x-loop. It is intended for use when other
//  specialized and optimized forms of atomic-fetch-modify are inapplicable.
//
//  A single call to this function will call op at least one time, but with no
//  upper bound on the number of times. It is expected that op be free of side-
//  effect.
//
//  Does not attempt to handle ABA scenarios.
//
//  Works with any atomic-like type exposing load and compare_exchange_weak,
//  including std::atomic, folly::atomic_ref, and folly::relaxed_atomic. Types
//  which give false for atomic_accepts_memory_order_v, such as relaxed_atomic,
//  have only the overload taking no memory order.
struct atomic_fetch_modify_fn {
  template <typename Atomic, typename Op>
  atomic_value_type_t<Atomic> operator()(Atomic& atomic, Op op) const;

  template <typename Atomic, typename Op>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic, Op op, std::memory_order mo) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_modify_fn atomic_fetch_modify{};

//  atomic_fetch_min
//
//  Atomically replaces the value in the atomic with the lesser of the current
//  value and the argument. Returns the previous value.
//
//  Uses Atomic::fetch_min when available, in either its memory-order-taking or
//  its memory-order-free form, otherwise falling back to atomic_fetch_modify,
//  whose caveats then apply.
//
//  Is a read-modify-write: stores unconditionally, even when the value is
//  unchanged, so it always participates in the modification order and may carry
//  a release edge. Compare atomic_fetch_min_cond, which elides the store when
//  the value is unchanged but which is correspondingly not always a
//  read-modify-write.
struct atomic_fetch_min_fn {
  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic, atomic_value_type_t<Atomic> value) const;

  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic,
      atomic_value_type_t<Atomic> value,
      std::memory_order mo) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_min_fn atomic_fetch_min{};

//  atomic_fetch_max
//
//  Atomically replaces the value in the atomic with the greater of the current
//  value and the argument. Returns the previous value.
//
//  Uses Atomic::fetch_max when available, in either its memory-order-taking or
//  its memory-order-free form, otherwise falling back to atomic_fetch_modify,
//  whose caveats then apply.
//
//  Is a read-modify-write: stores unconditionally, even when the value is
//  unchanged, so it always participates in the modification order and may carry
//  a release edge. Compare atomic_fetch_max_cond, which elides the store when
//  the value is unchanged but which is correspondingly not always a
//  read-modify-write.
struct atomic_fetch_max_fn {
  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic, atomic_value_type_t<Atomic> value) const;

  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic,
      atomic_value_type_t<Atomic> value,
      std::memory_order mo) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_max_fn atomic_fetch_max{};

/// atomic_fetch_min_cond
///
/// As atomic_fetch_min, but elides the store when the value is unchanged, so
/// that a converged value is not repeatedly written. Returns the previous
/// value.
///
/// Is not necessarily a read-modify-write. When the value is unchanged, the
/// operation is a plain load and no store takes place, so it may not be relied
/// on to carry a release edge or to act as a seq_cst synchronization point.
///
/// The trial load takes only the load part of the memory order, per
/// memory_order_load, since it never stores. The store, when it happens, takes
/// the memory order in full: it is itself a read-modify-write, and its own load
/// component, which yields the returned value, requires the load part.
///
/// The elision is best-effort: the value may be lowered concurrently between
/// the load and the store, in which case the store takes place anyway.
struct atomic_fetch_min_cond_fn {
  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic, atomic_value_type_t<Atomic> value) const;

  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic,
      atomic_value_type_t<Atomic> value,
      std::memory_order mo) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_min_cond_fn atomic_fetch_min_cond{};

/// atomic_fetch_max_cond
///
/// As atomic_fetch_max, but elides the store when the value is unchanged, so
/// that a converged value is not repeatedly written. Returns the previous
/// value.
///
/// Is not necessarily a read-modify-write. When the value is unchanged, the
/// operation is a plain load and no store takes place, so it may not be relied
/// on to carry a release edge or to act as a seq_cst synchronization point.
///
/// The trial load takes only the load part of the memory order, per
/// memory_order_load, since it never stores. The store, when it happens, takes
/// the memory order in full: it is itself a read-modify-write, and its own load
/// component, which yields the returned value, requires the load part.
///
/// The elision is best-effort: the value may be raised concurrently between the
/// load and the store, in which case the store takes place anyway.
struct atomic_fetch_max_cond_fn {
  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic, atomic_value_type_t<Atomic> value) const;

  template <typename Atomic>
  atomic_value_type_t<Atomic> operator()(
      Atomic& atomic,
      atomic_value_type_t<Atomic> value,
      std::memory_order mo) const
    requires(atomic_accepts_memory_order_v<Atomic>);
};
inline constexpr atomic_fetch_max_cond_fn atomic_fetch_max_cond{};

template <template <typename> class Atom>
struct atomic_thread_fence_traits;

template <>
struct atomic_thread_fence_traits<std::atomic> {
  static inline constexpr auto fence = std::atomic_thread_fence;
};

} // namespace folly

#include <folly/synchronization/AtomicUtil-inl.h>
