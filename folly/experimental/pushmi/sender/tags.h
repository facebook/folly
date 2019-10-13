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

#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

namespace awaitable_senders {
struct sender_adl_hook {
};
} // namespace awaitable_senders

// A sender inherits from
// sender_base<Category>::with_values<Values...>::with_error<Error> to
// become a TypedSender (where the Category is one of [single, many,
// flow_single, or flow_many; and where the ::with_error<Error> part is
// optional and defaults to ::with_error<std::exception_ptr>). If a sender can
// pass values As... or Bs... to a receiver, then the sender can inherit from
// sender_base<Category>::with_values<As...>::or_<Bs...>::with_error<Error>. More
// sets of alternate values can be added by tacking on more additional
// ::or_<...> instantiations; e.g.,
// sender_base<Category>::with_values<As...>::or_<Bs...>::or_<Cs...>::with_error<Error>
template <
  class Category,
  class BaseCategory = awaitable_senders::sender_adl_hook>
struct sender_base : BaseCategory {
  using sender_category = Category;

  struct no_values : private awaitable_senders::sender_adl_hook {
    using sender_category = Category;

    template<template<class...> class, template<class...> class Variant>
    using value_types = Variant<>;

    template<template<class...> class Variant>
    using error_type = Variant<std::exception_ptr>;

    struct no_error : private awaitable_senders::sender_adl_hook {
      using sender_category = Category;

      template<template<class...> class, template<class...> class Variant>
      using value_types = Variant<>;

      template<template<class...> class Variant>
      using error_type = Variant<>;
    };
  };

  template<class... Values>
  struct with_values : private awaitable_senders::sender_adl_hook {
    using sender_category = Category;

    template<template<class...> class Tuple, template<class...> class Variant>
    using value_types = Variant<Tuple<Values...>>;

    template<template<class...> class Variant>
    using error_type = Variant<std::exception_ptr>;

    struct no_error : private awaitable_senders::sender_adl_hook {
      using sender_category = Category;

      template<template<class...> class Tuple, template<class...> class Variant>
      using value_types = Variant<Tuple<Values...>>;

      template<template<class...> class Variant>
      using error_type = Variant<>;
    };

    template <class Error>
    struct with_error : private awaitable_senders::sender_adl_hook {
      using sender_category = Category;

      template<template<class...> class Tuple, template<class...> class Variant>
      using value_types = value_types<Tuple, Variant>;

      template<template<class...> class Variant>
      using error_type = Variant<Error>;

      template<typename Base, typename OtherError>
      struct _or_ : private awaitable_senders::sender_adl_hook {
      private:
        template<template<class...> class Variant>
        struct error_type_ {
          template<typename... BaseErrors>
          using make_variant_ = Variant<BaseErrors..., OtherError>;
        };
      public:
        using sender_category = Category;

        template<template<class...> class Tuple, template<class...> class Variant>
        using value_types =
          typename Base::template value_types<Tuple, Variant>;

        template<template<class...> class Variant>
        using error_type =
          typename Base::template error_type<
            error_type_<Variant>::template make_variant_>;

        template<typename YetOtherError>
        using or_ = _or_<_or_, YetOtherError>;
      };

      template<typename OtherError>
      using or_ = _or_<with_error, OtherError>;
    };

    template<typename Base, typename...MoreValues>
    struct _or_ : private awaitable_senders::sender_adl_hook {
    private:
      template<template<class...> class Tuple, template<class...> class Variant>
      struct value_types_ {
        template<typename... BaseTuples>
        using make_variant_ = Variant<BaseTuples..., Tuple<MoreValues...>>;
      };
    public:
      using sender_category = Category;

      template<template<class...> class Tuple, template<class...> class Variant>
      using value_types =
        typename Base::template value_types<
          Tuple,
          value_types_<Tuple, Variant>::template make_variant_>;

      template<template<class...> class Variant>
      using error_type = Variant<std::exception_ptr>;

      struct no_error : private awaitable_senders::sender_adl_hook {
        using sender_category = Category;

        template<
          template<class...> class Tuple,
          template<class...> class Variant>
        using value_types = value_types<Tuple, Variant>;

        template<template<class...> class Variant>
        using error_type = Variant<>;
      };

      template <class Error>
      struct with_error : private awaitable_senders::sender_adl_hook {
        using sender_category = Category;

        template<
          template<class...> class Tuple,
          template<class...> class Variant>
        using value_types = value_types<Tuple, Variant>;

        template<template<class...> class Variant>
        using error_type = Variant<Error>;

        template<typename OtherError>
        using or_ =
          typename with_values::template with_error<Error>::
            template _or_<with_error, OtherError>;
      };

      template<typename...EvenMoreValues>
      using or_ = _or_<_or_, EvenMoreValues...>;
    };

    template<typename...MoreValues>
    using or_ = _or_<with_values, MoreValues...>;
  };
};


// traits & tags
struct sender_tag
  : sender_base<sender_tag> {
};
namespace detail {
struct virtual_sender_tag
  : virtual sender_tag {
};
} // namespace detail

struct single_sender_tag
  : sender_base<single_sender_tag, detail::virtual_sender_tag> {
};

struct flow_sender_tag
  : sender_base<flow_sender_tag, detail::virtual_sender_tag> {
};
struct flow_single_sender_tag
  : sender_base<
      flow_single_sender_tag,
      detail::inherit<flow_sender_tag, single_sender_tag>> {
};

} // namespace pushmi
} // namespace folly
