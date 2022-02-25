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

#include <new>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>

namespace folly {

#if __cpp_aligned_new >= 201606 || _CPPLIB_VER
using std::align_val_t;
#else
enum class align_val_t : std::size_t {};
#endif

//  operator_new
struct operator_new_fn {
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s) const {
    return ::operator new(s);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::nothrow_t const&) const noexcept {
    return ::operator new(s, std::nothrow);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a) const {
#if __cpp_aligned_new >= 201606 || _CPPLIB_VER
    return ::operator new(s, a);
#else
    return ::operator new(s);
#endif
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a,
      std::nothrow_t const&) const noexcept {
#if __cpp_aligned_new >= 201606 || _CPPLIB_VER
    return ::operator new(s, a, std::nothrow);
#else
    return ::operator new(s, std::nothrow);
#endif
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_new_fn operator_new{};

//  operator_delete
struct operator_delete_fn {
  FOLLY_ERASE void operator()( //
      void* const p) const noexcept {
    return ::operator delete(p);
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED std::size_t const s) const noexcept {
#if __cpp_sized_deallocation >= 201309L || _CPPLIB_VER
    return ::operator delete(p, s);
#else
    return ::operator delete(p);
#endif
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED align_val_t const a) const noexcept {
#if __cpp_aligned_new >= 201606 || _CPPLIB_VER
    return ::operator delete(p, a);
#else
    return ::operator delete(p);
#endif
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a) const noexcept {
#if __cpp_aligned_new >= 201606 || _CPPLIB_VER
#if __cpp_sized_deallocation >= 201309L || _CPPLIB_VER
    return ::operator delete(p, s, a);
#else
    return ::operator delete(p, a);
#endif
#else
#if __cpp_sized_deallocation >= 201309L || _CPPLIB_VER
    return ::operator delete(p, s);
#else
    return ::operator delete(p);
#endif
#endif
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_delete_fn operator_delete{};

} // namespace folly
