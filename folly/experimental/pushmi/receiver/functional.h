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
#include <utility>

#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/traits.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/receiver/primitives.h>

namespace folly {
namespace pushmi {

struct ignoreNF {
  void operator()(detail::any) {}
};

struct ignoreVF {
  template<class... VN>
  void operator()(VN&&...) {}
};

struct abortEF {
  [[noreturn]]
  void operator()(detail::any) noexcept {
    std::terminate();
  }
};

struct ignoreDF {
  void operator()() {}
};

struct ignoreStrtF {
  void operator()(detail::any) {}
};

struct passDVF {
  PUSHMI_TEMPLATE(class Data, class... VN)
    (requires ReceiveValue<Data, VN...>)
  void operator()(Data& out, VN&&... vn) const {
    set_value(out, (VN&&) vn...);
  }
};

struct passDEF {
  PUSHMI_TEMPLATE(class Data, class E)
    (requires ReceiveError<Data, E>)
  void operator()(Data& out, E&& e) const noexcept {
    set_error(out, (E&&)e);
  }
};

struct passDDF {
  PUSHMI_TEMPLATE(class Data)
    (requires Receiver<Data>)
  void operator()(Data& out) const {
    set_done(out);
  }
};

struct passDStrtF {
  PUSHMI_TEMPLATE(class Up, class Data)
    (requires requires (
      set_starting(std::declval<Data&>(), std::declval<Up>())
    ) && Receiver<Data>)
  void operator()(Data& out, Up&& up) const {
    set_starting(out, (Up&&) up);
  }
};

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

template <class... Fns>
struct on_starting_fn : overload_fn<Fns...> {
  constexpr on_starting_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_starting(Fns... fns) -> on_starting_fn<Fns...> {
  return on_starting_fn<Fns...>{std::move(fns)...};
}

} // namespace pushmi
} // namespace folly
