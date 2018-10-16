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
template<class E, class TP>
struct any_time_executor_ref_base {
private:
  friend any_time_executor_ref<E, TP, 0>;
  friend any_time_executor_ref<E, TP, 1>;
  using Other = any_time_executor_ref<E, TP, 1>;

  void* pobj_;
  struct vtable {
    TP (*now_)(void*);
    void (*submit_)(void*, TP, void*);
  } const *vptr_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_base_of<any_time_executor_ref_base, U>::value, U>;
public:
  using properties = property_set<is_time<>, is_single<>>;

  any_time_executor_ref_base() = delete;
  any_time_executor_ref_base(const any_time_executor_ref_base&) = default;

  PUSHMI_TEMPLATE (class Wrapped)
    (requires TimeSender<wrapped_t<Wrapped>, is_single<>>)
    // (requires TimeSenderTo<wrapped_t<Wrapped>, single<Other, E>>)
  any_time_executor_ref_base(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, now(w), single<T,E>)
    // is well-formed (where T is an alias for any_time_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<single<T, E>, T'&, E'>, that
    // will ask whether value(single<T,E>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      TimeSenderTo<Wrapped, single<Other, E>>,
      "Expecting to be passed a TimeSender that can send to a SingleReceiver"
      " that accpets a value of type Other and an error of type E");
    struct s {
      static TP now(void* pobj) {
        return ::pushmi::now(*static_cast<Wrapped*>(pobj));
      }
      static void submit(void* pobj, TP tp, void* s) {
        return ::pushmi::submit(
          *static_cast<Wrapped*>(pobj),
          tp,
          std::move(*static_cast<single<Other, E>*>(s)));
      }
    };
    static const vtable vtbl{s::now, s::submit};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
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
} // namespace detail

template<class E, class TP, int i>
struct any_time_executor_ref : detail::any_time_executor_ref_base<E, TP> {
  using detail::any_time_executor_ref_base<E, TP>::any_time_executor_ref_base;
  any_time_executor_ref(const detail::any_time_executor_ref_base<E, TP>& o)
    : detail::any_time_executor_ref_base<E, TP>(o) {}
};

////////////////////////////////////////////////////////////////////////////////
// make_any_time_executor_ref
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
auto make_any_time_executor_ref() -> any_time_executor_ref<E, TP> {
  return any_time_executor_ref<E, TP, 0>{};
}
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point,
    class Wrapped>
auto make_any_time_executor_ref(Wrapped w) -> any_time_executor_ref<E, TP> {
  return any_time_executor_ref<E, TP, 0>{std::move(w)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_time_executor_ref() ->
    any_time_executor_ref<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;

template <class Wrapped>
any_time_executor_ref(Wrapped) ->
    any_time_executor_ref<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;
#endif

template<class E, class TP>
struct any_time_executor :
  any_time_single_deferred<any_time_executor_ref<E, TP>, E, TP> {
  constexpr any_time_executor() = default;
  using any_time_single_deferred<any_time_executor_ref<E, TP>, E, TP>::
    any_time_single_deferred;
};

////////////////////////////////////////////////////////////////////////////////
// make_any_time_executor
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
auto make_any_time_executor() -> any_time_executor<E, TP> {
  return any_time_executor<E, TP>{};
}

template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point,
    class Wrapped>
auto make_any_time_executor(Wrapped w) -> any_time_executor<E, TP> {
  return any_time_executor<E, TP>{std::move(w)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_time_executor() ->
    any_time_executor<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;

template <class Wrapped>
any_time_executor(Wrapped) ->
    any_time_executor<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;
#endif

} // namespace pushmi
