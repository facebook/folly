#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "single.h"

namespace pushmi {

template <class V, class PE, class E>
class flow_single<V, PE, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a std::promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible_v<Wrapped>;
  }
  enum struct op { destroy, move };
  struct vtable {
    void (*op_)(op, data&, data*) = +[](op, data&, data*) {};
    void (*done_)(data&) = +[](data&) {};
    void (*error_)(data&, E) noexcept = +[](data&, E) noexcept {
      std::terminate();
    };
    void (*value_)(data&, V) = +[](data&, V) {};
    void (*stopping_)(data&) noexcept = +[](data&) noexcept {};
    void (*starting_)(data&, any_none<PE>&) = +[](data&, any_none<PE>&) {};
    static constexpr vtable const noop_ = {};
  } const* vptr_ = &vtable::noop_;
  template <class Wrapped, bool = insitu<Wrapped>()>
  static constexpr vtable const vtable_v = {
      +[](op o, data& src, data* dst) {
        switch (o) {
          case op::move:
            dst->pobj_ = std::exchange(src.pobj_, nullptr);
          case op::destroy:
            delete static_cast<Wrapped const*>(src.pobj_);
        }
      },
      +[](data& src) { ::pushmi::set_done(*static_cast<Wrapped*>(src.pobj_)); },
      +[](data& src, E e) noexcept {
          ::pushmi::set_error(*static_cast<Wrapped*>(src.pobj_), std::move(e));
      },
      +[](data& src, V v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), std::move(v));
      },
      +[](data& src) {
        ::pushmi::set_stopping(*static_cast<Wrapped*>(src.pobj_));
      },
      +[](data& src, any_none<PE>& up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>(src.pobj_), up);
      }
  };

  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, flow_single>, U>;
public:
  using receiver_category = flow_tag;

  flow_single() = default;
  flow_single(flow_single&& that) noexcept : flow_single() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class Wrapped>
      requires FlowSingle<wrapped_t<Wrapped>, any_none<PE>, V, PE, E>
  explicit flow_single(Wrapped obj)
      : flow_single() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~flow_single() {
    vptr_->op_(op::destroy, data_, nullptr);
  }
  flow_single& operator=(flow_single&& that) noexcept {
    this->~flow_single();
    new ((void*)this) flow_single(std::move(that));
    return *this;
  }
  void value(V v) {
    vptr_->value_(data_, std::move(v));
  }
  void error(E e) noexcept {
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    vptr_->done_(data_);
  }

  void stopping() noexcept {
    vptr_->stopping_(data_);
  }
  void starting(any_none<PE>& up) {
    vptr_->starting_(data_, up);
  }
};

// Class static definitions:
template <class V, class PE, class E>
constexpr typename flow_single<V, PE, E>::vtable const
    flow_single<V, PE, E>::vtable::noop_;
template <class V, class PE, class E>
template <class Wrapped, bool Big>
constexpr typename flow_single<V, PE, E>::vtable const
    flow_single<V, PE, E>::vtable_v;
