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

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

//  register_pass_max_size
//
//  The platform-specific maximum size of a value which may be passed by-value
//  in registers.
//
//  According to each platform ABI, trivially-copyable types up to this maximum
//  size may, if the stars align, be passed by-value in registers rather than
//  implicitly by-reference to stack copies.
//
//  Approximate. Accuracy is not promised.
constexpr std::size_t register_pass_max_size =
    (kMscVer ? 1u : 2u) * sizeof(void*);

//  is_register_pass_v
//
//  Whether a value may be passed in a register.
//
//  Trivially-copyable values up to register_pass_max_size in width may be
//  passed by-value in registers rather than implicitly by-reference to stack
//  copies.
//
//  Approximate. Accuracy is not promised.
template <typename T>
constexpr bool is_register_pass_v =
    (sizeof(T) <= register_pass_max_size) && std::is_trivially_copyable_v<T>;
template <typename T>
constexpr bool is_register_pass_v<T&> = true;
template <typename T>
constexpr bool is_register_pass_v<T&&> = true;

/// register_pass_t
///
/// Chooses an optimal argument type for passing values of type T based on
/// whehter such values may be passed in registers.
template <typename T>
using register_pass_t = conditional_t<is_register_pass_v<T>, T const, T const&>;

//  has_extended_alignment
//
//  True if it may be presumed that the platform has static extended alignment;
//  false if it may not be so presumed, even when the platform might actually
//  have it. Static extended alignment refers to extended alignment of objects
//  with automatic, static, or thread storage. Whether the there is support for
//  dynamic extended alignment is a property of the allocator which is used for
//  each given dynamic allocation.
//
//  Currently, very heuristical - only non-mobile 64-bit linux gets the extended
//  alignment treatment. Theoretically, this could be tuned better.
constexpr bool has_extended_alignment =
    kIsLinux && sizeof(void*) >= sizeof(std::uint64_t);

namespace detail {

// Implemented this way because of a bug in Clang for ARMv7, which gives the
// wrong result for `alignof` a `union` with a field of each scalar type.
template <typename... Ts>
struct max_align_t_ {
  static constexpr std::size_t value() {
    std::size_t const values[] = {0u, alignof(Ts)...};
    std::size_t r = 0u;
    for (auto const v : values) {
      r = r < v ? v : r;
    }
    return r;
  }
};
using max_align_v_ = max_align_t_<
    long double,
    double,
    float,
    long long int,
    long int,
    int,
    short int,
    bool,
    char,
    char16_t,
    char32_t,
    wchar_t,
    void*,
    std::max_align_t>;

} // namespace detail

// max_align_v is the alignment of max_align_t.
//
// max_align_t is a type which is aligned at least as strictly as the
// most-aligned basic type (see the specification of std::max_align_t). This
// implementation exists because 32-bit iOS platforms have a broken
// std::max_align_t (see below).
//
// You should refer to this as `::folly::max_align_t` in portable code, even if
// you have `using namespace folly;` because C11 defines a global namespace
// `max_align_t` type.
//
// To be certain, we consider every non-void fundamental type specified by the
// standard. On most platforms `long double` would be enough, but iOS 32-bit
// has an 8-byte aligned `double` and `long long int` and a 4-byte aligned
// `long double`.
//
// So far we've covered locals and other non-allocated storage, but we also need
// confidence that allocated storage from `malloc`, `new`, etc will also be
// suitable for objects with this alignment requirement.
//
// Apple document that their implementation of malloc will issue 16-byte
// granularity chunks for small allocations (large allocations are page-size
// granularity and page-aligned). We think that allocated storage will be
// suitable for these objects based on the following assumptions:
//
// 1. 16-byte granularity also means 16-byte aligned.
// 2. `new` and other allocators follow the `malloc` rules.
//
// We also have some anecdotal evidence: we don't see lots of misaligned-storage
// crashes on 32-bit iOS apps that use `double`.
//
// Apple's allocation reference: http://bit.ly/malloc-small
constexpr std::size_t max_align_v = detail::max_align_v_::value();
struct alignas(max_align_v) max_align_t {};

