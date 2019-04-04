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

#include <folly/experimental/pushmi/extension_points.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/properties.h>
#include <folly/experimental/pushmi/tags.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

// traits & tags
struct sender_tag;
struct single_sender_tag;
struct flow_sender_tag;
struct flow_single_sender_tag;

// add concepts to support receivers
//

/// \cond
namespace detail {
template <bool>
struct Enable_ {};
template <>
struct Enable_<true> {
  template <class T>
  using _type = T;
};

#if defined(__cpp_fold_expressions) && __cpp_fold_expressions > 0
// An alias for the type in the pack if the pack has exactly one type in it.
// Otherwise, this SFINAE's away. T cannot be an array type of an abstract type.
template<class... Ts>
using identity_t = typename Enable_<sizeof...(Ts) == 1u>::
  template _type<decltype((((Ts(*)())0)(),...))>;
#else
template <class...>
struct Front_ {};
template <class T, class... Us>
struct Front_<T, Us...> {
  using type = T;
};
// Instantiation proceeds from left to right. The use of Enable_ here avoids
// needless instantiations of Front_.
template<class...Ts>
using identity_t = typename Enable_<sizeof...(Ts) == 1u>::
  template _type<Front_<Ts...>>::type;
#endif
} // namespace detail

// is_flow trait
template <class PS>
struct is_flow<PS> : property_query<PS, is_flow<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_flow_v = is_flow<PS>::value;

// is_receiver trait
template <class PS>
struct is_receiver<PS> : property_query<PS, is_receiver<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_receiver_v = is_receiver<PS>::value;

// add concepts to support receivers
//

PUSHMI_CONCEPT_DEF(
    template(class R) //
    (concept Receiver)(R), //
    requires(R& r) //
        (set_done(r)) &&
        SemiMovable<std::decay_t<R>> &&
        is_receiver_v<R>);

PUSHMI_CONCEPT_DEF(
    template(class R, class... VN) //
    (concept ReceiveValue)(R, VN...), //
    requires(R& r) //
        (set_value(r, std::declval<VN&&>()...)) &&
        Receiver<R>&& And<MoveConstructible<VN>...>);

PUSHMI_CONCEPT_DEF(
    template(class R, class E) //
    (concept ReceiveError)(R, E), //
    requires(R& r, E&& e)( //
        set_error(r, (E &&) e)) &&
        Receiver<R> && MoveConstructible<E>
);

// add concepts to support senders
//

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
  template<class, class = void>
  struct basic_sender_traits {
  };
  template<class S>
  struct basic_typed_sender_traits : awaitable_senders::sender_adl_hook {
    template<template<class...> class Tuple, template<class...> class Variant>
    using value_types = typename S::template value_types<Tuple, Variant>;

    template<template<class...> class Variant>
    using error_type = typename S::template error_type<Variant>;
  };
  template<class S>
  struct basic_sender_traits<S, std::enable_if_t<SenderLike_<S>>>
  : std::conditional_t<
      TypedSenderLike_<S>,
      basic_typed_sender_traits<S>,
      awaitable_senders::sender_adl_hook> {
    using sender_category = typename S::sender_category;
  };
} // namespace detail

template<typename S>
struct sender_traits
  : std::conditional_t<
      std::is_same<std::decay_t<S>, S>::value,
      detail::basic_sender_traits<S>,
      sender_traits<std::decay_t<S>>> {
  using _not_specialized = void;
};

// A Sender is, by default, a "many" sender, in that it may call
// set_value on its Receiver multiple times before calling set_done.
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

// A single sender is a special kind of sender that promises to only
// call set_value once (or not at all) before calling set_done.
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
//         std::is_void_v<result_t<Args...>>,
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
// struct transformed_sender_of : sender_base<sender_category_t<From>> {
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

PUSHMI_CONCEPT_DEF(
    template(class S, class R) //
    (concept SenderTo)(S, R), //
    requires(S&& s, R&& r) //
        (submit((S &&) s, (R &&) r)) &&
        Sender<S> && Receiver<R>);

// is_always_blocking trait and tag
template <class PS>
struct is_always_blocking<PS> : property_query<PS, is_always_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_always_blocking_v =
    is_always_blocking<PS>::value;

// is_never_blocking trait
template <class PS>
struct is_never_blocking<PS> : property_query<PS, is_never_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_never_blocking_v =
    is_never_blocking<PS>::value;

// is_maybe_blocking trait
template <class PS>
struct is_maybe_blocking<PS> : property_query<PS, is_maybe_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_maybe_blocking_v =
    is_maybe_blocking<PS>::value;

// is_fifo_sequence trait
template <class PS>
struct is_fifo_sequence<PS> : property_query<PS, is_fifo_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_fifo_sequence_v =
    is_fifo_sequence<PS>::value;

// is_concurrent_sequence trait
template <class PS>
struct is_concurrent_sequence<PS>
    : property_query<PS, is_concurrent_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_concurrent_sequence_v =
    is_concurrent_sequence<PS>::value;

