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

#include <folly/Portability.h>
#include <folly/Traits.h>
#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Traits.h>
#endif

#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/sender/detail/concepts.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

#if FOLLY_HAS_COROUTINES

namespace detail {

PUSHMI_CONCEPT_DEF(
  template(typename Tp)
  concept AwaitableLike_,
    SemiMovable<Tp> && coro::is_awaitable_v<Tp>
);

template<typename T>
struct awaitable_sender_traits_impl : awaitable_senders::sender_adl_hook {
  template<template<class...> class Tuple, template<class...> class Variant>
  using value_types = Variant<Tuple<T>>;

  template<template<class...> class Variant>
  using error_type = Variant<std::exception_ptr>;
};

template<>
struct awaitable_sender_traits_impl<void> : awaitable_senders::sender_adl_hook {
  template<template<class...> class Tuple, template<class...> class Variant>
  using value_types = Variant<Tuple<>>;

  template<template<class...> class Variant>
  using error_type = Variant<std::exception_ptr>;
};

template<class T>
struct IsAwaitableLike_
: std::integral_constant<bool, detail::AwaitableLike_<T>> {
};

std::true_type safe_to_test_awaitable(void const*);

template<
  typename T,
  // Don't ask if a type is awaitable if it inherits from
  // awaitable_senders::sender_adl_hook, because that will find the
  // default operator co_await that is constrained with TypedSingleSender
  // and cause a recursive template instantiation.
  bool = Conjunction<
    decltype(safe_to_test_awaitable(static_cast<T*>(nullptr))),
    IsAwaitableLike_<T>>::value>
struct awaitable_sender_traits {
};

template<typename T>
struct awaitable_sender_traits<T, true>
: awaitable_sender_traits_impl<coro::await_result_t<T>> {
  using sender_category = single_sender_tag;
};
} // namespace detail

PUSHMI_CONCEPT_DEF(
  template(typename Tp)
  concept Awaiter,
    coro::is_awaiter_v<Tp>
);

PUSHMI_CONCEPT_DEF(
  template(typename Tp)
  concept Awaitable,
    detail::AwaitableLike_<Tp> &&
    SingleTypedSender<Tp>
);

PUSHMI_CONCEPT_DEF(
  template(typename Tp, typename Result)
  concept AwaitableOf,
    Awaitable<Tp> && ConvertibleTo<coro::await_result_t<Tp>, Result>
);

#else // if FOLLY_HAS_COROUTINES
namespace detail {
template <class>
using awaitable_sender_traits = awaitable_senders::sender_adl_hook;
} // namespace detail
#endif // if FOLLY_HAS_COROUTINES

namespace detail {
template<class S>
struct basic_typed_sender_traits : awaitable_senders::sender_adl_hook {
  template<template<class...> class Tuple, template<class...> class Variant>
  using value_types = typename S::template value_types<Tuple, Variant>;

  template<template<class...> class Variant>
  using error_type = typename S::template error_type<Variant>;
};

template<class S, bool = SenderLike_<S>>
struct basic_sender_traits : awaitable_sender_traits<S> {
};

template<class S>
struct basic_sender_traits<S, true>
: std::conditional_t<
    TypedSenderLike_<S>,
    basic_typed_sender_traits<S>,
    awaitable_sender_traits<S>> {
  using sender_category = typename S::sender_category;
};
} // namespace detail

template<typename S, typename>
struct sender_traits
  : std::conditional_t<
      std::is_same<std::decay_t<S>, S>::value,
      detail::basic_sender_traits<S>,
      sender_traits<std::decay_t<S>>> {
  using _not_specialized = void;
};

} // namespace pushmi
} // namespace folly
