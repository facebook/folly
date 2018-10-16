#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <future>
#include <functional>

#include "traits.h"

namespace pushmi {
namespace __adl {
PUSHMI_TEMPLATE (class S)
  (requires requires (std::declval<S&>().done()))
void set_done(S& s) noexcept(noexcept(s.done())) {
  s.done();
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires (std::declval<S&>().error(std::declval<E>())))
void set_error(S& s, E e) noexcept(noexcept(s.error(std::move(e)))) {
  s.error(std::move(e));
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>().value(std::declval<V>())))
void set_value(S& s, V&& v) noexcept(noexcept(s.value((V&&) v))) {
  s.value((V&&) v);
}

PUSHMI_TEMPLATE (class S)
  (requires requires (std::declval<S&>().stopping()))
void set_stopping(S& s) noexcept(noexcept(s.stopping())) {
  s.stopping();
}
PUSHMI_TEMPLATE (class S, class Up)
  (requires requires (std::declval<S&>().starting(std::declval<Up&>())))
void set_starting(S& s, Up& up) noexcept(noexcept(s.starting(up))) {
  s.starting(up);
}

PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires (std::declval<SD&>().submit(std::declval<Out>())))
void submit(SD& sd, Out out) noexcept(noexcept(sd.submit(std::move(out)))) {
  sd.submit(std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>().now()))
auto now(SD& sd) noexcept(noexcept(sd.now())) {
  return sd.now();
}

PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    std::declval<SD&>().submit(
        std::declval<TP(&)(TP)>()(std::declval<SD&>().now()),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd.submit(std::move(tp), std::move(out)))) {
  sd.submit(std::move(tp), std::move(out));
}

template <class T>
void set_done(std::promise<T>& p) noexcept(
    noexcept(p.set_exception(std::make_exception_ptr(0)))) {
  p.set_exception(std::make_exception_ptr(
      std::logic_error("std::promise does not support done.")));
}
inline void set_done(std::promise<void>& p) noexcept(noexcept(p.set_value())) {
  p.set_value();
}
template <class T>
void set_error(std::promise<T>& s, std::exception_ptr e) noexcept {
  s.set_exception(std::move(e));
}
template <class T, class E>
void set_error(std::promise<T>& s, E e) noexcept {
  s.set_exception(std::make_exception_ptr(std::move(e)));
}
template <class T>
void set_value(std::promise<T>& s, T t) {
  s.set_value(std::move(t));
}

PUSHMI_TEMPLATE (class S)
  (requires requires ( set_done(std::declval<S&>()) ))
void set_done(std::reference_wrapper<S> s) noexcept(
  noexcept(set_done(s.get()))) {
  set_done(s.get());
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires ( set_error(std::declval<S&>(), std::declval<E>()) ))
void set_error(std::reference_wrapper<S> s, E e) noexcept {
  set_error(s.get(), std::move(e));
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires ( set_value(std::declval<S&>(), std::declval<V>()) ))
void set_value(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_value(s.get(), (V&&) v))) {
  set_value(s.get(), (V&&) v);
}
PUSHMI_TEMPLATE (class S)
  (requires requires ( set_stopping(std::declval<S&>()) ))
void set_stopping(std::reference_wrapper<S> s) noexcept(
  noexcept(set_stopping(s.get()))) {
  set_stopping(s.get());
}
PUSHMI_TEMPLATE (class S, class Up)
  (requires requires ( set_starting(std::declval<S&>(), std::declval<Up&>()) ))
void set_starting(std::reference_wrapper<S> s, Up& up) noexcept(
  noexcept(set_starting(s.get(), up))) {
  set_starting(s.get(), up);
}
PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires ( submit(std::declval<SD&>(), std::declval<Out>()) ))
void submit(std::reference_wrapper<SD> sd, Out out) noexcept(
  noexcept(submit(sd.get(), std::move(out)))) {
  submit(sd.get(), std::move(out));
}
PUSHMI_TEMPLATE (class SD)
  (requires requires ( now(std::declval<SD&>()) ))
auto now(std::reference_wrapper<SD> sd) noexcept(noexcept(now(sd.get()))) {
  return now(sd.get());
}
PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    submit(
      std::declval<SD&>(),
      std::declval<TP(&)(TP)>()(now(std::declval<SD&>())),
      std::declval<Out>())
  ))
void submit(std::reference_wrapper<SD> sd, TP tp, Out out)
  noexcept(noexcept(submit(sd.get(), std::move(tp), std::move(out)))) {
  submit(sd.get(), std::move(tp), std::move(out));
}


struct set_done_fn {
  PUSHMI_TEMPLATE (class S)
    (requires requires (
      set_done(std::declval<S&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s) const noexcept(noexcept(set_done(s))) {
    try {
      set_done(s);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};
struct set_error_fn {
  PUSHMI_TEMPLATE (class S, class E)
    (requires requires (
      set_error(std::declval<S&>(), std::declval<E>())
    ))
  void operator()(S&& s, E e) const
      noexcept(noexcept(set_error(s, std::move(e)))) {
    set_error(s, std::move(e));
  }
};
struct set_value_fn {
  PUSHMI_TEMPLATE (class S, class V)
    (requires requires (
      set_value(std::declval<S&>(), std::declval<V>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, V&& v) const
      noexcept(noexcept(set_value(s, (V&&) v))) {
    try {
      set_value(s, (V&&) v);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct set_stopping_fn {
  PUSHMI_TEMPLATE (class S)
    (requires requires (
      set_stopping(std::declval<S&>())
    ))
  void operator()(S&& s) const noexcept(noexcept(set_stopping(s))) {
    set_stopping(s);
  }
};
struct set_starting_fn {
  PUSHMI_TEMPLATE (class S, class Up)
    (requires requires (
      set_starting(std::declval<S&>(), std::declval<Up&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, Up& up) const
      noexcept(noexcept(set_starting(s, up))) {
    try {
      set_starting(s, up);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct do_submit_fn {
  PUSHMI_TEMPLATE (class SD, class Out)
    (requires requires (
      submit(std::declval<SD&>(), std::declval<Out>())
    ))
  void operator()(SD&& s, Out out) const
      noexcept(noexcept(submit(s, std::move(out)))) {
    submit(s, std::move(out));
  }

  PUSHMI_TEMPLATE (class SD, class TP, class Out)
    (requires requires (
      submit(
        std::declval<SD&>(),
        std::declval<TP>(),
        std::declval<Out>())
    ))
  void operator()(SD&& s, TP tp, Out out) const
      noexcept(noexcept(submit(s, std::move(tp), std::move(out)))) {
    submit(s, std::move(tp), std::move(out));
  }
};

struct get_now_fn {
  PUSHMI_TEMPLATE (class SD)
    (requires requires (
      now(std::declval<SD&>())
    ))
  auto operator()(SD&& sd) const noexcept(noexcept(now(sd))) {
    return now(sd);
  }
};

} // namespace __adl

PUSHMI_INLINE_VAR constexpr __adl::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr __adl::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr __adl::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr __adl::set_stopping_fn set_stopping{};
PUSHMI_INLINE_VAR constexpr __adl::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr __adl::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr __adl::get_now_fn now{};
PUSHMI_INLINE_VAR constexpr __adl::get_now_fn top{};

template <class T>
struct property_set_traits<std::promise<T>> {
  using properties = property_set<is_receiver<>, is_single<>>;
};
template <>
struct property_set_traits<std::promise<void>> {
  using properties = property_set<is_receiver<>, is_none<>>;
};

} // namespace pushmi
