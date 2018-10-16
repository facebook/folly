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
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_submit(data&, any_none<E>) {}
    void (*op_)(data&, data*) = s_op;
    void (*submit_)(data&, any_none<E>) = s_submit;
  };
  PUSHMI_DECLARE_CONSTEXPR_IN_CLASS_INIT(static vtable const noop_);
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  deferred(Wrapped obj, std::false_type) : deferred() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void submit(data& src, any_none<E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  deferred(Wrapped obj, std::true_type) noexcept : deferred() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void submit(data& src, any_none<E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, deferred>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_none<>>;

  deferred() = default;
  deferred(deferred&& that) noexcept : deferred() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires SenderTo<wrapped_t<Wrapped>, any_none<E>, is_none<>>)
  explicit deferred(Wrapped obj) noexcept(insitu<Wrapped>())
    : deferred{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
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
PUSHMI_DEFINE_CONSTEXPR_IN_CLASS_INIT(typename deferred<detail::erase_deferred_t, E>::vtable const
    deferred<detail::erase_deferred_t, E>::noop_);

template <class SF>
class deferred<SF> {
  SF sf_;

 public:
  using properties = property_set<is_sender<>, is_none<>>;

  constexpr deferred() = default;
  constexpr explicit deferred(SF sf) : sf_(std::move(sf)) {}
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_none<>>) Data, class DSF>
class deferred<Data, DSF> {
  Data data_;
  DSF sf_;
  static_assert(Sender<Data, is_none<>>, "The Data template parameter "
    "must satisfy the Sender concept.");

 public:
  using properties = property_set<is_sender<>, is_none<>>;

  constexpr deferred() = default;
  constexpr explicit deferred(Data data)
      : data_(std::move(data)) {}
  constexpr deferred(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<DSF&, Data&, Out>)
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_deferred
inline auto make_deferred() -> deferred<ignoreSF> {
  return deferred<ignoreSF>{};
}
template <class SF>
auto make_deferred(SF sf) -> deferred<SF> {
  return deferred<SF>{std::move(sf)};
}
PUSHMI_TEMPLATE(class Wrapped)
  (requires Sender<Wrapped, is_none<>>)
auto make_deferred(Wrapped w) ->
    deferred<detail::erase_deferred_t, std::exception_ptr> {
  return deferred<detail::erase_deferred_t, std::exception_ptr>{std::move(w)};
}
PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_none<>>)
auto make_deferred(Data data, DSF sf) -> deferred<Data, DSF> {
  return deferred<Data, DSF>{std::move(data), std::move(sf)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
deferred() -> deferred<ignoreSF>;

template <class SF>
deferred(SF) -> deferred<SF>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires Sender<Wrapped, is_none<>>)
deferred(Wrapped) ->
    deferred<detail::erase_deferred_t, std::exception_ptr>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_none<>>)
deferred(Data, DSF) -> deferred<Data, DSF>;
#endif

template <class E = std::exception_ptr>
using any_deferred = deferred<detail::erase_deferred_t, E>;

// template <SenderTo<any_none<std::exception_ptr>, is_none<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return deferred<detail::erase_deferred_t, std::exception_ptr>{std::move(w)};
// }
//
// template <class E, SenderTo<any_none<E>, is_none<>> Wrapped>
//   requires Same<is_none<>, properties_t<Wrapped>>
// auto erase_cast(Wrapped w) {
//   return deferred<detail::erase_deferred_t, E>{std::move(w)};
// }

} // namespace pushmi
