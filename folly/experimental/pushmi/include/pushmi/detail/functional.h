// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>

#include "concept_def.h"

namespace pushmi {

PUSHMI_INLINE_VAR constexpr struct invoke_fn {
private:
  template <class F>
  using mem_fn_t = decltype(std::mem_fn(std::declval<F>()));
public:
  template <class F, class... As>
  auto operator()(F&& f, As&&...as) const
      noexcept(noexcept(((F&&) f)((As&&) as...))) ->
      decltype(((F&&) f)((As&&) as...)) {
    return ((F&&) f)((As&&) as...);
  }
  template <class F, class... As>
  auto operator()(F&& f, As&&...as) const
      noexcept(noexcept(std::declval<mem_fn_t<F>>()((As&&) as...))) ->
      decltype(std::mem_fn(f)((As&&) as...)) {
    return std::mem_fn(f)((As&&) as...);
  }
} invoke {};

template <class F, class...As>
using invoke_result_t =
  decltype(pushmi::invoke(std::declval<F>(), std::declval<As>()...));

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept Invocable)(F, Args...),
    requires(F&& f) (
      pushmi::invoke((F &&) f, std::declval<Args>()...)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept NothrowInvocable)(F, Args...),
    requires(F&& f) (
      requires_<noexcept(pushmi::invoke((F &&) f, std::declval<Args>()...))>
    ) &&
    Invocable<F, Args...>
);

} // namespace pushmi
