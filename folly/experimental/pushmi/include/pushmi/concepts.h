// clang-format off
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "forwards.h"
#include "extension_points.h"
#include "properties.h"

namespace pushmi {

// traits & tags

// cardinality affects both sender and receiver

struct cardinality_category {};

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// time and constrained are mutually exclusive refinements of sender (time is a special case of constrained and may be folded in later)


// Silent trait and tag
template<class... TN>
struct is_silent;
// Tag
template<>
struct is_silent<> { using property_category = cardinality_category; };
// Trait
template<class PS>
struct is_silent<PS> : property_query<PS, is_silent<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_silent_v = is_silent<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Silent,
    is_silent_v<PS>
);

// None trait and tag
template<class... TN>
struct is_none;
// Tag
template<>
struct is_none<> : is_silent<> {};
// Trait
template<class PS>
struct is_none<PS> : property_query<PS, is_none<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_none_v = is_none<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept None,
    Silent<PS> && is_none_v<PS>
);

// Single trait and tag
template<class... TN>
struct is_single;
// Tag
template<>
struct is_single<> : is_none<> {};
// Trait
template<class PS>
struct is_single<PS> : property_query<PS, is_single<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_single_v = is_single<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Single,
    None<PS> && is_single_v<PS>
);

// Many trait and tag
template<class... TN>
struct is_many;
// Tag
template<>
struct is_many<> : is_none<> {}; // many::value() does not terminate, so it is not a refinement of single
// Trait
template<class PS>
struct is_many<PS> : property_query<PS, is_many<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_many_v = is_many<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Many,
    None<PS> && is_many_v<PS>
);

// Flow trait and tag
template<class... TN>
struct is_flow;
// Tag
template<>
struct is_flow<> { using property_category = flow_category; };
// Trait
template<class PS>
struct is_flow<PS> : property_query<PS, is_flow<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_flow_v = is_flow<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Flow,
    is_flow_v<PS>
);

// Receiver trait and tag
template<class... TN>
struct is_receiver;
// Tag
template<>
struct is_receiver<> { using property_category = receiver_category; };
// Trait
template<class PS>
struct is_receiver<PS> : property_query<PS, is_receiver<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_receiver_v = is_receiver<PS>::value;
// PUSHMI_CONCEPT_DEF(
//   template (class PS)
//   concept Receiver,
//     is_receiver_v<PS>
// );

// Sender trait and tag
template<class... TN>
struct is_sender;
// Tag
template<>
struct is_sender<> { using property_category = sender_category; };
// Trait
template<class PS>
struct is_sender<PS> : property_query<PS, is_sender<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_sender_v = is_sender<PS>::value;
// PUSHMI_CONCEPT_DEF(
//   template (class PS)
//   concept Sender,
//     is_sender_v<PS>
// );

// Time trait and tag
template<class... TN>
struct is_time;
// Tag
template<>
struct is_time<> : is_sender<> {};
// Trait
template<class PS>
struct is_time<PS> : property_query<PS, is_time<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_time_v = is_time<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Time,
    is_time_v<PS>
);

// Constrained trait and tag
template<class... TN>
struct is_constrained;
// Tag
template<>
struct is_constrained<> : is_sender<> {};
// Trait
template<class PS>
struct is_constrained<PS> : property_query<PS, is_constrained<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_constrained_v = is_constrained<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Constrained,
    is_constrained_v<PS>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept Receiver)(S, PropertyN...),
    requires (S& s) (
      ::pushmi::set_done(s)
    ) &&
    SemiMovable<S> &&
    property_query_v<S, PropertyN...> &&
    is_receiver_v<S>
);

PUSHMI_CONCEPT_DEF(
  template (class N, class E = std::exception_ptr)
  (concept NoneReceiver)(N, E),
    requires(N& n, E&& e) (
      ::pushmi::set_error(n, (E &&) e)
    ) &&
    Receiver<N> &&
    None<N> &&
    SemiMovable<E>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class T, class E = std::exception_ptr)
  (concept SingleReceiver)(S, T, E),
    requires(S& s, T&& t) (
      ::pushmi::set_value(s, (T &&) t) // Semantics: called exactly once.
    ) &&
    NoneReceiver<S, E> &&
    SemiMovable<T> &&
    SemiMovable<E> &&
    Single<S>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class T, class E = std::exception_ptr)
  (concept ManyReceiver)(S, T, E),
    requires(S& s, T&& t) (
      ::pushmi::set_next(s, (T &&) t) // Semantics: called 0-N times.
    ) &&
    NoneReceiver<S, E> &&
    SemiMovable<T> &&
    SemiMovable<E> &&
    Many<S>
);


