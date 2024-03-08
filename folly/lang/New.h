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
#include <folly/functional/Invoke.h>

namespace folly {

namespace detail {

#if defined(__cpp_aligned_new)
constexpr auto cpp_aligned_new_ = __cpp_aligned_new >= 201606;
#else
constexpr auto cpp_aligned_new_ = false;
#endif

#if defined(__cpp_sized_deallocation)
constexpr auto cpp_sized_deallocation_ = __cpp_sized_deallocation >= 201309L;
#else
constexpr auto cpp_sized_deallocation_ = false;
#endif

//  https://clang.llvm.org/docs/LanguageExtensions.html#builtin-operator-new-and-builtin-operator-delete

constexpr auto op_new_builtin_ =
    FOLLY_HAS_BUILTIN(__builtin_operator_new) >= 201802L;
constexpr auto op_del_builtin_ =
    FOLLY_HAS_BUILTIN(__builtin_operator_del) >= 201802L;

FOLLY_CREATE_QUAL_INVOKER(op_new_builtin_fn_, __builtin_operator_new);
FOLLY_CREATE_QUAL_INVOKER(op_new_library_fn_, ::operator new);

FOLLY_CREATE_QUAL_INVOKER(op_del_builtin_fn_, __builtin_operator_delete);
FOLLY_CREATE_QUAL_INVOKER(op_del_library_fn_, ::operator delete);

template <bool Usual, bool C = (Usual && op_new_builtin_)>
constexpr conditional_t<C, op_new_builtin_fn_, op_new_library_fn_> op_new_;

template <bool Usual, bool C = (Usual && op_del_builtin_)>
constexpr conditional_t<C, op_del_builtin_fn_, op_del_library_fn_> op_del_;

template <bool Usual, typename... A>
FOLLY_ERASE void do_op_del_sized_(
    void* const p, std::size_t const s, A const... a) {
  if constexpr (detail::cpp_sized_deallocation_) {
    return op_del_<Usual>(p, s, a...);
  } else {
    return op_del_<Usual>(p, a...);
  }
}

} // namespace detail

//  operator_new
struct operator_new_fn {
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s) const //
      noexcept(noexcept(::operator new(0))) {
    return detail::op_new_<true>(s);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::nothrow_t const& nt) const noexcept {
    return detail::op_new_<true>(s, nt);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::align_val_t const a) const //
      noexcept(noexcept(::operator new(0))) {
    return detail::op_new_<detail::cpp_aligned_new_>(s, a);
  }
  FOLLY_NODISCARD FOLLY_ERASE void* operator()( //
      std::size_t const s,
      std::align_val_t const a,
      std::nothrow_t const& nt) const noexcept {
    return detail::op_new_<detail::cpp_aligned_new_>(s, a, nt);
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_new_fn operator_new{};

//  operator_delete
struct operator_delete_fn {
  FOLLY_ERASE void operator()( //
      void* const p) const noexcept {
    return detail::op_del_<true>(p);
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      std::size_t const s) const noexcept {
    return detail::do_op_del_sized_<true>(p, s);
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      std::align_val_t const a) const noexcept {
    return detail::op_del_<detail::cpp_aligned_new_>(p, a);
  }
  FOLLY_ERASE void operator()( //
      void* const p,
      std::size_t const s,
      std::align_val_t const a) const noexcept {
    return detail::do_op_del_sized_<detail::cpp_aligned_new_>(p, s, a);
  }
};
FOLLY_INLINE_VARIABLE constexpr operator_delete_fn operator_delete{};

} // namespace folly