//  Memory locations within the same cache line are subject to destructive
//  interference, also known as false sharing, which is when concurrent
//  accesses to these different memory locations from different cores, where at
//  least one of the concurrent accesses is or involves a store operation,
//  induce contention and harm performance.
//
//  Microbenchmarks indicate that pairs of cache lines also see destructive
//  interference under heavy use of atomic operations, as observed for atomic
//  increment on Sandy Bridge.
//
//  We assume a cache line size of 64, so we use a cache line pair size of 128
//  to avoid destructive interference.
//
//  mimic: std::hardware_destructive_interference_size, C++17
constexpr std::size_t hardware_destructive_interference_size =
    (kIsArchArm || kIsArchS390X) ? 64 : 128;
static_assert(hardware_destructive_interference_size >= max_align_v, "math?");

//  Memory locations within the same cache line are subject to constructive
//  interference, also known as true sharing, which is when accesses to some
//  memory locations induce all memory locations within the same cache line to
//  be cached, benefiting subsequent accesses to different memory locations
//  within the same cache line and heping performance.
//
//  mimic: std::hardware_constructive_interference_size, C++17
constexpr std::size_t hardware_constructive_interference_size = 64;
static_assert(hardware_constructive_interference_size >= max_align_v, "math?");

//  A value corresponding to hardware_constructive_interference_size but which
//  may be used with alignas, since hardware_constructive_interference_size may
//  be too large on some platforms to be used with alignas.
constexpr std::size_t cacheline_align_v = has_extended_alignment
    ? hardware_constructive_interference_size
    : max_align_v;
struct alignas(cacheline_align_v) cacheline_align_t {};

/// valid_align_value
///
/// Returns whether an alignment value is valid. Valid alignment values are
/// powers of two representable as std::uintptr_t, with possibly additional
/// context-specific restrictions that are not checked here.
struct valid_align_value_fn {
  static_assert(sizeof(std::size_t) <= sizeof(std::uintptr_t));
  constexpr bool operator()(std::size_t align) const noexcept {
    return align && !(align & (align - 1));
  }
  constexpr bool operator()(std::align_val_t align) const noexcept {
    return operator()(static_cast<std::size_t>(align));
  }
};
inline constexpr valid_align_value_fn valid_align_value;

/// align_floor
/// align_floor_fn
///
/// Returns pointer rounded down to the given alignment.
struct align_floor_fn {
  constexpr std::uintptr_t operator()(
      std::uintptr_t x, std::size_t alignment) const {
    assert(valid_align_value(alignment));
    return x & ~(alignment - 1);
  }

  template <typename T>
  T* operator()(T* x, std::size_t alignment) const {
    auto asUint = reinterpret_cast<std::uintptr_t>(x);
    asUint = (*this)(asUint, alignment);
    return reinterpret_cast<T*>(asUint);
  }
};
inline constexpr align_floor_fn align_floor;

/// align_ceil
/// align_ceil_fn
///
/// Returns pointer rounded up to the given alignment.
struct align_ceil_fn {
  constexpr std::uintptr_t operator()(
      std::uintptr_t x, std::size_t alignment) const {
    assert(valid_align_value(alignment));
    auto alignmentAsInt = static_cast<std::intptr_t>(alignment);
    return (x + alignmentAsInt - 1) & (-alignmentAsInt);
  }

  template <typename T>
  T* operator()(T* x, std::size_t alignment) const {
    auto asUint = reinterpret_cast<std::uintptr_t>(x);
    asUint = (*this)(asUint, alignment);
    return reinterpret_cast<T*>(asUint);
  }
};
inline constexpr align_ceil_fn align_ceil;

} // namespace folly