// silent does not really make sense, but cannot test for
// None without the error type, use is_none<> to strengthen
// requirements
PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept Sender)(D, PropertyN...),
    SemiMovable<D> &&
    None<D> &&
    property_query_v<D, PropertyN...> &&
    is_sender_v<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept SenderTo)(D, S, PropertyN...),
    requires(D& d, S&& s) (
      ::pushmi::submit(d, (S &&) s)
    ) &&
    Sender<D> &&
    Receiver<S> &&
    property_query_v<D, PropertyN...>
);

// add concepts to support cancellation
//

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept FlowReceiver)(S, PropertyN...),
    Receiver<S> &&
    property_query_v<S, PropertyN...> &&
    Flow<S>
);

PUSHMI_CONCEPT_DEF(
  template (
    class N,
    class Up,
    class PE = std::exception_ptr,
    class E = PE)
  (concept FlowNoneReceiver)(N, Up, PE, E),
    requires(N& n, Up&& up) (
      ::pushmi::set_starting(n, (Up &&) up)
    ) &&
    FlowReceiver<N> &&
    Receiver<Up> &&
    SemiMovable<PE> &&
    SemiMovable<E> &&
    NoneReceiver<Up, PE> &&
    NoneReceiver<N, E>
);

PUSHMI_CONCEPT_DEF(
  template (
      class S,
      class Up,
      class T,
      class PE = std::exception_ptr,
      class E = PE)
  (concept FlowSingleReceiver)(S, Up, T, PE, E),
    SingleReceiver<S, T, E> &&
    FlowNoneReceiver<S, Up, PE, E>
);

PUSHMI_CONCEPT_DEF(
  template (
      class S,
      class Up,
      class T,
      class PT = std::ptrdiff_t,
      class PE = std::exception_ptr,
      class E = PE)
  (concept FlowManyReceiver)(S, Up, T, PT, PE, E),
    ManyReceiver<S, T, E> &&
    ManyReceiver<Up, PT, PE> &&
    FlowNoneReceiver<S, Up, PE, E>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept FlowSender)(S, PropertyN...),
    Sender<S> &&
    property_query_v<S, PropertyN...> &&
    Flow<S>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept FlowSenderTo)(D, S, PropertyN...),
    FlowSender<D> &&
    property_query_v<D, PropertyN...> &&
    FlowReceiver<S>
);

// add concepts for constraints
//

PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept TimeSender)(D, PropertyN...),
    requires(D& d) (
      ::pushmi::now(d),
      requires_<Regular<decltype(::pushmi::now(d))>>
    ) &&
    Sender<D> &&
    property_query_v<D, PropertyN...> &&
    Time<D> &&
    None<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept TimeSenderTo)(D, S, PropertyN...),
    requires(D& d, S&& s) (
      ::pushmi::submit(d, ::pushmi::now(d), (S &&) s)
    ) &&
    TimeSender<D> &&
    property_query_v<D, PropertyN...> &&
    Receiver<S>
);

template <class D>
PUSHMI_PP_CONSTRAINED_USING(
  TimeSender<D>,
  time_point_t =, decltype(::pushmi::now(std::declval<D&>())));

// this is a more general form where the constraint could be time or priority
// enum or any other ordering constraint value-type.
//
// top() returns the constraint value that will cause the item to run asap.
// So now() for time and NORMAL for priority.
//
// I would like to replace Time.. with Constrained.. but not sure if it will
// obscure too much.

PUSHMI_CONCEPT_DEF(
  template (class D)
  concept ConstrainedSender,
    requires(D& d) (
      ::pushmi::top(d),
      requires_<Regular<decltype(::pushmi::top(d))>>
    ) &&
    Sender<D> &&
    Constrained<D> &&
    None<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S)
  concept ConstrainedSenderTo,
    requires(D& d, S&& s) (
      ::pushmi::submit(d, ::pushmi::top(d), (S &&) s)
    ) &&
    ConstrainedSender<D> &&
    Receiver<S>
);

template <class D>
PUSHMI_PP_CONSTRAINED_USING(
  ConstrainedSender<D>,
  constraint_t =, decltype(::pushmi::top(std::declval<D&>())));

} // namespace pushmi
