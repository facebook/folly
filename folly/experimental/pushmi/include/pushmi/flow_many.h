#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "many.h"


namespace pushmi {
namespace detail {
struct erase_receiver_t {};
} // namespace detail

template <class V, class PV, class PE, class E>
class flow_many<detail::erase_receiver_t, V, PV, PE, E> {
  bool done_ = false;
  bool started_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); }
    static void s_next(data&, V) {}
    static void s_starting(data&, any_many<PV, PE>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*next_)(data&, V) = vtable::s_next;
    void (*starting_)(data&, any_many<PV, PE>) = vtable::s_starting;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_many(Wrapped obj, std::false_type) : flow_many() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>(src.pobj_));
      }
      static void error(data& src, E e) noexcept {
        ::pushmi::set_error(*static_cast<Wrapped*>(src.pobj_), std::move(e));
      }
      static void next(data& src, V v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), std::move(v));
      }
      static void starting(data& src, any_many<PV, PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>(src.pobj_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::next, s::starting};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  flow_many(Wrapped obj, std::true_type) noexcept : flow_many() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
      static void next(data& src, V v) {
        ::pushmi::set_next(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(v));
      }
      static void starting(data& src, any_many<PV, PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>((void*)src.buffer_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::next, s::starting};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_many>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_flow<>, is_many<>>;

  flow_many() = default;
  flow_many(flow_many&& that) noexcept : flow_many() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires FlowManyReceiver<wrapped_t<Wrapped>, any_many<PV, PE>, V, PV, PE, E>)
  explicit flow_many(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_many{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_many() {
    vptr_->op_(data_, nullptr);
  }
  flow_many& operator=(flow_many&& that) noexcept {
    this->~flow_many();
    new ((void*)this) flow_many(std::move(that));
    return *this;
  }
  void next(V v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    vptr_->next_(data_, std::move(v));
  }
  void error(E e) noexcept {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    vptr_->done_(data_);
  }

  void starting(any_many<PE> up) {
    if (started_) {std::abort();}
    started_ = true;
    vptr_->starting_(data_, std::move(up));
  }
};

// Class static definitions:
template <class V, class PV, class PE, class E>
constexpr typename flow_many<detail::erase_receiver_t, V, PV, PE, E>::vtable const
  flow_many<detail::erase_receiver_t, V, PV, PE, E>::noop_;

template <class NF, class EF, class DF, class StrtF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class flow_many<NF, EF, DF, StrtF> {
  bool done_ = false;
  bool started_ = false;
  NF nf_;
  EF ef_;
  DF df_;
  StrtF strtf_;

 public:
  using properties = property_set<is_receiver<>, is_flow<>, is_many<>>;

  static_assert(
      !detail::is_v<NF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");

  flow_many() = default;
  constexpr explicit flow_many(NF nf)
      : flow_many(std::move(nf), EF{}, DF{}) {}
  constexpr explicit flow_many(EF ef)
      : flow_many(NF{}, std::move(ef), DF{}) {}
  constexpr explicit flow_many(DF df)
      : flow_many(NF{}, EF{}, std::move(df)) {}
  constexpr flow_many(EF ef, DF df)
      : nf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr flow_many(
      NF nf,
      EF ef,
      DF df = DF{},
      StrtF strtf = StrtF{})
      : nf_(std::move(nf)),
        ef_(std::move(ef)),
        df_(std::move(df)),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<NF&, V>)
  void next(V&& v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    nf_((V&&) v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    ef_(std::move(e));
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    df_();
  }
  PUSHMI_TEMPLATE(class Up)
    (requires Invocable<StrtF&, Up&&>)
  void starting(Up&& up) {
    if (started_) {std::abort();}
    started_ = true;
    strtf_( (Up &&) up);
  }
};

template<
    PUSHMI_TYPE_CONSTRAINT(Receiver) Data,
    class DNF,
    class DEF,
    class DDF,
    class DStrtF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class flow_many<Data, DNF, DEF, DDF, DStrtF> {
  bool done_ = false;
  bool started_ = false;
  Data data_;
  DNF nf_;
  DEF ef_;
  DDF df_;
  DStrtF strtf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_flow<>, is_many<>>>;

  static_assert(
      !detail::is_v<DNF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");

  constexpr explicit flow_many(Data d)
      : flow_many(std::move(d), DNF{}, DEF{}, DDF{}) {}
  constexpr flow_many(Data d, DDF df)
      : data_(std::move(d)), nf_(), ef_(), df_(df) {}
  constexpr flow_many(Data d, DEF ef, DDF df = DDF{})
      : data_(std::move(d)), nf_(), ef_(ef), df_(df) {}
  constexpr flow_many(
      Data d,
      DNF nf,
      DEF ef = DEF{},
      DDF df = DDF{},
      DStrtF strtf = DStrtF{})
      : data_(std::move(d)),
        nf_(nf),
        ef_(ef),
        df_(df),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<DNF&, Data&, V>)
  void next(V&& v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    nf_(data_, (V&&) v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E&& e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    ef_(data_, (E&&) e);
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    df_(data_);
  }
  PUSHMI_TEMPLATE (class Up)
    (requires Invocable<DStrtF&, Data&, Up&&>)
  void starting(Up&& up) {
    if (started_) {std::abort();}
    started_ = true;
    strtf_(data_, (Up &&) up);
  }
};

template <>
class flow_many<>
    : public flow_many<ignoreNF, abortEF, ignoreDF, ignoreStrtF> {
};

// TODO winnow down the number of make_flow_many overloads and deduction
// guides here, as was done for make_many.

////////////////////////////////////////////////////////////////////////////////
// make_flow_many
PUSHMI_INLINE_VAR constexpr struct make_flow_many_fn {
  inline auto operator()() const {
    return flow_many<>{};
  }
  PUSHMI_TEMPLATE (class NF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
  auto operator()(NF nf) const {
    return flow_many<NF, abortEF, ignoreDF, ignoreStrtF>{
      std::move(nf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return flow_many<ignoreNF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>{
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return flow_many<ignoreNF, abortEF, DF, ignoreStrtF>{std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(NF nf, EF ef) const {
    return flow_many<NF, EF, ignoreDF, ignoreStrtF>{std::move(nf),
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return flow_many<ignoreNF, EF, DF, ignoreStrtF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df) const {
    return flow_many<NF, EF, DF, ignoreStrtF>{std::move(nf),
      std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF, class DF, class StrtF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df, StrtF strtf) const {
    return flow_many<NF, EF, DF, StrtF>{std::move(nf), std::move(ef),
      std::move(df), std::move(strtf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data>))
  auto operator()(Data d) const {
    return flow_many<Data, passDNXF, passDEF, passDDF, passDStrtF>{
        std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
  auto operator()(Data d, DNF nf) const {
    return flow_many<Data, DNF, passDEF, passDDF, passDStrtF>{
      std::move(d), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return flow_many<Data, passDNXF, on_error_fn<DEFN...>, passDDF, passDStrtF>{
      std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return flow_many<Data, passDNXF, passDEF, DDF, passDStrtF>{
      std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DNF nf, DEF ef) const {
    return flow_many<Data, DNF, DEF, passDDF, passDStrtF>{std::move(d), std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return flow_many<Data, passDNXF, DEF, DDF, passDStrtF>{
      std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df) const {
    return flow_many<Data, DNF, DEF, DDF, passDStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF, class DStrtF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df, DStrtF strtf) const {
    return flow_many<Data, DNF, DEF, DDF, DStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df), std::move(strtf)};
  }
} const make_flow_many {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_many() -> flow_many<>;

PUSHMI_TEMPLATE(class NF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
flow_many(NF) ->
  flow_many<NF, abortEF, ignoreDF, ignoreStrtF>;

template <class... EFN>
flow_many(on_error_fn<EFN...>) ->
  flow_many<ignoreNF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
flow_many(DF) ->
  flow_many<ignoreNF, abortEF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
flow_many(NF, EF) ->
  flow_many<NF, EF, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
flow_many(EF, DF) ->
  flow_many<ignoreNF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
flow_many(NF, EF, DF) ->
  flow_many<NF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF, class StrtF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
flow_many(NF, EF, DF, StrtF) ->
  flow_many<NF, EF, DF, StrtF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data>))
flow_many(Data d) ->
  flow_many<Data, passDNXF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
flow_many(Data d, DNF nf) ->
  flow_many<Data, DNF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data>))
flow_many(Data d, on_error_fn<DEFN...>) ->
  flow_many<Data, passDNXF, on_error_fn<DEFN...>, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DDF) ->
    flow_many<Data, passDNXF, passDEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
flow_many(Data d, DNF nf, DEF ef) ->
  flow_many<Data, DNF, DEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DEF, DDF) ->
  flow_many<Data, passDNXF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DNF nf, DEF ef, DDF df) ->
  flow_many<Data, DNF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF, class DStrtF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&> ))
flow_many(Data d, DNF nf, DEF ef, DDF df, DStrtF strtf) ->
  flow_many<Data, DNF, DEF, DDF, DStrtF>;
#endif

template <class V, class PV = std::ptrdiff_t, class PE = std::exception_ptr, class E = PE>
using any_flow_many = flow_many<detail::erase_receiver_t, V, PV, PE, E>;

template<>
struct construct_deduced<flow_many> : make_flow_many_fn {};

// template <class V, class PE = std::exception_ptr, class E = PE, class Wrapped>
//     requires FlowManyReceiver<Wrapped, V, PE, E> && !detail::is_v<Wrapped, many> &&
//     !detail::is_v<Wrapped, std::promise>
//     auto erase_cast(Wrapped w) {
//   return flow_many<V, PE, E>{std::move(w)};
// }

} // namespace pushmi
