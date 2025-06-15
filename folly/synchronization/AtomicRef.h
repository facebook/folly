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
#include <cstddef>
#include <type_traits>

#include <folly/Traits.h>
#include <folly/lang/SafeAssert.h>

namespace folly {

namespace detail {

template <typename T>
struct atomic_ref_base {
  using value_type = remove_cvref_t<T>;

 private:
  using atomic_reference = copy_cvref_t<T, std::atomic<value_type>>&;

 public:
  static_assert(
      sizeof(value_type) == sizeof(std::atomic<value_type>), "size mismatch");
  static_assert(
      std::is_trivially_copyable_v<value_type>, "value not trivially-copyable");

  static inline constexpr std::size_t required_alignment =
      alignof(std::atomic<value_type>);

  explicit atomic_ref_base(T& ref) : ref_(ref) { check_alignment_(); }
  atomic_ref_base(atomic_ref_base const&) = default;

  void store(
      value_type desired,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().store(desired, order);
  }

  value_type load(
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().load(order);
  }

  value_type exchange(
      value_type desired,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().exchange(desired, order);
  }

  bool compare_exchange_weak(
      value_type& expected,
      value_type desired,
      std::memory_order success,
      std::memory_order failure) const noexcept {
    return atomic().compare_exchange_weak(expected, desired, success, failure);
  }

  bool compare_exchange_weak(
      value_type& expected,
      value_type desired,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().compare_exchange_weak(expected, desired, order);
  }

  bool compare_exchange_strong(
      value_type& expected,
      value_type desired,
      std::memory_order success,
      std::memory_order failure) const noexcept {
    return atomic().compare_exchange_strong(
        expected, desired, success, failure);
  }

  bool compare_exchange_strong(
      value_type& expected,
      value_type desired,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().compare_exchange_strong(expected, desired, order);
  }

  atomic_reference atomic() const noexcept {
    return reinterpret_cast<atomic_reference>(ref_); // ub dragons be here
  }

 private:
  void check_alignment_() const noexcept {
    auto ptr = reinterpret_cast<uintptr_t>(
        &reinterpret_cast<unsigned char const&>(ref_));
    FOLLY_SAFE_DCHECK(ptr % required_alignment == 0);
  }

  T& ref_;
};

template <typename T>
struct atomic_ref_integral_base : atomic_ref_base<T> {
  using typename atomic_ref_base<T>::value_type;

  using atomic_ref_base<T>::atomic_ref_base;
  using atomic_ref_base<T>::atomic;

  value_type fetch_add(
      value_type arg,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().fetch_add(arg, order);
  }

  value_type fetch_sub(
      value_type arg,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().fetch_sub(arg, order);
  }

  value_type fetch_and(
      value_type arg,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().fetch_and(arg, order);
  }

  value_type fetch_or(
      value_type arg,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().fetch_or(arg, order);
  }

  value_type fetch_xor(
      value_type arg,
      std::memory_order order = std::memory_order_seq_cst) const noexcept {
    return atomic().fetch_xor(arg, order);
  }
};

template <typename T, typename TD = remove_cvref_t<T>>
using atomic_ref_select = conditional_t<
    std::is_integral<TD>::value && !std::is_same<TD, bool>::value,
    atomic_ref_integral_base<T>,
    atomic_ref_base<T>>;

} // namespace detail

//  atomic_ref
//
//  A very partial backport of std::atomic_ref from C++20, limited for now to
//  the common operations on counters for now. May become a complete backport
//  in the future.
//
//  Relies on the assumption that `T&` is reinterpretable as `std::atomic<T>&`.
//  And that the required alignment for that reinterpretation is `alignof(T)`.
//  When that is not the case, *kaboom*.
//
//  mimic: std::atomic_ref, C++20
template <typename T>
class atomic_ref : public detail::atomic_ref_select<T> {
 private:
  using base = detail::atomic_ref_select<T>;

 public:
  using base::base;
};

template <typename T>
atomic_ref(T&) -> atomic_ref<T>;

struct make_atomic_ref_t {
  template <
      typename T,
      typename...,
      typename TD = remove_cvref_t<T>,
      typename ATD = std::atomic<TD>,
      std::enable_if_t<
          std::is_trivially_copyable_v<TD> && //
              sizeof(TD) == sizeof(ATD) && alignof(TD) == alignof(ATD),
          int> = 0>
  atomic_ref<T> operator()(T& ref) const {
    return atomic_ref<T>{ref};
  }
};

inline constexpr make_atomic_ref_t make_atomic_ref;

} // namespace folly
