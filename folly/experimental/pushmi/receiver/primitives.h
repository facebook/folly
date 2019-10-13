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

#include <exception>
#include <future>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/lang/CustomizationPoint.h>
#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/receiver/tags.h>

namespace folly {
namespace pushmi {

// Receiver primatives and customization points
//

/// \cond
namespace detail {
template <class R, class = void>
struct basic_receiver_traits {
};

template <class R>
struct basic_receiver_traits<R, void_t<typename R::receiver_category>> {
  using receiver_category = typename R::receiver_category;
};
} // namespace detail
/// \endcond

template<typename R, typename>
struct receiver_traits
  : std::conditional_t<
      std::is_same<std::decay_t<R>, R>::value,
      detail::basic_receiver_traits<R>,
      receiver_traits<std::decay_t<R>>> {
  using _not_specialized = void;
};

/// A std::promise is a valid Receiver:
template <class T>
struct receiver_traits<std::promise<T>> {
  using receiver_category = receiver_tag;
};
template <>
struct receiver_traits<std::promise<void>> {
  using receiver_category = receiver_tag;
};

namespace _adl {

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
void set_error(std::promise<T>& p, E e) noexcept {
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
  PUSHMI_TEMPLATE_DEBUG(class S)
  (requires //
   requires(
       set_done(std::declval<S&>()),
       set_error(std::declval<S&>(), std::current_exception()))) //
  void operator()(S& s) const noexcept {
#if FOLLY_HAS_EXCEPTIONS
    try {
      set_done(s);
    } catch (...) {
      set_error(s, std::current_exception());
    }
#else // FOLLY_HAS_EXCEPTIONS
    set_done(s);
#endif // FOLLY_HAS_EXCEPTIONS
  }
};
struct set_error_fn {
  PUSHMI_TEMPLATE_DEBUG(class S, class E)
  (requires //
   requires(set_error(std::declval<S&>(), std::declval<E>()))) //
  void operator()(S& s, E&& e) const //
      noexcept(noexcept(set_error(s, (E &&)e))) {
    set_error(s, (E &&)e);
  }
};
struct set_value_fn {
  PUSHMI_TEMPLATE_DEBUG(class S, class... VN)
  (requires //
   requires(
       set_value(std::declval<S&>(), std::declval<VN>()...),
       set_error(std::declval<S&>(), std::current_exception()))) //
  void operator()(S& s, VN&&... vn) const noexcept {
#if FOLLY_HAS_EXCEPTIONS
    try {
      set_value(s, (VN &&) vn...);
    } catch (...) {
      set_error(s, std::current_exception());
    }
#else // FOLLY_HAS_EXCEPTIONS
    set_value(s, (VN &&) vn...);
#endif // FOLLY_HAS_EXCEPTIONS
  }
};

struct set_starting_fn {
  PUSHMI_TEMPLATE_DEBUG(class S, class Up)
  (requires //
   requires(
       set_starting(std::declval<S&>(), std::declval<Up>()),
       set_error(std::declval<S&>(), std::current_exception()))) //
  void operator()(S& s, Up&& up) const noexcept {
#if FOLLY_HAS_EXCEPTIONS
    try {
      set_starting(s, (Up &&) up);
    } catch (...) {
      set_error(s, std::current_exception());
    }
#else // FOLLY_HAS_EXCEPTIONS
    set_starting(s, (Up &&) up);
#endif // FOLLY_HAS_EXCEPTIONS
  }
};

} // namespace _adl

FOLLY_DEFINE_CPO(_adl::set_done_fn, set_done)
FOLLY_DEFINE_CPO(_adl::set_error_fn, set_error)
FOLLY_DEFINE_CPO(_adl::set_value_fn, set_value)
FOLLY_DEFINE_CPO(_adl::set_starting_fn, set_starting)

} // namespace pushmi
} // namespace folly
