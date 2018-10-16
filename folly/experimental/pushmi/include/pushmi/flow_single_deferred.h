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
        std::is_nothrow_move_constructible_v<Wrapped>;
  }
  enum struct op { destroy, move };
  struct vtable {
    void (*op_)(op, data&, data*) = +[](op, data&, data*) {};
    void (*submit_)(data&, flow_single<V, PE, E>) =
        +[](data&, flow_single<V, PE, E>) {};
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
      +[](data& src, flow_single<V, PE, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
  };

  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, flow_single_deferred>, U>;

 public:
  using sender_category = flow_tag;

  flow_single_deferred() = default;
  flow_single_deferred(flow_single_deferred&& that) noexcept
      : flow_single_deferred() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class Wrapped>
      requires FlowSingleSender<
          wrapped_t<Wrapped>,
          flow_single<V, PE, E>,
          any_none<PE>,
          V,
          E>
  explicit flow_single_deferred(Wrapped obj)
      : flow_single_deferred() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~flow_single_deferred() {
    vptr_->op_(op::destroy, data_, nullptr);
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
template <class V, class PE, class E>
template <class Wrapped, bool Big>
constexpr typename flow_single_deferred<V, PE, E>::vtable const
    flow_single_deferred<V, PE, E>::vtable_v;
template <class V, class PE, class E>
template <class Wrapped>
constexpr typename flow_single_deferred<V, PE, E>::vtable const
    flow_single_deferred<V, PE, E>::vtable_v<Wrapped, true> = {
        +[](op o, data& src, data* dst) {
          switch (o) {
            case op::move:
              new (dst->buffer_) Wrapped(
                  std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
            case op::destroy:
              static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
          }
        },
        +[](data& src, flow_single<V, PE, E> out) {
          ::pushmi::submit(
              *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
        }
    };

template <class SF>
class flow_single_deferred<SF> {
  SF sf_;

 public:
  using sender_category = flow_tag;

  constexpr flow_single_deferred() = default;
  constexpr explicit flow_single_deferred(SF sf)
      : sf_(std::move(sf)) {}

  template <Receiver<flow_tag> Out>
    requires Invocable<SF&, Out>
  void submit(Out out) {
    sf_(std::move(out));
  }
};

flow_single_deferred() -> flow_single_deferred<ignoreSF>;

template <class SF>
flow_single_deferred(SF) -> flow_single_deferred<SF>;

template <class V, class PE = std::exception_ptr, class E = PE>
using any_flow_single_deferred = flow_single_deferred<V, PE, E>;

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_single_deferred<V, E>{std::move(w)};
// }

} // namespace pushmi
