#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <utility>

#include "concepts.h"

namespace pushmi {

template<class T>
struct construct {
  template<class... AN>
    requires Constructible<T, AN...>
  auto operator()(AN&&... an) const {
    return T{std::forward<AN>(an)...};
  }
};

template<template <class...> class T>
struct construct_deduced {
  template<class... AN>
    requires requires (AN&&... an) { T{(AN&&) an...}; }
  auto operator()(AN&&... an) const {
    return T{std::forward<AN>(an)...};
  }
};

template <template <class...> class T, class... AN>
using deduced_type_t = std::invoke_result_t<construct_deduced<T>, AN...>;

// template <class Fn>
// struct apply {
//   template <class Tup>
//     requires requires (Tup&& tup) { std::apply(Fn{}, (Tup&&) tup); }
//   decltype(auto) operator()(Tup&& tup) const {
//     return std::apply(Fn{}, (Tup&&) tup);
//   }
// };
//
// template <class Fn, class Gn>
// struct compose {
//   template <class... AN>
//     requires Invocable<Gn, AN...> &&
//       Invocable<Fn, std::invoke_result_t<Gn, AN...>>
//   decltype(auto) operator()(AN&&... an) const {
//     return std::invoke(Fn{}, std::invoke(Gn{}, (AN&&) an...));
//   }
// };
//
// template <class T>
// inline constexpr apply<construct<T>> from_tuple {};
//
// template <template <class...> class T>
// inline constexpr apply<construct_deduced<T>> from_tuple_deduced {};

template <class T, class... AN>
auto from_tuple(std::tuple<AN...>&& t) {
  return std::apply(construct<T>{}, std::move(t));
}

template <template<class...> class T, class... AN>
auto from_tuple(std::tuple<AN...>&& t) {
  using Deduced = decltype(T{std::declval<AN&&>()...});
  return std::apply(construct<Deduced>{}, std::move(t));
}

template <class T>
void sfinae_from_tuple(...);

template <template<class... TN> class T>
void sfinae_from_tuple(...);

template <class T, class... AN,
  class Constructor = construct<T>>
auto sfinae_from_tuple(std::tuple<AN...>&& t) ->
  decltype(std::apply(Constructor{}, std::move(t))) {
  return std::apply(Constructor{}, std::move(t));
}

template <template<class...> class T, class... AN,
  class Deduced = decltype(T{std::declval<AN>()...}),
  class Constructor = construct<Deduced>>
auto sfinae_from_tuple(std::tuple<AN...>&& t) ->
  decltype(std::apply(Constructor{}, std::move(t))) {
  return std::apply(Constructor{}, std::move(t));
}

struct ignoreVF {
  template <class V>
  void operator()(V&&) {}
};

struct abortEF {
  template <class E>
  void operator()(E) noexcept {
    std::abort();
  }
};

struct ignoreDF {
  void operator()() {}
};

struct ignoreStpF {
  void operator()() {}
};

struct ignoreStrtF {
  template <class Up>
  void operator()(Up&) {}
};


struct ignoreSF {
  template <class Out>
  void operator()(Out) {}
  template <class TP, class Out>
  void operator()(TP, Out) {}
};

struct systemNowF {
  auto operator()() { return std::chrono::system_clock::now(); }
};

struct passDVF {
  template <class V, Receiver Data>
  requires requires(Data& out, V&& v) {
    ::pushmi::set_value(out, (V&&) v);
  }
  void operator()(Data& out, V&& v) const {
    ::pushmi::set_value(out, (V&&) v);
  }
};

struct passDEF {
  template <class E, NoneReceiver<E> Data>
  void operator()(Data& out, E e) const noexcept {
    ::pushmi::set_error(out, e);
  }
};

struct passDDF {
  template <Receiver Data>
  void operator()(Data& out) const {
    ::pushmi::set_done(out);
  }
};

struct passDStpF {
  template <Receiver Data>
  void operator()(Data& out) const {
    ::pushmi::set_stopping(out);
  }
};

struct passDStrtF {
  template <class Up, Receiver Data>
  requires requires(Data& out, Up& up) {
    ::pushmi::set_starting(out, up);
  }
  void operator()(Data& out, Up& up) const {
    ::pushmi::set_starting(out, up);
  }
};


struct passDSF {
  template <class Data, class Out>
  void operator()(Data& in, Out out) {
    ::pushmi::submit(in, std::move(out));
  }
  template <class Data, class TP, class Out>
  void operator()(Data& in, TP at, Out out) {
    ::pushmi::submit(in, std::move(at), std::move(out));
  }
};

struct passDNF {
  template <TimeSender Data>
  auto operator()(Data& in) const noexcept {
    return ::pushmi::now(in);
  }
};

// inspired by Ovrld - shown in a presentation by Nicolai Josuttis
template <SemiMovable... Fns>
struct overload : Fns... {
  constexpr overload() = default;
  constexpr explicit overload(Fns... fns) requires sizeof...(Fns) == 1
      : Fns(std::move(fns))... {}
  constexpr overload(Fns... fns) requires sizeof...(Fns) > 1
      : Fns(std::move(fns))... {}
  using Fns::operator()...;
};

template <class... F>
overload(F...) -> overload<F...>;

template <class... Fns>
struct on_value : overload<Fns...> {
  constexpr on_value() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class... F>
on_value(F...)->on_value<F...>;

template <class... Fns>
struct on_error : overload<Fns...> {
  constexpr on_error() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class... F>
on_error(F...)->on_error<F...>;

template <class... Fns>
struct on_done : overload<Fns...> {
  constexpr on_done() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class F>
on_done(F)->on_done<F>;

template <class... Fns>
struct on_stopping : overload<Fns...> {
  constexpr on_stopping() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class F>
on_stopping(F)->on_stopping<F>;

template <class... Fns>
struct on_starting : overload<Fns...> {
  constexpr on_starting() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class... F>
on_starting(F...)->on_starting<F...>;

template <class... Fns>
struct on_submit : overload<Fns...> {
  constexpr on_submit() = default;
  using overload<Fns...>::overload;
  using Fns::operator()...;
};

template <class... F>
on_submit(F...)->on_submit<F...>;

} // namespace pushmi
