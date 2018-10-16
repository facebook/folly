// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>

#include "../traits.h"
#include "../forwards.h"

namespace pushmi {

namespace detail {

struct placeholder;

#if (defined(__clang__) || defined(__GNUC__)) &&\
  __has_builtin(__type_pack_element)
#define PUSHMI_TYPE_PACK_ELEMENT(...) \
  __type_pack_element<__VA_ARGS__>
#else
template <std::size_t I, class... Args>
struct type_pack_element {
};
template <std::size_t I, class A, class... Args>
struct type_pack_element<I, A, Args...> : type_pack_element<I - 1, Args...> {
};
template <class A, class... Args>
struct type_pack_element<0, A, Args...> {
  using type = A;
};
#define PUSHMI_TYPE_PACK_ELEMENT(...) \
  typename type_pack_element<__VA_ARGS__>::type
#endif

template <class T, class Args, class = void>
struct substitute {
  using type = T;
};
template <std::size_t I, class... Args>
struct substitute<placeholder[I], typelist<Args...>,
    void_t<PUSHMI_TYPE_PACK_ELEMENT(I-1, Args...)>>
  : std::decay<PUSHMI_TYPE_PACK_ELEMENT(I-1, Args...)> {
};
template <std::size_t I, class... Args>
struct substitute<placeholder(&&)[I], typelist<Args...>,
    void_t<PUSHMI_TYPE_PACK_ELEMENT(I-1, Args...)>> {
  using type = PUSHMI_TYPE_PACK_ELEMENT(I-1, Args...);
};
template <template <class...> class R, class... Ts, class Args>
struct substitute<R<Ts...>, Args,
    void_t<R<typename substitute<Ts, Args>::type...>>> {
  using type = R<typename substitute<Ts, Args>::type...>;
};

template <class Fn, class Requirements>
struct constrained_fn : Fn {
  constrained_fn() = default;
  constrained_fn(Fn fn) : Fn(std::move(fn)) {}

  PUSHMI_TEMPLATE (class... Ts)
    (requires Invocable<Fn&, Ts...> &&
      (bool)typename substitute<Requirements, typelist<Ts...>>::type{})
  decltype(auto) operator()(Ts&&... ts)
      noexcept(noexcept(std::declval<Fn&>()((Ts&&) ts...))) {
    return static_cast<Fn&>(*this)((Ts&&) ts...);
  }
  PUSHMI_TEMPLATE (class... Ts)
    (requires Invocable<const Fn&, Ts...> &&
      (bool)typename substitute<Requirements, typelist<Ts...>>::type{})
  decltype(auto) operator()(Ts&&... ts) const
      noexcept(noexcept(std::declval<const Fn&>()((Ts&&) ts...))) {
    return static_cast<const Fn&>(*this)((Ts&&) ts...);
  }
};

struct constrain_fn {
  template <class Requirements, class Fn>
  constexpr auto operator()(Requirements, Fn fn) const {
    return constrained_fn<Fn, Requirements>{std::move(fn)};
  }
};

} // namespace detail

using _1 = detail::placeholder[1];
using _2 = detail::placeholder[2];
using _3 = detail::placeholder[3];

PUSHMI_INLINE_VAR constexpr const detail::constrain_fn constrain {};

} // namespace pushmi
