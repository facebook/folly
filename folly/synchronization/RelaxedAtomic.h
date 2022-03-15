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

namespace folly {

//  relaxed_atomic
//
//  Like atomic, but without the std::memory_order parameters on any member
//  functions. All operations use std::memory_order_relaxed, rather than the
//  std::memory_order_seq_cst, which is the default memory order in std::atomic.
//
//  Useful for values which may be loaded and stored concurrently, such as
//  counters, but which do not guard any associated data.
template <typename T>
struct relaxed_atomic;

namespace detail {

template <typename T>
struct relaxed_atomic_base : protected std::atomic<T> {
 private:
  using atomic = std::atomic<T>;

 public:
  using value_type = T;

  using atomic::atomic;

  T operator=(T desired) noexcept {
    store(desired);
    return desired;
  }
  T operator=(T desired) volatile noexcept {
    store(desired);
    return desired;
  }

  bool is_lock_free() const noexcept { return atomic::is_lock_free(); }
  bool is_lock_free() const volatile noexcept { return atomic::is_lock_free(); }

  void store(T desired) noexcept {
    atomic::store(desired, std::memory_order_relaxed);
  }
  void store(T desired) volatile noexcept {
    atomic::store(desired, std::memory_order_relaxed);
  }

  T load() const noexcept { return atomic::load(std::memory_order_relaxed); }
  T load() const volatile noexcept {
    return atomic::load(std::memory_order_relaxed);
  }

  operator T() const noexcept { return load(); }
  operator T() const volatile noexcept { return load(); }

  T exchange(T desired) noexcept {
    return atomic::exchange(desired, std::memory_order_relaxed);
  }
  T exchange(T desired) volatile noexcept {
    return atomic::exchange(desired, std::memory_order_relaxed);
  }

  bool compare_exchange_weak(T& expected, T desired) noexcept {
    return atomic::compare_exchange_weak(
        expected, desired, std::memory_order_relaxed);
  }
  bool compare_exchange_weak(T& expected, T desired) volatile noexcept {
    return atomic::compare_exchange_weak(
        expected, desired, std::memory_order_relaxed);
  }

  bool compare_exchange_strong(T& expected, T desired) noexcept {
    return atomic::compare_exchange_strong(
        expected, desired, std::memory_order_relaxed);
  }
  bool compare_exchange_strong(T& expected, T desired) volatile noexcept {
    return atomic::compare_exchange_strong(
        expected, desired, std::memory_order_relaxed);
  }
};

template <typename T>
struct relaxed_atomic_integral_base : private relaxed_atomic_base<T> {
 private:
  using atomic = std::atomic<T>;
  using base = relaxed_atomic_base<T>;

 public:
  using typename base::value_type;

  using base::relaxed_atomic_base;
  using base::operator=;
  using base::operator T;
  using base::compare_exchange_strong;
  using base::compare_exchange_weak;
  using base::exchange;
  using base::is_lock_free;
  using base::load;
  using base::store;

  T fetch_add(T arg) noexcept {
    return atomic::fetch_add(arg, std::memory_order_relaxed);
  }
  T fetch_add(T arg) volatile noexcept {
    return atomic::fetch_add(arg, std::memory_order_relaxed);
  }

  T fetch_sub(T arg) noexcept {
    return atomic::fetch_sub(arg, std::memory_order_relaxed);
  }
  T fetch_sub(T arg) volatile noexcept {
    return atomic::fetch_sub(arg, std::memory_order_relaxed);
  }

  T fetch_and(T arg) noexcept {
    return atomic::fetch_and(arg, std::memory_order_relaxed);
  }
  T fetch_and(T arg) volatile noexcept {
    return atomic::fetch_and(arg, std::memory_order_relaxed);
  }

  T fetch_or(T arg) noexcept {
    return atomic::fetch_or(arg, std::memory_order_relaxed);
  }
  T fetch_or(T arg) volatile noexcept {
    return atomic::fetch_or(arg, std::memory_order_relaxed);
  }

  T fetch_xor(T arg) noexcept {
    return atomic::fetch_xor(arg, std::memory_order_relaxed);
  }
  T fetch_xor(T arg) volatile noexcept {
    return atomic::fetch_xor(arg, std::memory_order_relaxed);
  }

  T operator++() noexcept { return fetch_add(1) + 1; }
  T operator++() volatile noexcept { return fetch_add(1) + 1; }

