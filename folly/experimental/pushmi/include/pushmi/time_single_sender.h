#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "single.h"
#include "executor.h"
#include "trampoline.h"

namespace pushmi {

template <class V, class E, class TP>
class any_time_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static TP s_now(data&) { return TP{}; }
    static any_time_executor<E, TP> s_executor(data&) { return {}; }
    static void s_submit(data&, TP, single<V, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    TP (*now_)(data&) = vtable::s_now;
    any_time_executor<E, TP> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, TP, single<V, E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_time_single_sender(Wrapped obj, std::false_type)
    : any_time_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static TP now(data& src) {
        return ::pushmi::now(*static_cast<Wrapped*>(src.pobj_));
      }
      static any_time_executor<E, TP> executor(data& src) {
        return any_time_executor<E, TP>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, TP at, single<V, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>(src.pobj_),
            std::move(at),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::now, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_time_single_sender(Wrapped obj, std::true_type) noexcept
    : any_time_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static TP now(data& src) {
        return ::pushmi::now(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static any_time_executor<E, TP> executor(data& src) {
        return any_time_executor<E, TP>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, TP tp, single<V, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(tp),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::now, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_time_single_sender>::value, U>;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  any_time_single_sender() = default;
  any_time_single_sender(any_time_single_sender&& that) noexcept
      : any_time_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires TimeSenderTo<wrapped_t<Wrapped>, single<V, E>>)
  explicit any_time_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
  : any_time_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {
  }
  ~any_time_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_time_single_sender& operator=(any_time_single_sender&& that) noexcept {
    this->~any_time_single_sender();
    new ((void*)this) any_time_single_sender(std::move(that));
    return *this;
  }
  TP now() {
    return vptr_->now_(data_);
  }
  any_time_executor<E, TP> executor() {
    return vptr_->executor_(data_);
  }
  void submit(TP at, single<V, E> out) {
    vptr_->submit_(data_, std::move(at), std::move(out));
  }
};

// Class static definitions:
template <class V, class E, class TP>
constexpr typename any_time_single_sender<V, E, TP>::vtable const
    any_time_single_sender<V, E, TP>::noop_;

template<class SF, class NF, class EXF>
  // (requires Invocable<NF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
class time_single_sender<SF, NF, EXF> {
  SF sf_;
  EXF exf_;
  NF nf_;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  constexpr time_single_sender() = default;
  constexpr explicit time_single_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr time_single_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}
  constexpr time_single_sender(SF sf, EXF exf, NF nf)
      : sf_(std::move(sf)), nf_(std::move(nf)), exf_(std::move(exf)) {}

  auto now() {
    return nf_();
  }
  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out, is_single<>> &&
      Invocable<SF&, TP, Out>)
  void submit(TP tp, Out out) {
    sf_(std::move(tp), std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(TimeSender<is_single<>>) Data, class DSF, class DNF, class DEXF>
#if __cpp_concepts
  requires Invocable<DNF&, Data&> && Invocable<DEXF&, Data&>
#endif
class time_single_sender<Data, DSF, DNF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;
  DNF nf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_time<>, is_single<>>>;

  constexpr time_single_sender() = default;
  constexpr explicit time_single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr time_single_sender(Data data, DSF sf, DEXF exf = DEXF{})
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}
  constexpr time_single_sender(Data data, DSF sf, DEXF exf, DNF nf)
      : data_(std::move(data)), sf_(std::move(sf)), nf_(std::move(nf)), exf_(std::move(exf)) {}

  auto now() {
    return nf_(data_);
  }
  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out, is_single<>> &&
      Invocable<DSF&, Data&, TP, Out>)
  void submit(TP tp, Out out) {
    sf_(data_, std::move(tp), std::move(out));
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_time_single_sender
PUSHMI_INLINE_VAR constexpr struct make_time_single_sender_fn {
  inline auto operator()() const  {
    return time_single_sender<ignoreSF, systemNowF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return time_single_sender<SF, systemNowF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE (class SF, class EXF)
    (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return time_single_sender<SF, systemNowF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE (class SF, class NF, class EXF)
    (requires Invocable<NF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf, NF nf) const {
    return time_single_sender<SF, NF, EXF>{std::move(sf), std::move(exf), std::move(nf)};
  }
  PUSHMI_TEMPLATE (class Data)
    (requires TimeSender<Data, is_single<>>)
  auto operator()(Data d) const {
    return time_single_sender<Data, passDSF, passDNF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF)
    (requires TimeSender<Data, is_single<>>)
  auto operator()(Data d, DSF sf) const {
    return time_single_sender<Data, DSF, passDNF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
    (requires TimeSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const  {
    return time_single_sender<Data, DSF, passDNF, DEXF>{std::move(d), std::move(sf),
      std::move(exf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DNF, class DEXF)
    (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf, DNF nf) const  {
    return time_single_sender<Data, DSF, DNF, DEXF>{std::move(d), std::move(sf),
      std::move(exf), std::move(nf)};
  }
} const make_time_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
time_single_sender() -> time_single_sender<ignoreSF, systemNowF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF) -> time_single_sender<SF, systemNowF, trampolineEXF>;

PUSHMI_TEMPLATE (class SF, class EXF)
  (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF, EXF) -> time_single_sender<SF, systemNowF, EXF>;

PUSHMI_TEMPLATE (class SF, class NF, class EXF)
  (requires Invocable<NF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF, EXF, NF) -> time_single_sender<SF, NF, EXF>;

PUSHMI_TEMPLATE (class Data, class DSF)
  (requires TimeSender<Data, is_single<>>)
time_single_sender(Data, DSF) -> time_single_sender<Data, DSF, passDNF, passDEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
  (requires TimeSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
time_single_sender(Data, DSF, DEXF) -> time_single_sender<Data, DSF, passDNF, DEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DNF, class DEXF)
  (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&> && Invocable<DEXF&, Data&>)
time_single_sender(Data, DSF, DEXF, DNF) -> time_single_sender<Data, DSF, DNF, DEXF>;
#endif

template<>
struct construct_deduced<time_single_sender>
  : make_time_single_sender_fn {};

// template <
//     class V,
//     class E = std::exception_ptr,
//     class TP = std::chrono::system_clock::time_point,
//     TimeSenderTo<single<V, E>, is_single<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return time_single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
