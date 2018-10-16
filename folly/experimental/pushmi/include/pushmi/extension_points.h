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
template <class S>
requires requires(S& s) {
  s.done();
}
void set_done(S& s) noexcept(noexcept(s.done())) {
  s.done();
}
template <class S, class E>
requires requires(S& s, E e) {
  s.error(std::move(e));
}
void set_error(S& s, E e) noexcept(noexcept(s.error(std::move(e)))) {
  s.error(std::move(e));
}
template <class S, class V>
requires requires(S& s, V&& v) {
  s.value((V&&) v);
}
void set_value(S& s, V&& v) noexcept(noexcept(s.value((V&&) v))) {
  s.value((V&&) v);
}

template <class S>
requires requires(S& s) {
  s.stopping();
}
void set_stopping(S& s) noexcept(noexcept(s.stopping())) {
  s.stopping();
}
template <class S, class Up>
requires requires(S& s, Up& up) {
  s.starting(up);
}
void set_starting(S& s, Up& up) noexcept(noexcept(s.starting(up))) {
  s.starting(up);
}

template <class SD, class Out>
requires requires(SD& sd, Out out) {
  sd.submit(std::move(out));
}
void submit(SD& sd, Out out) noexcept(noexcept(sd.submit(std::move(out)))) {
  sd.submit(std::move(out));
}

template <class SD>
requires requires(SD& sd) {
  sd.now();
}
auto now(SD& sd) noexcept(noexcept(sd.now())) {
  return sd.now();
}

template <class SD, class TP, class Out>
requires requires(SD& sd, TP tp, Out out) {
  { sd.now() } -> TP;
  sd.submit(std::move(tp), std::move(out));
}
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
requires !Same<T, void>
void set_value(std::promise<T>& s, T t) {
  s.set_value(std::move(t));
}

template <class S>
requires requires (S& s) { set_done(s); }
void set_done(std::reference_wrapper<S> s) noexcept(
  noexcept(set_done(s.get()))) {
  set_done(s.get());
}
template <class S, class E>
requires requires (S& s, E e) { set_error(s, std::move(e)); }
void set_error(std::reference_wrapper<S> s, E e) noexcept {
  set_error(s.get(), std::move(e));
}
template <class S, class V>
requires requires (S& s, V&& v) { set_value(s, (V&&) v); }
void set_value(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_value(s.get(), (V&&) v))) {
  set_value(s.get(), (V&&) v);
}
template <class S>
requires requires(S& s) { set_stopping(s); }
void set_stopping(std::reference_wrapper<S> s) noexcept(
  noexcept(set_stopping(s.get()))) {
  set_stopping(s.get());
}
template <class S, class Up>
requires requires(S& s, Up& up) { set_starting(s, up); }
void set_starting(std::reference_wrapper<S> s, Up& up) noexcept(
  noexcept(set_starting(s.get(), up))) {
  set_starting(s.get(), up);
}
template <class SD, class Out>
requires requires(SD& sd, Out out) { submit(sd, std::move(out)); }
void submit(std::reference_wrapper<SD> sd, Out out) noexcept(
  noexcept(submit(sd.get(), std::move(out)))) {
  submit(sd.get(), std::move(out));
}
template <class SD>
requires requires(SD& sd) { now(sd); }
auto now(std::reference_wrapper<SD> sd) noexcept(noexcept(now(sd.get()))) {
  return now(sd.get());
}
template <class SD, class TP, class Out>
requires requires(SD& sd, TP tp, Out out) {
  submit(sd, std::move(tp), std::move(out));
}
void submit(std::reference_wrapper<SD> sd, TP tp, Out out)
  noexcept(noexcept(submit(sd.get(), std::move(tp), std::move(out)))) {
  submit(sd.get(), std::move(tp), std::move(out));
}


struct set_done_fn {
  template <class S>
  requires requires(S& s) {
    set_done(s);
    set_error(s, std::current_exception());
  }
  void operator()(S&& s) const noexcept(noexcept(set_done(s))) {
    try {
      set_done(s);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};
struct set_error_fn {
  template <class S, class E>
  requires requires(S& s, E e) {
    { set_error(s, std::move(e)) } noexcept;
  }
  void operator()(S&& s, E e) const
      noexcept(noexcept(set_error(s, std::move(e)))) {
    set_error(s, std::move(e));
  }
};
struct set_value_fn {
  template <class S, class V>
  requires requires(S& s, V&& v) {
    set_value(s, (V&&) v);
    set_error(s, std::current_exception());
  }
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
  template <class S>
  requires requires(S& s) {
    set_stopping(s);
  }
  void operator()(S&& s) const noexcept(noexcept(set_stopping(s))) {
    set_stopping(s);
  }
};
struct set_starting_fn {
  template <class S, class Up>
  requires requires(S& s, Up& up) {
    set_starting(s, up);
    set_error(s, std::current_exception());
  }
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
  template <class SD, class Out>
  requires requires(SD& s, Out out) {
    submit(s, std::move(out));
  }
  void operator()(SD&& s, Out out) const
      noexcept(noexcept(submit(s, std::move(out)))) {
    submit(s, std::move(out));
  }

  template <class SD, class TP, class Out>
  requires requires(SD& s, TP tp, Out out) {
    submit(s, std::move(tp), std::move(out));
  }
  void operator()(SD&& s, TP tp, Out out) const
      noexcept(noexcept(submit(s, std::move(tp), std::move(out)))) {
    submit(s, std::move(tp), std::move(out));
  }
};

struct get_now_fn {
  template <class SD>
  requires requires(SD& sd) {
    now(sd);
  }
  auto operator()(SD&& sd) const noexcept(noexcept(now(sd))) {
    return now(sd);
  }
};

} // namespace __adl

inline constexpr __adl::set_done_fn set_done{};
inline constexpr __adl::set_error_fn set_error{};
inline constexpr __adl::set_value_fn set_value{};
inline constexpr __adl::set_stopping_fn set_stopping{};
inline constexpr __adl::set_starting_fn set_starting{};
inline constexpr __adl::do_submit_fn submit{};
inline constexpr __adl::get_now_fn now{};
inline constexpr __adl::get_now_fn top{};

template <class T>
struct receiver_traits<std::promise<T>> {
  using receiver_category = single_tag;
};
template <>
struct receiver_traits<std::promise<void>> {
  using receiver_category = none_tag;
};

} // namespace pushmi