  T operator++(int) noexcept { return fetch_add(1); }
  T operator++(int) volatile noexcept { return fetch_add(1); }

  T operator--() noexcept { return fetch_sub(1) - 1; }
  T operator--() volatile noexcept { return fetch_sub(1) - 1; }

  T operator--(int) noexcept { return fetch_sub(1); }
  T operator--(int) volatile noexcept { return fetch_sub(1); }

  T operator+=(T arg) noexcept { return fetch_add(arg) + arg; }
  T operator+=(T arg) volatile noexcept { return fetch_add(arg) + arg; }

  T operator-=(T arg) noexcept { return fetch_sub(arg) - arg; }
  T operator-=(T arg) volatile noexcept { return fetch_sub(arg) - arg; }

  T operator&=(T arg) noexcept { return fetch_and(arg) & arg; }
  T operator&=(T arg) volatile noexcept { return fetch_and(arg) & arg; }

  T operator|=(T arg) noexcept { return fetch_or(arg) | arg; }
  T operator|=(T arg) volatile noexcept { return fetch_or(arg) | arg; }

  T operator^=(T arg) noexcept { return fetch_xor(arg) ^ arg; }
  T operator^=(T arg) volatile noexcept { return fetch_xor(arg) ^ arg; }
};

} // namespace detail

template <>
struct relaxed_atomic<bool> : detail::relaxed_atomic_base<bool> {
 private:
  using base = detail::relaxed_atomic_base<bool>;

 public:
  using typename base::value_type;

  using base::relaxed_atomic_base;
  using base::operator=;
  using base::operator bool;
};

using relaxed_atomic_bool = relaxed_atomic<bool>;

template <typename T>
struct relaxed_atomic : detail::relaxed_atomic_base<T> {
 private:
  using base = detail::relaxed_atomic_base<T>;

 public:
  using typename base::value_type;

  using base::relaxed_atomic_base;
  using base::operator=;
  using base::operator T;
};

template <typename T>
struct relaxed_atomic<T*> : detail::relaxed_atomic_base<T*> {
 private:
  using atomic = std::atomic<T*>;
  using base = detail::relaxed_atomic_base<T*>;

 public:
  using typename base::value_type;

  using detail::relaxed_atomic_base<T*>::relaxed_atomic_base;
  using base::operator=;
  using base::operator T*;

  T* fetch_add(std::ptrdiff_t arg) noexcept {
    return atomic::fetch_add(arg, std::memory_order_relaxed);
  }
  T* fetch_add(std::ptrdiff_t arg) volatile noexcept {
    return atomic::fetch_add(arg, std::memory_order_relaxed);
  }

  T* fetch_sub(std::ptrdiff_t arg) noexcept {
    return atomic::fetch_sub(arg, std::memory_order_relaxed);
  }
  T* fetch_sub(std::ptrdiff_t arg) volatile noexcept {
    return atomic::fetch_sub(arg, std::memory_order_relaxed);
  }

  T* operator++() noexcept { return fetch_add(1) + 1; }
  T* operator++() volatile noexcept { return fetch_add(1) + 1; }

  T* operator++(int) noexcept { return fetch_add(1); }
  T* operator++(int) volatile noexcept { return fetch_add(1); }

  T* operator--() noexcept { return fetch_sub(1) - 1; }
  T* operator--() volatile noexcept { return fetch_sub(1) - 1; }

  T* operator--(int) noexcept { return fetch_sub(1); }
  T* operator--(int) volatile noexcept { return fetch_sub(1); }

  T* operator+=(std::ptrdiff_t arg) noexcept { return fetch_add(arg) + arg; }
  T* operator+=(std::ptrdiff_t arg) volatile noexcept {
    return fetch_add(arg) + arg;
  }

