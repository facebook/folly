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

#include <folly/experimental/pushmi/extension_points.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/properties.h>

namespace folly {
namespace pushmi {

// traits & tags

// cardinality affects both sender and receiver

struct cardinality_category {};

// Trait
template <class PS>
struct has_cardinality : category_query<PS, cardinality_category> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool has_cardinality_v = has_cardinality<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Cardinality, //
    has_cardinality_v<PS>);

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// for executors
// time and constrained are mutually exclusive refinements of executor (time is
// a special case of constrained and may be folded in later)

struct executor_category {};

// blocking affects senders

struct blocking_category {};

// sequence affects senders

struct sequence_category {};

// Single trait and tag
template <class... TN>
struct is_single;
// Tag
template <>
struct is_single<> {
  using property_category = cardinality_category;
};
// Trait
template <class PS>
struct is_single<PS> : property_query<PS, is_single<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_single_v = is_single<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Single, //
    is_single_v<PS>);

// Many trait and tag
template <class... TN>
struct is_many;
// Tag
template <>
struct is_many<> {
  using property_category = cardinality_category;
}; // many::value() does not terminate, so it is not a refinement of single
// Trait
template <class PS>
struct is_many<PS> : property_query<PS, is_many<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_many_v = is_many<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Many, //
    is_many_v<PS>);

// Flow trait and tag
template <class... TN>
struct is_flow;
// Tag
template <>
struct is_flow<> {
  using property_category = flow_category;
};
// Trait
template <class PS>
struct is_flow<PS> : property_query<PS, is_flow<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_flow_v = is_flow<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Flow, //
    is_flow_v<PS>);

// Receiver trait and tag
template <class... TN>
struct is_receiver;
// Tag
template <>
struct is_receiver<> {
  using property_category = receiver_category;
};
// Trait
template <class PS>
struct is_receiver<PS> : property_query<PS, is_receiver<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_receiver_v = is_receiver<PS>::value;

// Sender trait and tag
template <class... TN>
struct is_sender;
// Tag
template <>
struct is_sender<> {
  using property_category = sender_category;
};
// Trait
template <class PS>
struct is_sender<PS> : property_query<PS, is_sender<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_sender_v = is_sender<PS>::value;

// Executor trait and tag
template <class... TN>
struct is_executor;
// Tag
template <>
struct is_executor<> {
  using property_category = executor_category;
};
// Trait
template <class PS>
struct is_executor<PS> : property_query<PS, is_executor<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_executor_v = is_executor<PS>::value;

// Constrained trait and tag
template <class... TN>
struct is_constrained;
// Tag
template <>
struct is_constrained<> : is_executor<> {};
// Trait
template <class PS>
struct is_constrained<PS> : property_query<PS, is_constrained<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_constrained_v = is_constrained<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Constrained, //
    is_constrained_v<PS>&& is_executor_v<PS>);

// Time trait and tag
template <class... TN>
struct is_time;
// Tag
template <>
struct is_time<> : is_constrained<> {};
// Trait
template <class PS>
struct is_time<PS> : property_query<PS, is_time<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_time_v = is_time<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept Time, //
    is_time_v<PS>&& is_constrained_v<PS>&& is_executor_v<PS>);

// AlwaysBlocking trait and tag
template <class... TN>
struct is_always_blocking;
// Tag
template <>
struct is_always_blocking<> {
  using property_category = blocking_category;
};
// Trait
template <class PS>
struct is_always_blocking<PS> : property_query<PS, is_always_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_always_blocking_v =
    is_always_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept AlwaysBlocking, //
    is_always_blocking_v<PS>&& is_sender_v<PS>);

// NeverBlocking trait and tag
template <class... TN>
struct is_never_blocking;
// Tag
template <>
struct is_never_blocking<> {
  using property_category = blocking_category;
};
// Trait
template <class PS>
struct is_never_blocking<PS> : property_query<PS, is_never_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_never_blocking_v =
    is_never_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept NeverBlocking, //
    is_never_blocking_v<PS>&& is_sender_v<PS>);

