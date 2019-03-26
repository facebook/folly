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
#include <future>

#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/properties.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {
namespace __adl {

//
// support methods on a class reference
//

PUSHMI_TEMPLATE(class S)
(requires //
 requires(std::declval<S&>().done())) //
    void set_done(S& s) //
    noexcept(noexcept((s).done())) {
  s.done();
}
PUSHMI_TEMPLATE(class S, class E)
(requires //
 requires(std::declval<S&>().error(std::declval<E>()))) //
    void set_error(S& s, E e) //
    noexcept(noexcept((s).error(std::move(e)))) {
  s.error(std::move(e));
}
PUSHMI_TEMPLATE(class S, class... VN)
(requires //
 requires(std::declval<S&>().value(std::declval<VN>()...))) //
    void set_value(S& s, VN&&... vn) //
    noexcept(noexcept(s.value((VN &&) vn...))) {
  s.value((VN &&) vn...);
}

PUSHMI_TEMPLATE(class S, class Up)
(requires //
 requires(std::declval<S&>().starting(std::declval<Up>()))) //
    void set_starting(S& s, Up&& up) //
    noexcept(noexcept(s.starting((Up &&) up))) {
  s.starting((Up &&) up);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().executor())) //
    auto get_executor(SD& sd) //
    noexcept(noexcept(sd.executor())) {
  return sd.executor();
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().make_strand())) //
    auto make_strand(SD& sd) //
    noexcept(noexcept(sd.make_strand())) {
  return sd.make_strand();
}

PUSHMI_TEMPLATE(class SD, class Out)
(requires //
 requires(std::declval<SD>().submit(std::declval<Out>()))) //
    void submit(SD&& sd, Out&& out) //
    noexcept(noexcept(((SD &&) sd).submit((Out &&) out))) {
  ((SD &&) sd).submit((Out &&) out);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().top())) //
    auto top(SD& sd) //
    noexcept(noexcept(sd.top())) {
  return sd.top();
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().schedule())) //
    auto schedule(SD& sd) //
    noexcept(noexcept(sd.schedule())) {
  return sd.schedule();
}

PUSHMI_TEMPLATE(class SD, class TP)
(requires //
 requires(std::declval<SD&>().schedule(
     std::declval<TP (&)(TP)>()(top(std::declval<SD&>()))))) //
    auto schedule(SD& sd, TP tp) //
    noexcept(noexcept(sd.schedule(std::move(tp)))) {
  return sd.schedule(std::move(tp));
}

//
// support methods on a class pointer
//

PUSHMI_TEMPLATE(class S)
(requires //
 requires(set_done(*std::declval<S>()))) //
    void set_done(S&& s) //
    noexcept(noexcept(set_done(*s))) {
  set_done(*s);
}
PUSHMI_TEMPLATE(class S, class E)
(requires //
 requires(set_error(*std::declval<S>(), std::declval<E>()))) //
    void set_error(S&& s, E e) //
    noexcept(noexcept(set_error(*s, std::move(e)))) {
  set_error(*s, std::move(e));
}
PUSHMI_TEMPLATE(class S, class... VN)
(requires //
 requires(set_value(*std::declval<S>(), std::declval<VN>()...))) //
    void set_value(S&& s, VN&&... vn) //
    noexcept(noexcept(set_value(*s, (VN &&) vn...))) {
  set_value(*s, (VN &&) vn...);
}

PUSHMI_TEMPLATE(class S, class Up)
(requires //
 requires(set_starting(*std::declval<S>(), std::declval<Up>()))) //
    void set_starting(S&& s, Up&& up) //
    noexcept(noexcept(set_starting(*s, (Up &&) up))) {
  set_starting(*s, (Up &&) up);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(get_executor(*std::declval<SD>()))) //
    auto get_executor(SD&& sd) //
    noexcept(noexcept(get_executor(*sd))) {
  return get_executor(*sd);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(make_strand(*std::declval<SD>()))) //
    auto make_strand(SD&& sd) //
    noexcept(noexcept(make_strand(*sd))) {
  return make_strand(*sd);
}

PUSHMI_TEMPLATE(class SD, class Out)
(requires //
 requires(submit(*std::declval<SD>(), std::declval<Out>()))) //
    void submit(SD&& sd, Out&& out) //
    noexcept(noexcept(submit(*sd, (Out &&) out))) {
  submit(*sd, (Out &&) out);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(top(*std::declval<SD>()))) //
    auto top(SD&& sd) //
    noexcept(noexcept(top(*sd))) {
  return top(*sd);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(schedule(*std::declval<SD>()))) //
    auto schedule(SD&& sd) //
    noexcept(noexcept(schedule(*sd))) {
  return schedule(*sd);
}

PUSHMI_TEMPLATE(class SD, class TP)
(requires //
 requires(schedule(
     *std::declval<SD>(),
     std::declval<TP (&)(TP)>()(top(std::declval<SD&>()))))) //
    auto schedule(SD&& sd, TP tp) //
    noexcept(noexcept(schedule(*sd, std::move(tp)))) {
  return schedule(*sd, std::move(tp));
}

//
// support a nullary function as a StrandFactory
//

