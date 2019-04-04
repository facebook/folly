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
#include <folly/experimental/pushmi/detail/sender_concepts.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

// add concepts to support receivers
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

PUSHMI_CONCEPT_DEF(
  template(class R) //
  (concept Receiver)(R), //
    requires(R& r)( //
      set_done(r) //
    ) && //
    SemiMovable<std::decay_t<R>> &&
    True<typename receiver_traits<R>::receiver_category> &&
    DerivedFrom<typename receiver_traits<R>::receiver_category, receiver_tag>
);

template <class R>
PUSHMI_PP_CONSTRAINED_USING(
  Receiver<R>,
  receiver_category_t =,
    typename receiver_traits<R>::receiver_category
);

PUSHMI_CONCEPT_DEF(
  template(class R, class... VN) //
  (concept ReceiveValue)(R, VN...), //
    requires(R& r)( //
      set_value(r, std::declval<VN&&>()...) //
    ) && //
    Receiver<R> && //
    And<MoveConstructible<VN>...>
);

PUSHMI_CONCEPT_DEF(
  template(class R, class E) //
  (concept ReceiveError)(R, E), //
    requires(R& r, E&& e)( //
        set_error(r, (E &&) e) //
    ) && //
    Receiver<R> && //
    MoveConstructible<E>
);

// add concepts to support senders
//

PUSHMI_CONCEPT_DEF(
    template(class S, class R) //
    (concept SenderTo)(S, R), //
    requires(S&& s, R&& r) //
        (pushmi::submit((S &&) s, (R &&) r)) &&
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
    DerivedFrom<receiver_category_t<R>, flow_receiver_tag>);

PUSHMI_CONCEPT_DEF(
  template(class R, class... VN) //
  (concept FlowReceiveValue)(R, VN...), //
    FlowReceiver<R>&& ReceiveValue<R, VN...>);

PUSHMI_CONCEPT_DEF(
  template(class R, class E = std::exception_ptr) //
  (concept FlowReceiveError)(R, E), //
    FlowReceiver<R>&& ReceiveError<R, E>);

PUSHMI_CONCEPT_DEF(
  template(class R, class Up) //
  (concept FlowUpTo)(R, Up), //
    requires(R& r, Up&& up)( //
        set_starting(r, (Up &&) up) //
    ) &&
    FlowReceiver<R>
);

PUSHMI_CONCEPT_DEF(
  template(class S) //
  (concept FlowSender)(S), //
    Sender<S>&&
    DerivedFrom<sender_category_t<S>, flow_sender_tag>
);

PUSHMI_CONCEPT_DEF(
  template(class S, class R) //
  (concept FlowSenderTo)(S, R), //
    FlowSender<S> && //
    SenderTo<S, R>&& //
    FlowReceiver<R>
);

} // namespace pushmi
} // namespace folly

// Make all single typed senders also awaitable:
#include <folly/experimental/pushmi/detail/sender_adapter.h>