// MaybeBlocking trait and tag
template <class... TN>
struct is_maybe_blocking;
// Tag
template <>
struct is_maybe_blocking<> {
  using property_category = blocking_category;
};
// Trait
template <class PS>
struct is_maybe_blocking<PS> : property_query<PS, is_maybe_blocking<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_maybe_blocking_v =
    is_maybe_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept MaybeBlocking, //
    is_maybe_blocking_v<PS>&& is_sender_v<PS>);

// FifoSequence trait and tag
template <class... TN>
struct is_fifo_sequence;
// Tag
template <>
struct is_fifo_sequence<> {
  using property_category = sequence_category;
};
// Trait
template <class PS>
struct is_fifo_sequence<PS> : property_query<PS, is_fifo_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_fifo_sequence_v =
    is_fifo_sequence<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept FifoSequence, //
    is_fifo_sequence_v<PS>&& is_executor_v<PS>);

// ConcurrentSequence trait and tag
template <class... TN>
struct is_concurrent_sequence;
// Tag
template <>
struct is_concurrent_sequence<> {
  using property_category = sequence_category;
};
// Trait
template <class PS>
struct is_concurrent_sequence<PS>
    : property_query<PS, is_concurrent_sequence<>> {};
template <class PS>
PUSHMI_INLINE_VAR constexpr bool is_concurrent_sequence_v =
    is_concurrent_sequence<PS>::value;
PUSHMI_CONCEPT_DEF(
    template(class PS) //
    concept ConcurrentSequence, //
    is_concurrent_sequence_v<PS>&& is_executor_v<PS>);

// concepts to support execution
//

PUSHMI_CONCEPT_DEF(
    template(class Exec, class... PropertyN) //
    (concept Executor)(Exec, PropertyN...), //
    requires(Exec& exec) //
        (schedule(exec), requires_<is_sender_v<decltype(schedule(exec))>>) &&
        SemiMovable<std::decay_t<Exec>> && property_query_v<Exec, PropertyN...> &&
        is_executor_v<Exec>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
    Executor<Exec>,
    sender_t =,
    decltype(schedule(std::declval<Exec&>())));

PUSHMI_CONCEPT_DEF(
    template(class Exec, class... PropertyN) //
    (concept ExecutorProvider)(Exec, PropertyN...), //
    requires(Exec& exec) //
        (get_executor(exec),
         requires_<Executor<decltype(get_executor(exec))>>) &&
        SemiMovable<std::decay_t<Exec>> && property_query_v<Exec, PropertyN...>);

PUSHMI_CONCEPT_DEF(
    template(class Exec, class... PropertyN) //
    (concept Strand)(Exec, PropertyN...), //
    Executor<Exec>&& property_query_v<Exec, is_fifo_sequence<>, PropertyN...>);

