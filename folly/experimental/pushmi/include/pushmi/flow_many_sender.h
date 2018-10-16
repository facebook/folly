#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "flow_many.h"
#include "executor.h"
#include "trampoline.h"

namespace pushmi {

template <class V, class PV, class PE, class E>
class flow_many_sender<V, PV, PE, E> {
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
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_flow_many<V, PV, PE, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_flow_many<V, PV, PE, E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_many_sender(Wrapped obj, std::false_type) : flow_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_flow_many<V, PV, PE, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  flow_many_sender(Wrapped obj, std::true_type) noexcept
    : flow_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_flow_many<V, PV, PE, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_many_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_many<>>;

  flow_many_sender() = default;
  flow_many_sender(flow_many_sender&& that) noexcept
      : flow_many_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_many<>>)
  explicit flow_many_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_many_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_many_sender() {
    vptr_->op_(data_, nullptr);
  }
  flow_many_sender& operator=(flow_many_sender&& that) noexcept {
    this->~flow_many_sender();
    new ((void*)this) flow_many_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_flow_many<V, PV, PE, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class PV, class PE, class E>
constexpr typename flow_many_sender<V, PV, PE, E>::vtable const
    flow_many_sender<V, PV, PE, E>::noop_;

template <class SF, class EXF>
class flow_many_sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_many<>>;

  constexpr flow_many_sender() = default;
  constexpr explicit flow_many_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr flow_many_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_many<>, is_flow<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_many<>, is_flow<>>) Data, class DSF, class DEXF>
class flow_many_sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_flow<>, is_many<>>>;

  constexpr flow_many_sender() = default;
  constexpr explicit flow_many_sender(Data data)
      : data_(std::move(data)) {}
  constexpr flow_many_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr flow_many_sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_many<>, is_flow<>> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class flow_many_sender<>
    : public flow_many_sender<ignoreSF, trampolineEXF> {
public:
  flow_many_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_many_sender
PUSHMI_INLINE_VAR constexpr struct make_flow_many_sender_fn {
  inline auto operator()() const {
    return flow_many_sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return flow_many_sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return flow_many_sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_many<>, is_flow<>>)
  auto operator()(Data d) const {
    return flow_many_sender<Data, passDSF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_many<>, is_flow<>>)
  auto operator()(Data d, DSF sf) const {
    return flow_many_sender<Data, DSF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_many<>, is_flow<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const {
    return flow_many_sender<Data, DSF, DEXF>{std::move(d), std::move(sf), std::move(exf)};
  }
} const make_flow_many_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_many_sender() -> flow_many_sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_many_sender(SF) -> flow_many_sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_many_sender(SF, EXF) -> flow_many_sender<SF, EXF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_many<>, is_flow<>>)
flow_many_sender(Data) -> flow_many_sender<Data, passDSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_many<>, is_flow<>>)
flow_many_sender(Data, DSF) -> flow_many_sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_many<>, is_flow<>> && Invocable<DEXF&, Data&>)
flow_many_sender(Data, DSF, DEXF) -> flow_many_sender<Data, DSF, DEXF>;
#endif

template <class V, class PV = std::ptrdiff_t, class PE = std::exception_ptr, class E = PE>
using any_flow_many_sender = flow_many_sender<V, PV, PE, E>;

template<>
struct construct_deduced<flow_many_sender>
  : make_flow_many_sender_fn {};

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_many_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