template <class V, class PE, class E>
template <class Wrapped>
constexpr typename flow_single<V, PE, E>::vtable const
    flow_single<V, PE, E>::vtable_v<Wrapped, true> = {
        +[](op o, data& src, data* dst) {
          switch (o) {
            case op::move:
              new (dst->buffer_) Wrapped(
                  std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
            case op::destroy:
              static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
          }
        },
        +[](data& src) {
          ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
        },
        +[](data& src, E e) noexcept {::pushmi::set_error(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(e));
        },
        +[](data& src, V v) {
          ::pushmi::set_value(
              *static_cast<Wrapped*>((void*)src.buffer_), std::move(v));
        },
        +[](data& src) noexcept {
          ::pushmi::set_stopping(*static_cast<Wrapped*>((void*)src.buffer_));
        },
        +[](data& src, any_none<PE>& up) {
          ::pushmi::set_starting(*static_cast<Wrapped*>((void*)src.buffer_), up);
        }
    };

template <class VF, class EF, class DF, class StpF, class StrtF>
  requires Invocable<DF&>
class flow_single<VF, EF, DF, StpF, StrtF> {
  VF vf_;
  EF ef_;
  DF df_;
  StpF stpf_;
  StrtF strtf_;

 public:
  using receiver_category = flow_tag;

  static_assert(
      !detail::is_v<VF, on_error>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value>,
      "the second parameter is the error implementation, but on_value{} was passed");

  flow_single() = default;
  constexpr explicit flow_single(VF vf)
      : flow_single(std::move(vf), EF{}, DF{}) {}
  constexpr explicit flow_single(EF ef)
      : flow_single(VF{}, std::move(ef), DF{}) {}
  constexpr explicit flow_single(DF df)
      : flow_single(VF{}, EF{}, std::move(df)) {}
  constexpr flow_single(EF ef, DF df)
      : vf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr flow_single(
      VF vf,
      EF ef,
      DF df = DF{},
      StpF stpf = StpF{},
      StrtF strtf = StrtF{})
      : vf_(std::move(vf)),
        ef_(std::move(ef)),
        df_(std::move(df)),
        stpf_(std::move(stpf)),
        strtf_(std::move(strtf)) {}
  template <class V>
  requires Invocable<VF&, V>
  void value(V v) {
    vf_(v);
  }
  template <class E>
    requires Invocable<EF&, E>
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    ef_(std::move(e));
  }
  void done() {
    df_();
  }
  void stopping() noexcept {
    stpf_();
  }
  template <Receiver<none_tag> Up>
    requires Invocable<StrtF&, Up&>
  void starting(Up& up) {
    strtf_(up);
  }
};

template <
    Receiver Data,
    class DVF,
    class DEF,
    class DDF,
    class DStpF,
    class DStrtF>
requires Invocable<DDF&, Data&>
class flow_single<Data, DVF, DEF, DDF, DStpF, DStrtF> {
  Data data_;
  DVF vf_;
  DEF ef_;
  DDF df_;
  DStpF stpf_;
  DStrtF strtf_;

 public:
  using receiver_category = flow_tag;

  static_assert(
      !detail::is_v<DVF, on_error>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value>,
      "the second parameter is the error implementation, but on_value{} was passed");

  constexpr explicit flow_single(Data d)
      : flow_single(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr flow_single(Data d, DDF df)
      : data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr flow_single(Data d, DEF ef, DDF df = DDF{})
      : data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr flow_single(
      Data d,
      DVF vf,
      DEF ef = DEF{},
      DDF df = DDF{},
      DStpF stpf = DStpF{},
      DStrtF strtf = DStrtF{})
      : data_(std::move(d)),
        vf_(vf),
        ef_(ef),
        df_(df),
        stpf_(std::move(stpf)),
        strtf_(std::move(strtf)) {}
  template <class V>
  requires Invocable<DVF&, Data&, V> void value(V v) {
    vf_(data_, v);
  }
  template <class E>
  requires Invocable<DEF&, Data&, E> void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    ef_(data_, e);
  }
  void done() {
    df_(data_);
  }
  void stopping() noexcept {
    stpf_(data_);
  }
  template <class Up>
  requires Invocable<DStrtF&, Data&, Up&>
  void starting(Up& up) {
    strtf_(data_, up);
  }
};

template <>
class flow_single<>
    : public flow_single<ignoreVF, abortEF, ignoreDF, ignoreStpF, ignoreStrtF> {
};

using archetype_flow_single = flow_single<>;

flow_single()->archetype_flow_single;

template <class VF>
    requires !Receiver<VF> && !detail::is_v<VF, on_error> &&
    !detail::is_v<VF, on_done> flow_single(VF)
         ->flow_single<VF, abortEF, ignoreDF, ignoreStpF, ignoreStrtF>;

template <class... EFN>
flow_single(on_error<EFN...>)
    ->flow_single<
        ignoreVF,
        on_error<EFN...>,
        ignoreDF,
        ignoreStpF,
        ignoreStrtF>;

template <class DF>
flow_single(on_done<DF>)
    ->flow_single<ignoreVF, abortEF, on_done<DF>, ignoreStpF, ignoreStrtF>;

template <class V, class PE, class E, class Wrapped>
    requires FlowSingle<Wrapped, V, PE, E> &&
    !detail::is_v<Wrapped, none> flow_single(Wrapped)->flow_single<V, PE, E>;

