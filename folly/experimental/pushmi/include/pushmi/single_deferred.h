#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "single.h"

namespace pushmi {

template <class V, class E>
class single_deferred<V, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible_v<Wrapped>;
  }
  enum struct op { destroy, move };
  struct vtable {
    void (*op_)(op, data&, data*) = +[](op, data&, data*) {};
    void (*submit_)(data&, single<V, E>) = +[](data&, single<V, E>) {};
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
      +[](data& src, single<V, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }};

  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, single_deferred>, U>;

 public:
  using sender_category = single_tag;

  single_deferred() = default;
  single_deferred(single_deferred&& that) noexcept : single_deferred() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }

  template <class Wrapped>
    requires SenderTo<wrapped_t<Wrapped>, single<V, E>, single_tag>
  explicit single_deferred(Wrapped obj)
      : single_deferred() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~single_deferred() {
    vptr_->op_(op::destroy, data_, nullptr);
  }
  single_deferred& operator=(single_deferred&& that) noexcept {
    this->~single_deferred();
    new ((void*)this) single_deferred(std::move(that));
    return *this;
  }
  void submit(single<V, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class E>
constexpr typename single_deferred<V, E>::vtable const
    single_deferred<V, E>::vtable::noop_;
template <class V, class E>
template <class Wrapped, bool Big>
constexpr typename single_deferred<V, E>::vtable const
    single_deferred<V, E>::vtable_v;
template <class V, class E>
template <class Wrapped>
constexpr typename single_deferred<V, E>::vtable const
    single_deferred<V, E>::vtable_v<Wrapped, true> = {
        +[](op o, data& src, data* dst) {
          switch (o) {
            case op::move:
              new (dst->buffer_) Wrapped(
                  std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
            case op::destroy:
              static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
          }
        },
        +[](data& src, single<V, E> out) {
          ::pushmi::submit(
              *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
        }
    };

template <class SF>
class single_deferred<SF> {
  SF sf_;

 public:
  using sender_category = single_tag;

  constexpr single_deferred() = default;
  constexpr explicit single_deferred(SF sf)
      : sf_(std::move(sf)) {}

  template <Receiver<single_tag> Out>
    requires Invocable<SF&, Out>
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <Sender<single_tag> Data, class DSF>
class single_deferred<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using sender_category = single_tag;

  constexpr single_deferred() = default;
  constexpr explicit single_deferred(Data data)
      : data_(std::move(data)) {}
  constexpr single_deferred(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  template <Receiver<single_tag> Out>
    requires Invocable<DSF&, Data&, Out>
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

single_deferred() -> single_deferred<ignoreSF>;

template <class SF>
single_deferred(SF) -> single_deferred<SF>;

template <Sender<single_tag> Data>
single_deferred(Data) -> single_deferred<Data, passDSF>;

template <Sender<single_tag> Data, class DSF>
single_deferred(Data, DSF) -> single_deferred<Data, DSF>;

template <class V, class E = std::exception_ptr>
using any_single_deferred = single_deferred<V, E>;

// template <
//     class V,
//     class E = std::exception_ptr,
//     SenderTo<single<V, E>, single_tag> Wrapped>
// auto erase_cast(Wrapped w) {
//   return single_deferred<V, E>{std::move(w)};
// }

} // namespace pushmi
