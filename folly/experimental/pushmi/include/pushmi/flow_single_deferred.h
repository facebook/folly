#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "flow_single.h"

namespace pushmi {

template <class V, class PE, class E>
class flow_single_deferred<V, PE, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_submit(data&, flow_single<V, PE, E>) {}
    void (*op_)(data&, data*) = s_op;
    void (*submit_)(data&, flow_single<V, PE, E>) = s_submit;
    static constexpr vtable const noop_ {};
  } const* vptr_ = &vtable::noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_single_deferred>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  flow_single_deferred() = default;
  flow_single_deferred(flow_single_deferred&& that) noexcept
      : flow_single_deferred() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_single<>>
      PUSHMI_BROKEN_SUBSUMPTION(&& !insitu<Wrapped>()))
  explicit flow_single_deferred(Wrapped obj) : flow_single_deferred() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void submit(data& src, flow_single<V, PE, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_single<>> && insitu<Wrapped>())
  explicit flow_single_deferred(Wrapped obj) noexcept : flow_single_deferred() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void submit(data& src, flow_single<V, PE, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  ~flow_single_deferred() {
    vptr_->op_(data_, nullptr);
  }
  flow_single_deferred& operator=(flow_single_deferred&& that) noexcept {
    this->~flow_single_deferred();
    new ((void*)this) flow_single_deferred(std::move(that));
    return *this;
  }
  void submit(flow_single<V, PE, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class PE, class E>
constexpr typename flow_single_deferred<V, PE, E>::vtable const
    flow_single_deferred<V, PE, E>::vtable::noop_;

template <class SF>
class flow_single_deferred<SF> {
  SF sf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  constexpr flow_single_deferred() = default;
  constexpr explicit flow_single_deferred(SF sf)
      : sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_flow<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_single_deferred
inline auto make_flow_single_deferred() -> flow_single_deferred<ignoreSF> {
  return flow_single_deferred<ignoreSF>{};
}
template <class SF>
auto make_flow_single_deferred(SF sf) -> flow_single_deferred<SF> {
  return flow_single_deferred<SF>(std::move(sf));
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_single_deferred() -> flow_single_deferred<ignoreSF>;

template <class SF>
flow_single_deferred(SF) -> flow_single_deferred<SF>;
#endif

template <class V, class PE = std::exception_ptr, class E = PE>
using any_flow_single_deferred = flow_single_deferred<V, PE, E>;

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_single_deferred<V, E>{std::move(w)};
// }

} // namespace pushmi
