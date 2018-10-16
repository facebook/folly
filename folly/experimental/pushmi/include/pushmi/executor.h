#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <chrono>
#include <functional>
#include "time_single_deferred.h"

namespace pushmi {

namespace detail {

template<class TP>
struct any_time_executor_ref_vtable {
  TP (*now_)(void*);
  void (*submit_)(void*, TP, void*);
};

template <class E, class TP, class Other, class Wrapped>
auto any_time_executor_ref_vtable_v() {
  static constexpr any_time_executor_ref_vtable<TP> const vtable_v {
    +[](void* pobj) { return ::pushmi::now(*static_cast<Wrapped*>(pobj)); },
    +[](void* pobj, TP tp, void* s) {
      return ::pushmi::submit(
        *static_cast<Wrapped*>(pobj),
        tp,
        std::move(*static_cast<single<Other, E>*>(s)));
    }
  };
  return &vtable_v;
};

} // namespace detail

template<class E, class TP, int i>
struct any_time_executor_ref {
private:
  // use two instances to resolve recurive type definition.
  using This = any_time_executor_ref<E, TP, i>;
  using Other = any_time_executor_ref<E, TP, i == 0 ? 1 : 0>;

  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<
      !std::is_same_v<U, This> &&
      !std::is_same_v<U, Other>, U>;
  void* pobj_;
  detail::any_time_executor_ref_vtable<TP> const *vptr_;
public:
  using sender_category = single_tag;

  any_time_executor_ref() = delete;

  template<int n>
  any_time_executor_ref(any_time_executor_ref<E, TP, n>&& o) :
    pobj_(o.pobj_), vptr_(o.vptr_) {
    o.pobj_ = nullptr;
    o.vptr_ = nullptr;
  };
  template<int n>
  any_time_executor_ref(const any_time_executor_ref<E, TP, n>& o) :
    pobj_(o.pobj_), vptr_(o.vptr_) {};

  template <class Wrapped, TimeSender<single_tag> W = wrapped_t<Wrapped>>
    // requires TimeSenderTo<W, single<Other, E>>
  any_time_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, now(w), single<T,E>)
    // is well-formed (where T is an alias for any_time_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<single<T, E>, T'&, E'>, that
    // will ask whether value(single<T,E>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
   static_assert(TimeSenderTo<W, single<Other, E>>);
   if constexpr((bool)TimeSenderTo<W, single<Other, E>>) {
      pobj_ = std::addressof(w);
      vptr_ = detail::any_time_executor_ref_vtable_v<E, TP, Other, Wrapped>();
   }
  }
  std::chrono::system_clock::time_point now() {
    return vptr_->now_(pobj_);
  }
  template<class SingleReceiver>
  void submit(TP tp, SingleReceiver&& sa) {
    // static_assert(
    //   ConvertibleTo<SingleReceiver, any_single<Other, E>>,
    //   "requires any_single<any_time_executor_ref<E, TP>, E>");
    any_single<Other, E> s{(SingleReceiver&&) sa};
    vptr_->submit_(pobj_, tp, &s);
  }
};

using archtype_any_time_executor_ref = any_time_executor_ref<std::exception_ptr, std::chrono::system_clock::time_point, 0>;

template <class E = std::exception_ptr, class TP = std::chrono::system_clock::time_point>
any_time_executor_ref()->any_time_executor_ref<E, TP, 0>;

template <class Wrapped, class E = std::exception_ptr, class TP = std::chrono::system_clock::time_point>
any_time_executor_ref(Wrapped)->any_time_executor_ref<E, TP, 0>;

template<class E, class TP>
struct any_time_executor :
  any_time_single_deferred<any_time_executor_ref<E, TP>, E, TP> {
  constexpr any_time_executor() = default;
  using any_time_single_deferred<any_time_executor_ref<E, TP>, E, TP>::
    any_time_single_deferred;
};

template <class E = std::exception_ptr, class TP = std::chrono::system_clock::time_point>
any_time_executor()->any_time_executor<E, TP>;

template <class E = std::exception_ptr, class TP = std::chrono::system_clock::time_point, class Wrapped>
any_time_executor(Wrapped)->any_time_executor<E, TP>;

} // namespace pushmi
