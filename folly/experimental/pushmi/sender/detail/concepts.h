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

#include <utility>
#include <type_traits>

#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/sender/tags.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {
namespace detail {
  template<template<template<class...> class, template<class...> class> class>
  struct test_value_types;
  template<template<template<class...> class> class>
  struct test_error_type;

  PUSHMI_CONCEPT_DEF(
    template(class S)
    concept SenderLike_,
      True<typename S::sender_category>
  );

  PUSHMI_CONCEPT_DEF(
    template(class S)
    concept TypedSenderLike_,
      SenderLike_<S> &&
      True<test_value_types<S::template value_types>> &&
      True<test_error_type<S::template error_type>>
  );
} // namespace detail

PUSHMI_CONCEPT_DEF(
  template(class S)
  concept Sender,
    SemiMovable<remove_cvref_t<S>> &&
    True<typename sender_traits<S>::sender_category> &&
    DerivedFrom<typename sender_traits<S>::sender_category, sender_tag>
);

template<class S>
PUSHMI_PP_CONSTRAINED_USING(
  Sender<S>,
  sender_category_t =,
    typename sender_traits<S>::sender_category
);

PUSHMI_CONCEPT_DEF(
  template(class S)
  concept SingleSender,
    Sender<S> &&
    DerivedFrom<typename sender_traits<S>::sender_category, single_sender_tag>
);

PUSHMI_CONCEPT_DEF(
  template(class S)
  concept TypedSender,
    Sender<S> &&
    True<detail::test_value_types<sender_traits<S>::template value_types>> &&
    True<detail::test_error_type<sender_traits<S>::template error_type>>
);

template<
  class From,
  template<class...> class Tuple,
  template<class...> class Variant = detail::identity_t>
PUSHMI_PP_CONSTRAINED_USING(
  TypedSender<From>,
  sender_values_t =,
    typename sender_traits<remove_cvref_t<From>>::
      template value_types<Tuple, Variant>
);

template<class From, template<class...> class Variant = detail::identity_t>
PUSHMI_PP_CONSTRAINED_USING(
  TypedSender<From>,
  sender_error_t =,
    typename sender_traits<remove_cvref_t<From>>::
      template error_type<Variant>
);

namespace detail {
template<class... Ts>
using count_values = std::integral_constant<std::size_t, sizeof...(Ts)>;
} // namespace detail

PUSHMI_CONCEPT_DEF(
  template(class S)
  concept SingleTypedSender,
    TypedSender<S> &&
    SingleSender<S> &&
    (sender_values_t<S, detail::count_values>::value <= 1u)
);

// /// \cond
// template<class Fun>
// struct __invoke_with {
//   template<typename...Args>
//   using result_t = std::invoke_result_t<Fun, Args...>;
//   template<template<class...> class Tuple>
//   struct as {
//     template<typename...Args>
//     using result_t =
//       std::conditional_t<
//         std::is_void<result_t<Args...>>::value,
//         Tuple<>,
//         Tuple<result_t<Args...>>>;
//   };
// };
// /// \endcond
//
// template<class Fun, TypedSender From>
//   requires requires {
//     typename sender_traits<From>::template value_types<
//       __invoke_with<Fun>::template result_t, __typelist>;
//   }
// struct transformed_sender_of : sender<sender_category_t<From>> {
//   template<template<class...> class Variant = identity_t>
//   using error_type = sender_error_t<Variant>;
//   template<
//     template<class...> class Tuple,
//     template<class...> class Variant = identity_t>
//   using value_types =
//     sender_values_t<
//       From,
//       __invoke_with<Fun>::template as<Tuple>::template result_t,
//       Variant>;
// };

} // namespace pushmi
} // namespace folly
