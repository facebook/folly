#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "none.h"

namespace pushmi {
namespace detail {
struct erase_deferred_t {};
} // namespace detail

template <class E>
class deferred<detail::erase_deferred_t, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<void>)]; // can hold a void promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible_v<Wrapped>;
  }
  struct vtable {
    void (*op_)(data&, data*) = +[](data&, data*) {};
    void (*submit_)(data&, any_none<E>) = +[](data&, any_none<E>) {};
    static constexpr vtable const noop_ = {};
  } const* vptr_ = &vtable::noop_;
  template <class Wrapped, bool = insitu<Wrapped>()>
  static constexpr vtable const vtable_v = {
      +[](data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      },
      +[](data& src, any_none<E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
  };
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, deferred>, U>;
 public:
  using sender_category = none_tag;

  deferred() = default;
  deferred(deferred&& that) noexcept : deferred() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class Wrapped>
      requires SenderTo<wrapped_t<Wrapped>, any_none<E>, none_tag>
  explicit deferred(Wrapped obj)
      : deferred() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~deferred() {
    vptr_->op_(data_, nullptr);
  }
  deferred& operator=(deferred&& that) noexcept {
    this->~deferred();
    new ((void*)this) deferred(std::move(that));
    return *this;
  }
  void submit(any_none<E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class E>
constexpr typename deferred<detail::erase_deferred_t, E>::vtable const
    deferred<detail::erase_deferred_t, E>::vtable::noop_;
template <class E>
template <class Wrapped, bool Big>
constexpr typename deferred<detail::erase_deferred_t, E>::vtable const
    deferred<detail::erase_deferred_t, E>::vtable_v;
template <class E>
template <class Wrapped>
constexpr typename deferred<detail::erase_deferred_t, E>::vtable const
    deferred<detail::erase_deferred_t, E>::vtable_v<Wrapped, true> = {
        +[](data& src, data* dst) {
          if (dst)
            new (dst->buffer_) Wrapped(
                std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
          static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
        },
        +[](data& src, any_none<E> out) {
          ::pushmi::submit(
              *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
        }
    };

template <class SF>
class deferred<SF> {
  SF sf_;

 public:
  using sender_category = none_tag;

  constexpr deferred() = default;
  constexpr explicit deferred(SF sf) : sf_(std::move(sf)) {}
  template <Receiver<none_tag> Out>
    requires Invocable<SF&, Out>
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <Sender<none_tag> Data, class DSF>
class deferred<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using sender_category = none_tag;

  constexpr deferred() = default;
  constexpr explicit deferred(Data data)
      : data_(std::move(data)) {}
  constexpr deferred(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}

  template <Receiver<none_tag> Out>
    requires Invocable<DSF&, Data&, Out>
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

deferred() -> deferred<ignoreSF>;

template <class SF>
deferred(SF) -> deferred<SF>;

template <Sender<none_tag> Wrapped>
deferred(Wrapped) ->
    deferred<detail::erase_deferred_t, std::exception_ptr>;

template <Sender<none_tag> Data, class DSF>
deferred(Data, DSF) -> deferred<Data, DSF>;

template <class E = std::exception_ptr>
using any_deferred = deferred<detail::erase_deferred_t, E>;

// template <SenderTo<any_none<std::exception_ptr>, none_tag> Wrapped>
// auto erase_cast(Wrapped w) {
//   return deferred<detail::erase_deferred_t, std::exception_ptr>{std::move(w)};
// }
//
// template <class E, SenderTo<any_none<E>, none_tag> Wrapped>
//   requires Same<none_tag, sender_category_t<Wrapped>>
// auto erase_cast(Wrapped w) {
//   return deferred<detail::erase_deferred_t, E>{std::move(w)};
// }

} // namespace pushmi
