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

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// blocking affects senders

struct blocking_category {};

// sequence affects senders

struct sequence_category {};

// is_single trait and tag
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

// is_many trait and tag
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

// is_flow trait and tag
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
    template(class R, class E = std::exception_ptr) //
    (concept ReceiveError)(R, E), //
    requires(R& r, E&& e)( //
        set_error(r, (E &&) e)) &&
        Receiver<R> && MoveConstructible<E>
);

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

// add concepts to support senders
//

PUSHMI_CONCEPT_DEF(
    template(class S) //
    (concept SenderImpl)(S), //
    SemiMovable<S>&& has_cardinality_v<S>&&
    is_sender_v<S>);

PUSHMI_CONCEPT_DEF(
    template(class S) //
    (concept Sender)(S), //
    SenderImpl<std::decay_t<S>>);

PUSHMI_CONCEPT_DEF(
    template(class S, class R) //
    (concept SenderTo)(S, R), //
    requires(S&& s, R&& r) //
        (submit((S &&) s, (R &&) r)) &&
        Sender<S> && Receiver<R>);

// is_always_blocking trait and tag
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

// is_never_blocking trait and tag
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

// is_maybe_blocking trait and tag
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

// is_fifo_sequence trait and tag
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

// is_concurrent_sequence trait and tag
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

// concepts to support execution
//

PUSHMI_CONCEPT_DEF(
    template(class Exec) //
    (concept Executor)(Exec), //
    requires(Exec& exec)( //
        schedule(exec),
        requires_<Sender<decltype(schedule(exec))>>) &&
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
        make_strand(exec), requires_<Strand<decltype(make_strand(exec))>>) &&
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
        requires_<Sender<decltype(schedule(exec, top(exec)))>>) &&
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
        requires_<Sender<decltype(exec.schedule(tp + d))>>,
        requires_<Sender<decltype(exec.schedule(d + tp))>>,
        requires_<Sender<decltype(exec.schedule(tp - d))>>,
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
        requires_<Sender<decltype(schedule(exec, now(exec)))>>) &&
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
    is_flow_v<S>);

PUSHMI_CONCEPT_DEF(
    template(class S, class R) //
    (concept FlowSenderTo)(S, R), //
    FlowSender<S>&& SenderTo<S, R>&&
    FlowReceiver<R>);

} // namespace pushmi
} // namespace folly