PUSHMI_CONCEPT_DEF(
    template(class Exec) //
    (concept StrandFactory)(Exec), //
    requires(Exec& exec) //
        (make_strand(exec), requires_<Strand<decltype(make_strand(exec))>>) &&
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
//

PUSHMI_CONCEPT_DEF(
    template(class Exec, class... PropertyN) //
    (concept ConstrainedExecutor)(Exec, PropertyN...), //
    requires(Exec& exec) //
        (top(exec),
         requires_<Regular<decltype(top(exec))>>,
         schedule(exec, top(exec)),
         requires_<is_sender_v<decltype(schedule(exec, top(exec)))>>) &&
        Executor<Exec, is_constrained<>, PropertyN...>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
    ConstrainedExecutor<Exec>,
    constraint_t =,
    decltype(top(std::declval<Exec&>())));

PUSHMI_CONCEPT_DEF(
    template(class Exec, class... PropertyN) //
    (concept TimeExecutor)(Exec, PropertyN...), //
    requires(Exec& exec) //
        (now(exec),
         schedule(exec, now(exec)),
         requires_<Regular<decltype(now(exec) + std::chrono::seconds(1))>>) &&
        ConstrainedExecutor<Exec, is_time<>, PropertyN...>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
    TimeExecutor<Exec>,
    time_point_t =,
    decltype(now(std::declval<Exec&>())));

// add concepts to support receivers
//

PUSHMI_CONCEPT_DEF(
    template(class R, class... PropertyN) //
    (concept Receiver)(R, PropertyN...), //
    requires(R& r) //
        (set_done(r)) &&
        SemiMovable<std::decay_t<R>> &&
        property_query_v<R, PropertyN...> &&
        is_receiver_v<R> && !is_sender_v<R>
      );

PUSHMI_CONCEPT_DEF(
    template(class R, class... VN) //
    (concept ReceiveValue)(R, VN...), //
    requires(R& r) //
        (set_value(r, std::declval<VN&&>()...)) &&
        Receiver<R>&& And<MoveConstructible<VN>...>);

PUSHMI_CONCEPT_DEF(
    template(class R, class E = std::exception_ptr) //
    (concept ReceiveError)(R, E), //
    requires(R& r, E&& e)( //
        set_error(r, (E &&) e)) &&
        Receiver<R> && MoveConstructible<E>);

// add concepts to support senders
//

PUSHMI_CONCEPT_DEF(
    template(class S, class... PropertyN) //
    (concept SenderImpl)(S, PropertyN...), //
    SemiMovable<S>&& Cardinality<S>&& property_query_v<S, PropertyN...>&&
            is_sender_v<S> &&
        !is_receiver_v<S>);

PUSHMI_CONCEPT_DEF(
    template(class S, class... PropertyN) //
    (concept Sender)(S, PropertyN...), //
    SenderImpl<std::decay_t<S>, PropertyN...>);

PUSHMI_CONCEPT_DEF(
    template(class S, class R, class... PropertyN) //
    (concept SenderTo)(S, R, PropertyN...), //
    requires(S&& s, R&& r) //
        (submit((S &&) s, (R &&) r)) &&
        Sender<S, PropertyN...> && Receiver<R>);

template <class S>
PUSHMI_PP_CONSTRAINED_USING(
    Sender<S>,
    executor_t =,
    decltype(get_executor(std::declval<S&>())));

// add concepts to support cancellation and rate control
//

PUSHMI_CONCEPT_DEF(
    template(class R, class... PropertyN) //
    (concept FlowReceiver)(R, PropertyN...), //
    Receiver<R>&& property_query_v<R, PropertyN...>&& Flow<R>);

PUSHMI_CONCEPT_DEF(
    template(class R, class... VN) //
    (concept FlowReceiveValue)(R, VN...), //
    Flow<R>&& ReceiveValue<R, VN...>);

PUSHMI_CONCEPT_DEF(
    template(class R, class E = std::exception_ptr) //
    (concept FlowReceiveError)(R, E), //
    Flow<R>&& ReceiveError<R, E>);

PUSHMI_CONCEPT_DEF(
    template(class R, class Up) //
    (concept FlowUpTo)(R, Up), //
    requires(R& r, Up&& up) //
        (set_starting(r, (Up &&) up)) &&
        Flow<R>);

PUSHMI_CONCEPT_DEF(
    template(class S, class... PropertyN) //
    (concept FlowSender)(S, PropertyN...), //
    Sender<S>&& property_query_v<S, PropertyN...>&& Flow<S>);

PUSHMI_CONCEPT_DEF(
    template(class S, class R, class... PropertyN) //
    (concept FlowSenderTo)(S, R, PropertyN...), //
    FlowSender<S>&& SenderTo<S, R>&& property_query_v<S, PropertyN...>&&
        FlowReceiver<R>);

} // namespace pushmi
} // namespace folly
