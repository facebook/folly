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

//
// support methods on a class reference
//

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
  (requires requires (std::declval<S&>().value(std::declval<V&&>())))
void set_value(S& s, V&& v) noexcept(noexcept(s.value((V&&) v))) {
  s.value((V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>().next(std::declval<V&&>())))
void set_next(S& s, V&& v) noexcept(noexcept(s.next((V&&) v))) {
  s.next((V&&) v);
}

PUSHMI_TEMPLATE (class S, class Up)
  (requires requires (std::declval<S&>().starting(std::declval<Up&&>())))
void set_starting(S& s, Up&& up) noexcept(noexcept(s.starting((Up&&) up))) {
  s.starting((Up&&) up);
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>().executor()))
auto executor(SD& sd) noexcept(noexcept(sd.executor())) {
  return sd.executor();
}

PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires (std::declval<SD&>().submit(std::declval<Out>())))
void submit(SD& sd, Out out) noexcept(noexcept(sd.submit(std::move(out)))) {
  sd.submit(std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>().top()))
auto top(SD& sd) noexcept(noexcept(sd.top())) {
  return sd.top();
}

PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    std::declval<SD&>().submit(
        std::declval<TP(&)(TP)>()(std::declval<SD&>().top()),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd.submit(std::move(tp), std::move(out)))) {
  sd.submit(std::move(tp), std::move(out));
}

//
// support methods on a class pointer
//

PUSHMI_TEMPLATE (class S)
  (requires requires (std::declval<S&>()->done()))
void set_done(S& s) noexcept(noexcept(s->done())) {
  s->done();
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires (std::declval<S&>()->error(std::declval<E>())))
void set_error(S& s, E e) noexcept(noexcept(s->error(std::move(e)))) {
  s->error(std::move(e));
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>()->value(std::declval<V&&>())))
void set_value(S& s, V&& v) noexcept(noexcept(s->value((V&&) v))) {
  s->value((V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>()->next(std::declval<V&&>())))
void set_next(S& s, V&& v) noexcept(noexcept(s->next((V&&) v))) {
  s->next((V&&) v);
}

PUSHMI_TEMPLATE (class S, class Up)
  (requires requires (std::declval<S&>()->starting(std::declval<Up&&>())))
void set_starting(S& s, Up&& up) noexcept(noexcept(s->starting((Up&&) up))) {
  s->starting((Up&&) up);
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>()->executor()))
auto executor(SD& sd) noexcept(noexcept(sd->executor())) {
  return sd->executor();
}

PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires (std::declval<SD&>()->submit(std::declval<Out>())))
void submit(SD& sd, Out out) noexcept(noexcept(sd->submit(std::move(out)))) {
  sd->submit(std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>()->top()))
auto top(SD& sd) noexcept(noexcept(sd->top())) {
  return sd->top();
}

PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    std::declval<SD&>()->submit(
        std::declval<TP(&)(TP)>()(std::declval<SD&>()->top()),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd->submit(std::move(tp), std::move(out)))) {
  sd->submit(std::move(tp), std::move(out));
}

//
// add support for std::promise externally
//

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

//
// support reference_wrapper
//

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
  (requires requires ( set_value(std::declval<S&>(), std::declval<V&&>()) ))
void set_value(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_value(s.get(), (V&&) v))) {
  set_value(s.get(), (V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires ( set_next(std::declval<S&>(), std::declval<V&&>()) ))
void set_next(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_next(s.get(), (V&&) v))) {
  set_next(s.get(), (V&&) v);
}
PUSHMI_TEMPLATE (class S, class Up)
  (requires requires ( set_starting(std::declval<S&>(), std::declval<Up&&>()) ))
void set_starting(std::reference_wrapper<S> s, Up&& up) noexcept(
  noexcept(set_starting(s.get(), (Up&&) up))) {
  set_starting(s.get(), (Up&&) up);
}
PUSHMI_TEMPLATE (class SD)
  (requires requires ( executor(std::declval<SD&>()) ))
auto executor(std::reference_wrapper<SD> sd) noexcept(noexcept(executor(sd.get()))) {
  return executor(sd.get());
}
PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires ( submit(std::declval<SD&>(), std::declval<Out>()) ))
void submit(std::reference_wrapper<SD> sd, Out out) noexcept(
  noexcept(submit(sd.get(), std::move(out)))) {
  submit(sd.get(), std::move(out));
}
PUSHMI_TEMPLATE (class SD)
  (requires requires ( top(std::declval<SD&>()) ))
auto top(std::reference_wrapper<SD> sd) noexcept(noexcept(top(sd.get()))) {
  return top(sd.get());
}
PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    submit(
      std::declval<SD&>(),
      std::declval<TP(&)(TP)>()(top(std::declval<SD&>())),
      std::declval<Out>())
  ))
void submit(std::reference_wrapper<SD> sd, TP tp, Out out)
  noexcept(noexcept(submit(sd.get(), std::move(tp), std::move(out)))) {
  submit(sd.get(), std::move(tp), std::move(out));
}

//
// accessors for free functions in this namespace
//

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
      set_value(std::declval<S&>(), std::declval<V&&>()),
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
struct set_next_fn {
  PUSHMI_TEMPLATE (class S, class V)
    (requires requires (
      set_next(std::declval<S&>(), std::declval<V&&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, V&& v) const
      noexcept(noexcept(set_next(s, (V&&) v))) {
    try {
      set_next(s, (V&&) v);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct set_starting_fn {
  PUSHMI_TEMPLATE (class S, class Up)
    (requires requires (
      set_starting(std::declval<S&>(), std::declval<Up&&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, Up&& up) const
    noexcept(noexcept(set_starting(s, (Up&&) up))) {
    try {
      set_starting(s, (Up&&) up);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct get_executor_fn {
  PUSHMI_TEMPLATE (class SD)
    (requires requires (
      executor(std::declval<SD&>())
    ))
  auto operator()(SD&& sd) const noexcept(noexcept(executor(sd))) {
    return executor(sd);
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

struct get_top_fn {
  PUSHMI_TEMPLATE (class SD)
    (requires requires (
      top(std::declval<SD&>())
    ))
  auto operator()(SD&& sd) const noexcept(noexcept(top(sd))) {
    return top(sd);
  }
};

} // namespace __adl

PUSHMI_INLINE_VAR constexpr __adl::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr __adl::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr __adl::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr __adl::set_next_fn set_next{};
PUSHMI_INLINE_VAR constexpr __adl::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr __adl::get_executor_fn executor{};
PUSHMI_INLINE_VAR constexpr __adl::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn now{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn top{};

template <class T>
struct property_set_traits<std::promise<T>> {
  using properties = property_set<is_receiver<>, is_single<>>;
};
template <>
struct property_set_traits<std::promise<void>> {
  using properties = property_set<is_receiver<>, is_none<>>;
};

} // namespace pushmi
