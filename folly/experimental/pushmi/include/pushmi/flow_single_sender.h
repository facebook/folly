#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "flow_single.h"

namespace pushmi {

template <class V, class PE, class E>
class flow_single_sender<V, PE, E> {
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
    void (*op_)(data&, data*) = vtable::s_op;
    void (*submit_)(data&, flow_single<V, PE, E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_single_sender(Wrapped obj, std::false_type) : flow_single_sender() {
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
  template <class Wrapped>
  flow_single_sender(Wrapped obj, std::true_type) noexcept
    : flow_single_sender() {
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
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_single_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  flow_single_sender() = default;
  flow_single_sender(flow_single_sender&& that) noexcept
      : flow_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_single<>>)
  explicit flow_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  flow_single_sender& operator=(flow_single_sender&& that) noexcept {
    this->~flow_single_sender();
    new ((void*)this) flow_single_sender(std::move(that));
    return *this;
  }
  void submit(flow_single<V, PE, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class PE, class E>
constexpr typename flow_single_sender<V, PE, E>::vtable const
    flow_single_sender<V, PE, E>::noop_;

template <class SF>
class flow_single_sender<SF> {
  SF sf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(SF sf)
      : sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_single<>, is_flow<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_single<>, is_flow<>>) Data, class DSF>
class flow_single_sender<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using properties = property_set<is_sender<>, is_single<>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr flow_single_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_single<>, is_flow<>> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_single_sender
PUSHMI_INLINE_VAR constexpr struct make_flow_single_sender_fn {
  inline auto operator()() const {
    return flow_single_sender<ignoreSF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return flow_single_sender<SF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_single<>, is_flow<>>)
  auto operator()(Data d) const {
    return flow_single_sender<Data, passDSF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_single<>, is_flow<>>)
  auto operator()(Data d, DSF sf) const {
    return flow_single_sender<Data, DSF>{std::move(d), std::move(sf)};
  }
} const make_flow_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_single_sender() -> flow_single_sender<ignoreSF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_single_sender(SF) -> flow_single_sender<SF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_single<>, is_flow<>>)
flow_single_sender(Data) -> flow_single_sender<Data, passDSF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_single<>, is_flow<>>)
flow_single_sender(Data, DSF) -> flow_single_sender<Data, DSF>;
#endif

template <class V, class PE = std::exception_ptr, class E = PE>
using any_flow_single_sender = flow_single_sender<V, PE, E>;

template<>
struct construct_deduced<flow_single_sender>
  : make_flow_single_sender_fn {};

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