  T* operator-=(std::ptrdiff_t arg) noexcept { return fetch_sub(arg) - arg; }
  T* operator-=(std::ptrdiff_t arg) volatile noexcept {
    return fetch_sub(arg) - arg;
  }
};

#if __cpp_deduction_guides >= 201611
template <typename T>
relaxed_atomic(T) -> relaxed_atomic<T>;
#endif

#define FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(type)            \
  template <>                                                                \
  struct relaxed_atomic<type> : detail::relaxed_atomic_integral_base<type> { \
   private:                                                                  \
    using base = detail::relaxed_atomic_integral_base<type>;                 \
                                                                             \
   public:                                                                   \
    using base::relaxed_atomic_integral_base;                                \
    using base::operator=;                                                   \
    using base::operator type;                                               \
  };

FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(char)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(signed char)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(unsigned char)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(signed short)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(unsigned short)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(signed int)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(unsigned int)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(signed long)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(unsigned long)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(signed long long)
FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION(unsigned long long)

#undef FOLLY_RELAXED_ATOMIC_DEFINE_INTEGRAL_SPECIALIZATION

using relaxed_atomic_char = relaxed_atomic<char>;
using relaxed_atomic_schar = relaxed_atomic<signed char>;
using relaxed_atomic_uchar = relaxed_atomic<unsigned char>;
using relaxed_atomic_short = relaxed_atomic<short>;
using relaxed_atomic_ushort = relaxed_atomic<unsigned short>;
using relaxed_atomic_int = relaxed_atomic<int>;
using relaxed_atomic_uint = relaxed_atomic<unsigned int>;
using relaxed_atomic_long = relaxed_atomic<long>;
using relaxed_atomic_ulong = relaxed_atomic<unsigned long>;
using relaxed_atomic_llong = relaxed_atomic<long long>;
using relaxed_atomic_ullong = relaxed_atomic<unsigned long long>;
using relaxed_atomic_char16_t = relaxed_atomic<char16_t>;
using relaxed_atomic_char32_t = relaxed_atomic<char32_t>;
using relaxed_atomic_wchar_t = relaxed_atomic<wchar_t>;
using relaxed_atomic_int8_t = relaxed_atomic<std::int8_t>;
using relaxed_atomic_uint8_t = relaxed_atomic<std::uint8_t>;
using relaxed_atomic_int16_t = relaxed_atomic<std::int16_t>;
using relaxed_atomic_uint16_t = relaxed_atomic<std::uint16_t>;
using relaxed_atomic_int32_t = relaxed_atomic<std::int32_t>;
using relaxed_atomic_uint32_t = relaxed_atomic<std::uint32_t>;
using relaxed_atomic_int64_t = relaxed_atomic<std::int64_t>;
using relaxed_atomic_uint64_t = relaxed_atomic<std::uint64_t>;
using relaxed_atomic_int_least8_t = relaxed_atomic<std::int_least8_t>;
using relaxed_atomic_uint_least8_t = relaxed_atomic<std::uint_least8_t>;
using relaxed_atomic_int_least16_t = relaxed_atomic<std::int_least16_t>;
using relaxed_atomic_uint_least16_t = relaxed_atomic<std::uint_least16_t>;
using relaxed_atomic_int_least32_t = relaxed_atomic<std::int_least32_t>;
using relaxed_atomic_uint_least32_t = relaxed_atomic<std::uint_least32_t>;
using relaxed_atomic_int_least64_t = relaxed_atomic<std::int_least64_t>;
using relaxed_atomic_uint_least64_t = relaxed_atomic<std::uint_least64_t>;
using relaxed_atomic_int_fast8_t = relaxed_atomic<std::int_fast8_t>;
using relaxed_atomic_uint_fast8_t = relaxed_atomic<std::uint_fast8_t>;
using relaxed_atomic_int_fast16_t = relaxed_atomic<std::int_fast16_t>;
using relaxed_atomic_uint_fast16_t = relaxed_atomic<std::uint_fast16_t>;
using relaxed_atomic_int_fast32_t = relaxed_atomic<std::int_fast32_t>;
using relaxed_atomic_uint_fast32_t = relaxed_atomic<std::uint_fast32_t>;
using relaxed_atomic_int_fast64_t = relaxed_atomic<std::int_fast64_t>;
using relaxed_atomic_uint_fast64_t = relaxed_atomic<std::uint_fast64_t>;
using relaxed_atomic_intptr_t = relaxed_atomic<std::intptr_t>;
using relaxed_atomic_uintptr_t = relaxed_atomic<std::uintptr_t>;
using relaxed_atomic_size_t = relaxed_atomic<std::size_t>;
using relaxed_atomic_ptrdiff_t = relaxed_atomic<std::ptrdiff_t>;
using relaxed_atomic_intmax_t = relaxed_atomic<std::intmax_t>;
using relaxed_atomic_uintmax_t = relaxed_atomic<std::uintmax_t>;

} // namespace folly
