/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <functional>

#include <folly/experimental/pushmi/detail/concept_def.h>

namespace folly {
namespace pushmi {

PUSHMI_INLINE_VAR constexpr struct invoke_fn {
 private:
  template <class F>
  using mem_fn_t = decltype(std::mem_fn(std::declval<F>()));

 public:
  PUSHMI_TEMPLATE(class F, class... As)
  (requires //
    requires(
    std::declval<F>()(std::declval<As>()...))) //
  auto operator()(F&& f, As&&... as) const
      noexcept(noexcept(((F &&) f)((As &&) as...))) {
    return ((F &&) f)((As &&) as...);
  }
  PUSHMI_TEMPLATE(class F, class... As)
  (requires //
    requires(
    std::mem_fn(std::declval<F>())(std::declval<As>()...))) //
  auto operator()(F&& f, As&&... as) const
      noexcept(noexcept(std::declval<mem_fn_t<F>>()((As &&) as...))) {
    return std::mem_fn(f)((As &&) as...);
  }
} invoke{};

template <class F, class... As>
using invoke_result_t =
    decltype(folly::pushmi::invoke(std::declval<F>(), std::declval<As>()...));

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept Invocable)(F, Args...),
    requires(F&& f) (
      ::folly::pushmi::invoke((F &&) f, std::declval<Args>()...)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept NothrowInvocable)(F, Args...),
    requires(F&& f) (
      requires_<noexcept(::folly::pushmi::invoke((F &&) f, std::declval<Args>()...))>
    ) &&
    Invocable<F, Args...>
);

} // namespace pushmi
} // namespace folly