template <class VF, class EF>
    requires !Receiver<VF> && !detail::is_v<VF, on_error> &&
    !detail::is_v<VF, on_done> && !detail::is_v<EF, on_value> &&
    !detail::is_v<EF, on_done> flow_single(VF, EF)
         ->flow_single<VF, EF, ignoreDF, ignoreStpF, ignoreStrtF>;

template <class... EFN, class DF>
flow_single(on_error<EFN...>, on_done<DF>)
    ->flow_single<
        ignoreVF,
        on_error<EFN...>,
        on_done<DF>,
        ignoreStpF,
        ignoreStrtF>;

template <class VF, class EF, class DF>
requires Invocable<DF&> flow_single(VF, EF, DF)
    ->flow_single<VF, EF, DF, ignoreStpF, ignoreStrtF>;

template <class VF, class EF, class DF, class StpF>
requires Invocable<DF&>&& Invocable<StpF&> flow_single(VF, EF, DF, StpF)
    ->flow_single<VF, EF, DF, StpF, ignoreStrtF>;

template <class VF, class EF, class DF, class StpF, class StrtF>
requires Invocable<DF&>&& Invocable<StpF&> flow_single(VF, EF, DF, StpF, StrtF)
    ->flow_single<VF, EF, DF, StpF, StrtF>;

template <Receiver Data>
flow_single(Data d)
    ->flow_single<Data, passDVF, passDEF, passDDF, passDStpF, passDStrtF>;

template <Receiver Data, class DVF>
    requires !detail::is_v<DVF, on_error> &&
    !detail::is_v<DVF, on_done> flow_single(Data d, DVF vf)
         ->flow_single<Data, DVF, passDEF, passDDF, passDStpF, passDStrtF>;

template <Receiver Data, class... DEFN>
flow_single(Data d, on_error<DEFN...>)
    ->flow_single<
        Data,
        passDVF,
        on_error<DEFN...>,
        passDDF,
        passDStpF,
        passDStrtF>;

template <Receiver Data, class DVF, class DEF>
    requires !detail::is_v<DVF, on_error> && !detail::is_v<DVF, on_done> &&
    !detail::is_v<DEF, on_done> flow_single(Data d, DVF vf, DEF ef)
         ->flow_single<Data, DVF, DEF, passDDF, passDStpF, passDStrtF>;

template <Receiver Data, class... DEFN, class DDF>
flow_single(Data d, on_error<DEFN...>, on_done<DDF>)
    ->flow_single<
        Data,
        passDVF,
        on_error<DEFN...>,
        on_done<DDF>,
        passDStpF,
        passDStrtF>;

template <Receiver Data, class DDF>
flow_single(Data d, on_done<DDF>)
    ->flow_single<Data, passDVF, passDEF, on_done<DDF>, passDStpF, passDStrtF>;

template <Receiver Data, class DVF, class DEF, class DDF>
requires Invocable<DDF&, Data&> flow_single(Data d, DVF vf, DEF ef, DDF df)
    ->flow_single<Data, DVF, DEF, DDF, passDStpF, passDStrtF>;

template <Receiver Data, class DVF, class DEF, class DDF, class DStpF>
requires Invocable<DDF&, Data&>&& Invocable<DStpF&, Data&>
flow_single(Data d, DVF vf, DEF ef, DDF df, DStpF stpf)
    ->flow_single<Data, DVF, DEF, DDF, DStpF, passDStrtF>;

template <
    Receiver Data,
    class DVF,
    class DEF,
    class DDF,
    class DStpF,
    class DStrtF>
requires Invocable<DDF&, Data&>&& Invocable<DStpF&, Data&>
flow_single(Data d, DVF vf, DEF ef, DDF df, DStpF stpf, DStrtF strtf)
    ->flow_single<Data, DVF, DEF, DDF, DStpF, DStrtF>;

template <class V, class PE = std::exception_ptr, class E = PE>
using any_flow_single = flow_single<V, PE, E>;

// template <class V, class PE = std::exception_ptr, class E = PE, class Wrapped>
//     requires FlowSingle<Wrapped, V, PE, E> && !detail::is_v<Wrapped, none> &&
//     !detail::is_v<Wrapped, std::promise>
//     auto erase_cast(Wrapped w) {
//   return flow_single<V, PE, E>{std::move(w)};
// }

} // namespace pushmi
