/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <utility>

#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/detail/traits.h>
#include <folly/functional/Invoke.h>

namespace folly {
namespace pushmi {

/* using override */ using folly::invoke;

/* using override */ using folly::invoke_result;
/* using override */ using folly::invoke_result_t;
/* using override */ using folly::is_invocable;
/* using override */ using folly::is_invocable_r;
/* using override */ using folly::is_nothrow_invocable;
/* using override */ using folly::is_nothrow_invocable_r;

PUSHMI_CONCEPT_DEF(
    template(class F, class... Args) //
    (concept Invocable)(F, Args...), //
    requires(F&& f)( //
        pushmi::invoke((F &&) f, std::declval<Args>()...) //
        ) //
);

PUSHMI_CONCEPT_DEF(
    template(class F, class Ret, class... Args) //
    (concept _InvocableR)(F, Ret, Args...), //
    Invocable<F, Args...>&& ConvertibleTo<invoke_result_t<F, Args...>, Ret> //
);

PUSHMI_CONCEPT_DEF(
    template(class F, class... Args) //
    (concept NothrowInvocable)(F, Args...), //
    requires(F&& f)( //
        requires_<
            noexcept(pushmi::invoke((F &&) f, std::declval<Args>()...))> //
        ) &&
        Invocable<F, Args...> //
);

//
// construct_deduced
//

// For emulating CTAD on compilers that don't support it. Must be specialized.
template <template <class...> class T>
struct construct_deduced;

template <template <class...> class T, class... AN>
using deduced_type_t = invoke_result_t<construct_deduced<T>, AN...>;

//
// overload
//

// inspired by Ovrld - shown in a presentation by Nicolai Josuttis
#if __cpp_variadic_using >= 201611 && __cpp_concepts
template <SemiMovable... Fns>
    requires sizeof...(Fns) > 0 //
    struct overload_fn : Fns... {
  constexpr overload_fn() = default;
  constexpr explicit overload_fn(Fns... fns) //
      requires sizeof...(Fns) == 1 : Fns(std::move(fns))... {}
  constexpr overload_fn(Fns... fns) requires sizeof...(Fns) > 1
      : Fns(std::move(fns))... {}
  using Fns::operator()...;
};
#else
template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... Fns>
#if __cpp_concepts
    requires sizeof...(Fns) > 0
#endif
    struct overload_fn;
template <class Fn>
struct overload_fn<Fn> : Fn {
  constexpr overload_fn() = default;
  constexpr explicit overload_fn(Fn fn) : Fn(std::move(fn)) {}
  constexpr overload_fn(overload_fn&&) = default;
  constexpr overload_fn& operator=(overload_fn&&) = default;
  constexpr overload_fn(const overload_fn&) = default;
  constexpr overload_fn& operator=(const overload_fn&) = default;
  using Fn::operator();
};
#if !defined(__GNUC__) || __GNUC__ >= 8
template <class Fn, class... Fns>
struct overload_fn<Fn, Fns...> : Fn, overload_fn<Fns...> {
  constexpr overload_fn() = default;
  constexpr overload_fn(Fn fn, Fns... fns)
      : Fn(std::move(fn)), overload_fn<Fns...>{std::move(fns)...} {}
  constexpr overload_fn(overload_fn&&) = default;
  constexpr overload_fn& operator=(overload_fn&&) = default;
  constexpr overload_fn(const overload_fn&) = default;
  constexpr overload_fn& operator=(const overload_fn&) = default;
  using Fn::operator();
  using overload_fn<Fns...>::operator();
};
#else
template <class Fn, class... Fns>
struct overload_fn<Fn, Fns...> {
 private:
  std::pair<Fn, overload_fn<Fns...>> fns_;
  template <bool B>
  using _which_t = std::conditional_t<B, Fn, overload_fn<Fns...>>;

 public:
  constexpr overload_fn() = default;
  constexpr overload_fn(Fn fn, Fns... fns)
      : fns_{std::move(fn), overload_fn<Fns...>{std::move(fns)...}} {}
  constexpr overload_fn(overload_fn&&) = default;
  constexpr overload_fn& operator=(overload_fn&&) = default;
  constexpr overload_fn(const overload_fn&) = default;
  constexpr overload_fn& operator=(const overload_fn&) = default;
  PUSHMI_TEMPLATE(class... Args) //
  (requires lazy::Invocable<Fn&, Args...> ||
   lazy::Invocable<overload_fn<Fns...>&, Args...>) //
      decltype(auto)
      operator()(Args&&... args) noexcept(
          noexcept(std::declval<_which_t<Invocable<Fn&, Args...>>&>()(
              std::declval<Args>()...))) {
    return std::get<!Invocable<Fn&, Args...>>(fns_)((Args &&) args...);
  }
  PUSHMI_TEMPLATE(class... Args) //
  (requires lazy::Invocable<const Fn&, Args...> ||
   lazy::Invocable<const overload_fn<Fns...>&, Args...>) //
      decltype(auto)
      operator()(Args&&... args) const noexcept(noexcept(
          std::declval<const _which_t<Invocable<const Fn&, Args...>>&>()(
              std::declval<Args>()...))) {
    return std::get<!Invocable<const Fn&, Args...>>(fns_)((Args &&) args...);
  }
};
#endif
#endif

template <class... Fns>
auto overload(Fns... fns) -> overload_fn<Fns...> {
  return overload_fn<Fns...>{std::move(fns)...};
}

} // namespace pushmi
} // namespace folly
