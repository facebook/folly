// clang-format off
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "forwards.h"
#include "extension_points.h"

namespace pushmi {

// tag types
struct silent_tag {};
struct none_tag : silent_tag {};
struct single_tag : none_tag {};
struct flow_tag : single_tag {};

template <class Tag>
concept bool Silent = Derived<Tag, silent_tag>;

template <class Tag>
concept bool None = Silent<Tag> && Derived<Tag, none_tag>;

template <class Tag>
concept bool Single = None<Tag> && Derived<Tag, single_tag>;

template <class Tag>
concept bool Flow = Single<Tag> && Derived<Tag, flow_tag>;




template <class T>
using __sender_category_t = typename T::sender_category;

template <class T>
struct sender_traits : sender_traits<std::decay_t<T>> {
};
template <Decayed T>
struct sender_traits<T> {
};
template <Decayed T>
  requires Valid<T, __sender_category_t>
struct sender_traits<T> {
  using sender_category = __sender_category_t<T>;
};

template <class T>
using sender_category_t = __sender_category_t<sender_traits<T>>;

template <class T>
using __receiver_category_t = typename T::receiver_category;

template <class T>
struct receiver_traits : receiver_traits<std::decay_t<T>> {
};
template <Decayed T>
struct receiver_traits<T> {
};
template <Decayed T>
  requires Valid<T, __receiver_category_t>
struct receiver_traits<T> {
  using receiver_category = __receiver_category_t<T>;
};

template <class T>
using receiver_category_t = __receiver_category_t<receiver_traits<T>>;





template <class S, class Tag = silent_tag>
concept bool Receiver = Valid<receiver_traits<S>, __receiver_category_t> &&
  Derived<receiver_category_t<S>, Tag> &&
  SemiMovable<S> && requires (S& s) {
    ::pushmi::set_done(s);
  };

template <class S, class E = std::exception_ptr>
concept bool NoneReceiver = Receiver<S> &&
  Derived<receiver_category_t<S>, none_tag> &&
  requires(S& s, E&& e) {
    ::pushmi::set_error(s, (E &&) e);
  };

template <class S, class T, class E = std::exception_ptr>
concept bool SingleReceiver = NoneReceiver<S, E> &&
  Derived<receiver_category_t<S>, single_tag> &&
  requires(S& s, T&& t) {
    ::pushmi::set_value(s, (T &&) t); // Semantics: called exactly once.
  };







template <class D, class Tag = silent_tag>
concept bool Sender = Valid<sender_traits<D>, __sender_category_t> &&
  Derived<sender_category_t<D>, Tag> && SemiMovable<D>;

template <class D, class S, class Tag = silent_tag>
concept bool SenderTo = Sender<D, Tag> &&
  Derived<sender_category_t<D>, Tag> &&
  Receiver<S, Tag> && requires(D& d, S&& s) {
    ::pushmi::submit(d, (S &&) s);
  };






template <class D, class Tag = silent_tag>
concept bool TimeSender = Sender<D, Tag> && requires(D& d) {
  { ::pushmi::now(d) } -> Regular
};

template <class D, class S, class Tag = silent_tag>
concept bool TimeSenderTo = Receiver<S, Tag> && TimeSender<D, Tag> &&
  requires(D& d, S&& s) {
    ::pushmi::submit(d, ::pushmi::now(d), (S &&) s);
  };

template <TimeSender D>
using time_point_t = decltype(::pushmi::now(std::declval<D&>()));


// // this is a more general form where C (Constraint) could be time or priority
// // enum or any other ordering constraint value-type.
// //
// // top() returns the constraint value that will push the item as high in the
// // queue as currently possible. So now() for time and HIGH for priority.
// //
// // I would like to replace Time.. with Priority.. but not sure if it will
// // obscure too much.
// template <class D>
// concept bool PrioritySource = requires(D& d) {
//   { ::pushmi::top(d) } -> Regular
// };
//
// template <PrioritySource D>
// using constraint_t = decltype(::pushmi::top(std::declval<D&>()));
//
// template <class D, class S>
// concept bool SemiPrioritySender = requires(D& d, S&& s) {
//   ::pushmi::submit(d, ::pushmi::top(d), (S &&) s);
// };
//
// template <class D, class S, class E = std::exception_ptr>
// concept bool PrioritySender =
//     NoneReceiver<S, E> && SemiPrioritySender<D, S> &&
//     PrioritySource<D> && requires(D& d, S& s) {
//   { ::pushmi::top(d) } -> constraint_t<D>;
// };
//
// template <class D, class S, class T, class E = std::exception_ptr>
// concept bool PrioritySingleSender = SingleReceiver<S, T, E> &&
//   PrioritySender<D, S, E>;
//
// template <class D, class S>
// concept bool SemiPrioritySingleSender = SemiPrioritySender<D, S>;

// add concepts to support cancellation
//

template <class N, class Up, class PE = std::exception_ptr>
concept bool FlowNone = NoneReceiver<Up, PE> && requires(N& n, Up& up) {
  ::pushmi::set_stopping(n);
  ::pushmi::set_starting(n, up);
};

template <
    class D,
    class N,
    class Up,
    class PE = std::exception_ptr,
    class E = PE>
concept bool FlowNoneSender = FlowNone<N, Up, PE> &&
  SenderTo<D, N> && NoneReceiver<N, E>;

template <
    class S,
    class Up,
    class T,
    class PE = std::exception_ptr,
    class E = PE>
concept bool FlowSingle = SingleReceiver<S, T, E> && FlowNone<S, Up, PE>;

template <
    class D,
    class S,
    class Up,
    class T,
    class PE = std::exception_ptr,
    class E = PE>
concept bool FlowSingleSender =
    FlowSingle<S, Up, T, PE, E> && FlowNoneSender<D, S, Up, PE, E>;

template <class D, class S>
concept bool SemiFlowSingleSender = SenderTo<D, S, single_tag>;

} // namespace pushmi
