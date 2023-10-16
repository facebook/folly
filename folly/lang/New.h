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

#if defined(__clang__) && FOLLY_HAS_BUILTIN(__builtin_operator_new) >= 201802
#define FOLLY_DETAIL_LANG_NEW_IMPL_N __builtin_operator_new
#else
#define FOLLY_DETAIL_LANG_NEW_IMPL_N ::operator new
#endif

#if defined(__clang__) && FOLLY_HAS_BUILTIN(__builtin_operator_delete) >= 201802
#define FOLLY_DETAIL_LANG_NEW_IMPL_D __builtin_operator_delete
#else
#define FOLLY_DETAIL_LANG_NEW_IMPL_D ::operator delete
#endif

#if defined(__cpp_aligned_new) && __cpp_aligned_new >= 201606
#define FOLLY_DETAIL_LANG_NEW_HAVE_AN 1
#else
#define FOLLY_DETAIL_LANG_NEW_HAVE_AN 0
#endif

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309L
#define FOLLY_DETAIL_LANG_NEW_HAVE_SD 1
#elif defined(_CPPLIB_VER) && !__clang__
#define FOLLY_DETAIL_LANG_NEW_HAVE_SD 1
#else
#define FOLLY_DETAIL_LANG_NEW_HAVE_SD 0
#endif

#if FOLLY_DETAIL_LANG_NEW_HAVE_AN
using std::align_val_t;
#else
enum class align_val_t : std::size_t {};
#endif

//  operator_new
struct operator_new_fn {
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s) const //
      noexcept(noexcept(::operator new(0))) {
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::nothrow_t const&) const noexcept {
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s, std::nothrow);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a) const
      noexcept(noexcept(::operator new(0))) {
#if FOLLY_DETAIL_LANG_NEW_HAVE_AN
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s, a);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s);
#endif
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a,
      std::nothrow_t const&) const noexcept {
#if FOLLY_DETAIL_LANG_NEW_HAVE_AN
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s, a, std::nothrow);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_N(s, std::nothrow);
#endif
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_new_fn operator_new{};

//  operator_delete
struct operator_delete_fn {
  FOLLY_ERASE void operator()( //
      void* const p) const noexcept {
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p);
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED std::size_t const s) const noexcept {
#if FOLLY_DETAIL_LANG_NEW_HAVE_SD
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p, s);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p);
#endif
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED align_val_t const a) const noexcept {
#if FOLLY_DETAIL_LANG_NEW_HAVE_AN
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p, a);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p);
#endif
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      FOLLY_MAYBE_UNUSED std::size_t const s,
      FOLLY_MAYBE_UNUSED align_val_t const a) const noexcept {
#if FOLLY_DETAIL_LANG_NEW_HAVE_AN
#if FOLLY_DETAIL_LANG_NEW_HAVE_SD
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p, s, a);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p, a);
#endif
#else
#if FOLLY_DETAIL_LANG_NEW_HAVE_SD
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p, s);
#else
    return FOLLY_DETAIL_LANG_NEW_IMPL_D(p);
#endif
#endif
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_delete_fn operator_delete{};

} // namespace folly