PUSHMI_TEMPLATE(class S)
(requires Invocable<S&>) //
    auto make_strand(S& s) //
    noexcept(noexcept(s())) {
  return s();
}

//
// support a nullary function as a receiver
//

PUSHMI_TEMPLATE(class S)
(requires Invocable<S&>) //
    void set_done(S&) noexcept {}
PUSHMI_TEMPLATE(class S, class E)
(requires Invocable<S&>) //
    void set_error(S&, E&&) noexcept {
  std::terminate();
}
PUSHMI_TEMPLATE(class S)
(requires Invocable<S&>) //
    void set_value(S& s) //
    noexcept(noexcept(s())) {
  s();
}

//
// add support for std::promise externally
//

// std::promise does not support the done signal.
// either set_value or set_error must be called
template <class T>
void set_done(std::promise<T>&) noexcept {}

template <class T>
void set_error(std::promise<T>& p, std::exception_ptr e) noexcept {
  p.set_exception(std::move(e));
}
template <class T, class E>
void set_error(std::promise<T>& p, E e) //
    noexcept {
  p.set_exception(std::make_exception_ptr(std::move(e)));
}
PUSHMI_TEMPLATE(class T, class U)
(requires ConvertibleTo<U, T>) //
    void set_value(std::promise<T>& p, U&& u) //
    noexcept(noexcept(p.set_value((U &&) u))) {
  p.set_value((U &&) u);
}
inline void set_value(std::promise<void>& p) //
    noexcept(noexcept(p.set_value())) {
  p.set_value();
}

//
// accessors for free functions in this namespace
//

struct set_done_fn {
  PUSHMI_TEMPLATE(class S)
  (requires //
   requires(
       set_done(std::declval<S&>()),
       set_error(std::declval<S&>(), std::current_exception()))) //
      void
      operator()(S& s) const noexcept {
    try {
      set_done(s);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};
struct set_error_fn {
  PUSHMI_TEMPLATE(class S, class E)
  (requires //
   requires(set_error(std::declval<S&>(), std::declval<E>()))) //
      void
      operator()(S& s, E&& e) const //
      noexcept(noexcept(set_error(s, (E &&)e))) {
    set_error(s, (E &&)e);
  }
};
struct set_value_fn {
  PUSHMI_TEMPLATE(class S, class... VN)
  (requires //
   requires(
       set_value(std::declval<S&>(), std::declval<VN>()...),
       set_error(std::declval<S&>(), std::current_exception()))) //
      void
      operator()(S& s, VN&&... vn) const noexcept {
    try {
      set_value(s, (VN &&) vn...);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct set_starting_fn {
  PUSHMI_TEMPLATE(class S, class Up)
  (requires //
   requires(
       set_starting(std::declval<S&>(), std::declval<Up>()),
       set_error(std::declval<S&>(), std::current_exception()))) //
      void
      operator()(S& s, Up&& up) const noexcept {
    try {
      set_starting(s, (Up &&) up);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct get_executor_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires //
   requires(get_executor(std::declval<SD&>()))) //
      auto
      operator()(SD& sd) const //
      noexcept(noexcept(get_executor(sd))) {
    return get_executor(sd);
  }
};

struct make_strand_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires //
   requires(make_strand(std::declval<SD&>()))) //
      auto
      operator()(SD& sd) const //
      noexcept(noexcept(make_strand(sd))) {
    return make_strand(sd);
  }
};

struct do_submit_fn {
  PUSHMI_TEMPLATE(class SD, class Out)
  (requires //
   requires(submit(std::declval<SD>(), std::declval<Out>()))) //
      void
      operator()(SD&& s, Out&& out) const //
      noexcept(noexcept(submit((SD &&) s, (Out &&) out))) {
    submit((SD &&) s, (Out &&) out);
  }
};

struct do_schedule_fn {
  PUSHMI_TEMPLATE(class SD, class... VN)
  (requires //
   requires(schedule(std::declval<SD&>(), std::declval<VN>()...))) //
      auto
      operator()(SD& s, VN&&... vn) const //
      noexcept(noexcept(schedule(s, (VN &&) vn...))) {
    return schedule(s, (VN &&) vn...);
  }
};

struct get_top_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires //
   requires(top(std::declval<SD&>()))) //
      auto
      operator()(SD& sd) const //
      noexcept(noexcept(top(sd))) {
    return top(sd);
  }
};

} // namespace __adl

PUSHMI_INLINE_VAR constexpr __adl::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr __adl::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr __adl::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr __adl::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr __adl::get_executor_fn get_executor{};
PUSHMI_INLINE_VAR constexpr __adl::make_strand_fn make_strand{};
PUSHMI_INLINE_VAR constexpr __adl::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr __adl::do_schedule_fn schedule{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn now{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn top{};

template <class T>
struct property_set_traits<T*> : property_set_traits<T> {};

template <class T>
struct property_set_traits<
    T,
    void_t<invoke_result_t<T&>>> {
  using properties = property_set<is_receiver<>>;
};

template <class T>
struct property_set_traits<std::promise<T>> {
  using properties = property_set<is_receiver<>>;
};
template <>
struct property_set_traits<std::promise<void>> {
  using properties = property_set<is_receiver<>>;
};

} // namespace pushmi
} // namespace folly
