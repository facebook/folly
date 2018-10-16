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
  PUSHMI_TEMPLATE(class... AN)
    (requires Constructible<T, AN...>)
  auto operator()(AN&&... an) const {
    return T{std::forward<AN>(an)...};
  }
};

template<template <class...> class T>
struct construct_deduced;

template<>
struct construct_deduced<none>;

template<>
struct construct_deduced<single>;

template <template <class...> class T, class... AN>
using deduced_type_t = pushmi::invoke_result_t<construct_deduced<T>, AN...>;

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
  PUSHMI_TEMPLATE(class V, class Data)
    (requires requires (
      ::pushmi::set_value(std::declval<Data&>(), std::declval<V>())
    ) && Receiver<Data>)
  void operator()(Data& out, V&& v) const {
    ::pushmi::set_value(out, (V&&) v);
  }
};

struct passDEF {
  PUSHMI_TEMPLATE(class E, class Data)
    (requires NoneReceiver<Data, E>)
  void operator()(Data& out, E e) const noexcept {
    ::pushmi::set_error(out, e);
  }
};

struct passDDF {
  PUSHMI_TEMPLATE(class Data)
    (requires Receiver<Data>)
  void operator()(Data& out) const {
    ::pushmi::set_done(out);
  }
};

struct passDStpF {
  PUSHMI_TEMPLATE(class Data)
    (requires Receiver<Data>)
  void operator()(Data& out) const {
    ::pushmi::set_stopping(out);
  }
};

struct passDStrtF {
  PUSHMI_TEMPLATE(class Up, class Data)
    (requires requires (
      ::pushmi::set_starting(std::declval<Data&>(), std::declval<Up&>())
    ) && Receiver<Data>)
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
  PUSHMI_TEMPLATE(class Data)
    (requires TimeSender<Data>)
  auto operator()(Data& in) const noexcept {
    return ::pushmi::now(in);
  }
};

// inspired by Ovrld - shown in a presentation by Nicolai Josuttis
#if __cpp_variadic_using >= 201611 && __cpp_concepts
template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... Fns>
  requires sizeof...(Fns) > 0
struct overload_fn : Fns... {
  constexpr overload_fn() = default;
  constexpr explicit overload_fn(Fns... fns) requires sizeof...(Fns) == 1
      : Fns(std::move(fns))... {}
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
  constexpr explicit overload_fn(Fn fn)
      : Fn(std::move(fn)) {}
  using Fn::operator();
};
template <class Fn, class... Fns>
struct overload_fn<Fn, Fns...> : Fn, overload_fn<Fns...> {
  constexpr overload_fn() = default;
  constexpr overload_fn(Fn fn, Fns... fns)
      : Fn(std::move(fn)), overload_fn<Fns...>{std::move(fns)...} {}
  using Fn::operator();
  using overload_fn<Fns...>::operator();
};
#endif

template <class... Fns>
auto overload(Fns... fns) -> overload_fn<Fns...> {
  return overload_fn<Fns...>{std::move(fns)...};
}

template <class... Fns>
struct on_value_fn : overload_fn<Fns...> {
  constexpr on_value_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_value(Fns... fns) -> on_value_fn<Fns...> {
  return on_value_fn<Fns...>{std::move(fns)...};
}

template <class... Fns>
struct on_error_fn : overload_fn<Fns...> {
  constexpr on_error_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_error(Fns... fns) -> on_error_fn<Fns...> {
  return on_error_fn<Fns...>{std::move(fns)...};
}

template <class Fn>
struct on_done_fn : Fn {
  constexpr on_done_fn() = default;
  constexpr explicit on_done_fn(Fn fn) : Fn(std::move(fn)) {}
  using Fn::operator();
};

template <class Fn>
auto on_done(Fn fn) -> on_done_fn<Fn> {
  return on_done_fn<Fn>{std::move(fn)};
}

template <class Fn>
struct on_stopping_fn : Fn {
  constexpr on_stopping_fn() = default;
  constexpr explicit on_stopping_fn(Fn fn) : Fn(std::move(fn)) {}
  using Fn::operator();
};

template <class Fn>
auto on_stopping(Fn fn) -> on_stopping_fn<Fn> {
  return on_stopping_fn<Fn>{std::move(fn)};
}

template <class... Fns>
struct on_starting_fn : overload_fn<Fns...> {
  constexpr on_starting_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_starting(Fns... fns) -> on_starting_fn<Fns...> {
  return on_starting_fn<Fns...>{std::move(fns)...};
}

template <class... Fns>
struct on_submit_fn : overload_fn<Fns...> {
  constexpr on_submit_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_submit(Fns... fns) -> on_submit_fn<Fns...> {
  return on_submit_fn<Fns...>{std::move(fns)...};
}

} // namespace pushmi