// concepts to support execution
//

PUSHMI_CONCEPT_DEF(
    template(class Exec) //
    (concept Executor)(Exec), //
    requires(Exec& exec)( //
        schedule(exec),
        requires_<SingleSender<decltype(schedule(exec))>>) &&
        SemiMovable<std::decay_t<Exec>>);

template <class Exec, class... Args>
PUSHMI_PP_CONSTRAINED_USING(
  Executor<Exec>,
  sender_t =,
    decltype(schedule(std::declval<Exec&>(), std::declval<Args>()...)));

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept ExecutorProvider)(Exec), //
  requires(Exec& exec)( //
    get_executor(exec),
    requires_<Executor<decltype(get_executor(exec))>>) &&
    SemiMovable<std::decay_t<Exec>>);

template <class S>
PUSHMI_PP_CONSTRAINED_USING(
  ExecutorProvider<S>,
  executor_t =,
    decltype(get_executor(std::declval<S&>())));

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept Strand)(Exec), //
    Executor<Exec>&&
    is_fifo_sequence_v<Exec>);

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept StrandFactory)(Exec), //
    requires(Exec& exec)( //
        make_strand(exec), //
        requires_<Strand<decltype(make_strand(exec))>>) &&
        SemiMovable<std::decay_t<Exec>>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  StrandFactory<Exec>,
  strand_t =,
    decltype(make_strand(std::declval<Exec&>())));

// add concepts for execution constraints
//
// the constraint could be time or priority enum or any other
// ordering constraint value-type.
//
// top() returns the constraint value that will cause the item to run asap.
// So now() for time and NORMAL for priority.

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept ConstrainedExecutor)(Exec), //
  requires(Exec& exec)( //
    top(exec),
    requires_<Regular<decltype(top(exec))>>,
    schedule(exec, top(exec)),
    requires_<SingleSender<decltype(schedule(exec, top(exec)))>>) &&
    Executor<Exec>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  ConstrainedExecutor<Exec>,
  constraint_t =,
    decltype(top(std::declval<Exec&>())));

PUSHMI_CONCEPT_DEF(
  template(class Exec, class TP, class Duration) //
  concept TimeExecutorImpl2_, //
    requires(Exec& exec, TP tp, Duration d)( //
        requires_<SingleSender<decltype(exec.schedule(tp + d))>>,
        requires_<SingleSender<decltype(exec.schedule(d + tp))>>,
        requires_<SingleSender<decltype(exec.schedule(tp - d))>>,
        tp += d,
        tp -= d));

PUSHMI_CONCEPT_DEF(
  template(class Exec, class TP = decltype(now(std::declval<Exec&>()))) //
  (concept TimeExecutorImpl_)(Exec, TP), //
      Regular<TP> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::nanoseconds> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::microseconds> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::milliseconds> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::seconds> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::minutes> &&
      TimeExecutorImpl2_<Exec, TP, std::chrono::hours> &&
      TimeExecutorImpl2_<Exec, TP, decltype(TP{} - TP{})>);

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept TimeExecutor)(Exec), //
    requires(Exec& exec)( //
        now(exec),
        schedule(exec, now(exec)),
        requires_<SingleSender<decltype(schedule(exec, now(exec)))>>) &&
        ConstrainedExecutor<Exec> &&
        TimeExecutorImpl_<Exec>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  TimeExecutor<Exec>,
  time_point_t =,
    decltype(now(std::declval<Exec&>())));

// add concepts to support cancellation and rate control
//

PUSHMI_CONCEPT_DEF(
  template(class R) //
  (concept FlowReceiver)(R), //
    Receiver<R>&&
    is_flow_v<R>);

PUSHMI_CONCEPT_DEF(
  template(class R, class... VN) //
  (concept FlowReceiveValue)(R, VN...), //
    is_flow_v<R>&& ReceiveValue<R, VN...>);

PUSHMI_CONCEPT_DEF(
  template(class R, class E = std::exception_ptr) //
  (concept FlowReceiveError)(R, E), //
    is_flow_v<R>&& ReceiveError<R, E>);

PUSHMI_CONCEPT_DEF(
  template(class R, class Up) //
  (concept FlowUpTo)(R, Up), //
    requires(R& r, Up&& up)( //
        set_starting(r, (Up &&) up)) &&
        is_flow_v<R>);

PUSHMI_CONCEPT_DEF(
  template(class S) //
  (concept FlowSender)(S), //
    Sender<S>&&
    DerivedFrom<sender_category_t<S>, flow_sender_tag>);

PUSHMI_CONCEPT_DEF(
  template(class S, class R) //
  (concept FlowSenderTo)(S, R), //
    FlowSender<S>&& SenderTo<S, R>&&
    FlowReceiver<R>);

} // namespace pushmi
} // namespace folly
