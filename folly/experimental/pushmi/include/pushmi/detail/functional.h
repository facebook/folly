// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>

#include <meta/meta.hpp>

#include "../traits.h"
#include "../forwards.h"

namespace pushmi {

namespace detail {

struct placeholder;

template <class T, class Args, class = void>
struct substitute {
  using type = T;
};
template <std::size_t I, class Args>
struct substitute<placeholder[I], Args>
  : meta::lazy::let<
      meta::defer<std::decay_t, meta::lazy::at<Args, meta::size_t<I-1>>>> {
};
template <std::size_t I, class Args>
struct substitute<placeholder(&&)[I], Args>
  : meta::lazy::at<Args, meta::size_t<I-1>> {
};
template <template <class...> class R, class... Ts, class Args>
struct substitute<R<Ts...>, Args, meta::void_<R<meta::_t<substitute<Ts, Args>>...>>> {
  using type = R<meta::_t<substitute<Ts, Args>>...>;
};

template <class Fn, class Requirements>
struct constrained_fn : Fn {
  constrained_fn() = default;
  constrained_fn(Fn fn) : Fn(std::move(fn)) {}

  PUSHMI_TEMPLATE (class... Ts)
    (requires Invocable<Fn&, Ts...> &&
      (bool)meta::_t<substitute<Requirements, meta::list<Ts...>>>{})
  decltype(auto) operator()(Ts&&... ts) noexcept(noexcept(std::declval<Fn&>()((Ts&&) ts...))) {
    return static_cast<Fn&>(*this)((Ts&&) ts...);
  }
  PUSHMI_TEMPLATE (class... Ts)
    (requires Invocable<const Fn&, Ts...> &&
      (bool)meta::_t<substitute<Requirements, meta::list<Ts...>>>{})
  decltype(auto) operator()(Ts&&... ts) const noexcept(noexcept(std::declval<const Fn&>()((Ts&&) ts...))) {
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
