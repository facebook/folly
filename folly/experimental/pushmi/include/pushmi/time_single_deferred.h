#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "single.h"

namespace pushmi {

template <class V, class E, class TP>
class time_single_deferred<V, E, TP> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible_v<Wrapped>;
  }
  enum struct op { destroy, move };
  struct vtable {
    void (*op_)(op, data&, data*) = +[](op, data&, data*) {};
    TP (*now_)(data&) = +[](data&) { return TP{}; };
    void (*submit_)(data&, TP, single<V, E>) = +[](data&, TP, single<V, E>) {};
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
      +[](data& src) -> TP {
        return ::pushmi::now(*static_cast<Wrapped*>(src.pobj_));
      },
      +[](data& src, TP at, single<V, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(at), std::move(out));
      }};

 public:
  using sender_category = single_tag;

  time_single_deferred() = default;
  time_single_deferred(time_single_deferred&& that) noexcept
      : time_single_deferred() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, time_single_deferred>, U>;
  template <class Wrapped, Sender<single_tag> W = wrapped_t<Wrapped>>
    requires TimeSenderTo<W, single<V, E>>
  explicit time_single_deferred(Wrapped obj)
      : time_single_deferred() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~time_single_deferred() {
    vptr_->op_(op::destroy, data_, nullptr);
  }
  time_single_deferred& operator=(time_single_deferred&& that) noexcept {
    this->~time_single_deferred();
    new ((void*)this) time_single_deferred(std::move(that));
    return *this;
  }
  TP now() {
    vptr_->now_(data_);
  }
  void submit(TP at, single<V, E> out) {
    vptr_->submit_(data_, std::move(at), std::move(out));
  }
};

// Class static definitions:
template <class V, class E, class TP>
constexpr typename time_single_deferred<V, E, TP>::vtable const
    time_single_deferred<V, E, TP>::vtable::noop_;
template <class V, class E, class TP>
template <class Wrapped, bool Big>
constexpr typename time_single_deferred<V, E, TP>::vtable const
    time_single_deferred<V, E, TP>::vtable_v;
template <class V, class E, class TP>
template <class Wrapped>
constexpr typename time_single_deferred<V, E, TP>::vtable const
    time_single_deferred<V, E, TP>::vtable_v<Wrapped, true> = {
        +[](op o, data& src, data* dst) {
          switch (o) {
            case op::move:
              new (dst->buffer_) Wrapped(
                  std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
            case op::destroy:
              static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
          }
        },
        +[](data& src) -> TP {
          return ::pushmi::now(*static_cast<Wrapped*>((void*)src.buffer_));
        },
        +[](data& src, TP tp, single<V, E> out) {
          ::pushmi::submit(
              *static_cast<Wrapped*>((void*)src.buffer_),
              std::move(tp),
              std::move(out));
        }
    };

template <class SF, Invocable NF>
class time_single_deferred<SF, NF> {
  SF sf_{};
  NF nf_{};

 public:
  using sender_category = single_tag;

  constexpr time_single_deferred() = default;
  constexpr explicit time_single_deferred(SF sf)
      : sf_(std::move(sf)) {}
  constexpr time_single_deferred(SF sf, NF nf)
      : sf_(std::move(sf)), nf_(std::move(nf)) {}
  auto now() {
    return nf_();
  }
  template <Regular TP, Receiver<single_tag> Out>
    requires Invocable<SF&, TP, Out>
  void submit(TP tp, Out out) {
    sf_(std::move(tp), std::move(out));
  }
};

template <TimeSender<single_tag> Data, class DSF, Invocable<Data&> DNF>
class time_single_deferred<Data, DSF, DNF> {
  Data data_{};
  DSF sf_{};
  DNF nf_{};

 public:
  using sender_category = single_tag;

  constexpr time_single_deferred() = default;
  constexpr explicit time_single_deferred(Data data)
      : data_(std::move(data)) {}
  constexpr time_single_deferred(Data data, DSF sf, DNF nf = DNF{})
      : data_(std::move(data)), sf_(std::move(sf)), nf_(std::move(nf)) {}
  auto now() {
    return nf_(data_);
  }

  template <class TP, Receiver<single_tag> Out>
    requires Invocable<DSF&, Data&, TP, Out>
  void submit(TP tp, Out out) {
    sf_(data_, std::move(tp), std::move(out));
  }
};

time_single_deferred()->time_single_deferred<ignoreSF, systemNowF>;

template <class SF>
time_single_deferred(SF)->time_single_deferred<SF, systemNowF>;

template <class SF, Invocable NF>
time_single_deferred(SF, NF)->time_single_deferred<SF, NF>;

template <TimeSender<single_tag> Data, class DSF>
time_single_deferred(Data, DSF)->time_single_deferred<Data, DSF, passDNF>;

template <TimeSender<single_tag> Data, class DSF, class DNF>
time_single_deferred(Data, DSF, DNF)->time_single_deferred<Data, DSF, DNF>;

template <
    class V,
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
using any_time_single_deferred = time_single_deferred<V, E, TP>;

// template <
//     class V,
//     class E = std::exception_ptr,
//     class TP = std::chrono::system_clock::time_point,
//     TimeSenderTo<single<V, E>, single_tag> Wrapped>
// auto erase_cast(Wrapped w) {
//   return time_single_deferred<V, E>{std::move(w)};
// }

} // namespace pushmi
