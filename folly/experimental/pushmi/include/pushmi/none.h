#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "boosters.h"

namespace pushmi {

template <class E>
class none<E> {
  bool done_ = false;
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
      }
  }; // namespace pushmi
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, none>, U>;
public:
  using receiver_category = none_tag;

  none() = default;
  none(none&& that) noexcept : none() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class Wrapped>
      requires NoneReceiver<wrapped_t<Wrapped>, E>
  explicit none(Wrapped obj) : none() {
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v<Wrapped>;
  }
  ~none() {
    vptr_->op_(op::destroy, data_, nullptr);
  }
  none& operator=(none&& that) noexcept {
    this->~none();
    new ((void*)this) none(std::move(that));
    return *this;
  }
  void error(E e) noexcept {
    if (done_) {return;}
    done_ = true;
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    vptr_->done_(data_);
  }
};

// Class static definitions:
template <class E>
constexpr typename none<E>::vtable const none<E>::vtable::noop_;
template <class E>
template <class Wrapped, bool Big>
constexpr typename none<E>::vtable const none<E>::vtable_v;
template <class E>
template <class Wrapped>
constexpr typename none<E>::vtable const none<E>::vtable_v<Wrapped, true> = {
    +[](op o, data& src, data* dst) {
      switch (o) {
        case op::move:
          new (dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
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
}
}
;

template <class EF, class DF>
requires Invocable<DF&> && !detail::is_v<EF, on_value> && !detail::is_v<EF, single>
class none<EF, DF> {
  bool done_ = false;
  EF ef_;
  DF df_;

 public:
  using receiver_category = none_tag;

  // static_assert(
  //     !detail::is_v<EF, on_value>,
  //     "the first parameter is the error implementation, but on_value{} was passed");
  //
  none() = default;
  constexpr explicit none(EF ef)
      : none(std::move(ef), DF{}) {}
  constexpr explicit none(DF df)
      : none(EF{}, std::move(df)) {}
  constexpr none(EF ef, DF df)
      : done_(false), ef_(std::move(ef)), df_(std::move(df)) {}

  template <class E>
    requires Invocable<EF&, E>
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    if (done_) {return;}
    done_ = true;
    ef_(e);
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    df_();
  }
};

template <Receiver Data, class DEF, class DDF>
requires Invocable<DDF&, Data&> && !detail::is_v<DEF, on_value> && !detail::is_v<Data, single>
class none<Data, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DEF ef_;
  DDF df_;

 public:
  using receiver_category = none_tag;

  // static_assert(
  //     !detail::is_v<DEF, on_value>,
  //     "the first parameter is the error implementation, but on_value{} was passed");
  //
  constexpr explicit none(Data d) : none(std::move(d), DEF{}, DDF{}) {}
  constexpr none(Data d, DDF df)
      : done_(false), data_(std::move(d)), ef_(), df_(std::move(df)) {}
  constexpr none(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), ef_(std::move(ef)), df_(std::move(df)) {}
  template <class E>
  requires Invocable<DEF&, Data&, E> void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    if (done_) {return;}
    done_ = true;
    ef_(data_, e);
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    df_(data_);
  }
};

template <> class none<> : public none<abortEF, ignoreDF> {};

using archetype_none = none<>;

none()->archetype_none;

template <class EF>
requires !Receiver<EF> &&
!detail::is_v<EF, single> &&
!detail::is_v<EF, on_value> &&
!detail::is_v<EF, on_done>
none(EF)->none<EF, ignoreDF>;

template <class DF>
requires !Receiver<DF> &&
!detail::is_v<DF, on_value> &&
!detail::is_v<DF, single>
none(on_done<DF>)->none<abortEF, on_done<DF>>;

template <class E, class Wrapped>
requires NoneReceiver<Wrapped, E> &&
!detail::is_v<E, on_value> &&
!detail::is_v<Wrapped, single>
none(Wrapped)->none<E>;

template <class EF, class DF>
requires Invocable<DF&> none(EF, DF)->none<EF, DF>;

template <Receiver Data>
requires !detail::is_v<Data, on_value> &&
!detail::is_v<Data, single>
none(Data)->none<Data, passDEF, passDDF>;

template <Receiver Data, class DEF>
requires !detail::is_v<DEF, on_done> &&
!detail::is_v<Data, on_value> &&
!detail::is_v<Data, single>
none(Data, DEF)->none<Data, DEF, passDDF>;

template <Receiver Data, class DDF>
requires !detail::is_v<Data, on_value> &&
!detail::is_v<Data, single>
none(Data, on_done<DDF>)->none<Data, passDEF, on_done<DDF>>;

template <Receiver Data, class DEF, class DDF>
requires Invocable<DDF&, Data&> &&
!detail::is_v<Data, on_value> &&
!detail::is_v<Data, single>
none(Data, DEF, DDF)->none<Data, DEF, DDF>;

template <class E = std::exception_ptr>
using any_none = none<E>;

// // this is ambiguous because NoneReceiver and SingleReceiver only constrain the done() method.
// // template <class E = std::exception_ptr, NoneReceiver<E> Wrapped>
// // auto erase_cast(Wrapped w) {
// //   return none<erase_cast_t, E>{std::move(w)};
// // }
// template <class E = std::exception_ptr, class... TN>
// auto erase_cast(none<TN...> w) {
//   return none<E>{std::move(w)};
// }
// template <class E = std::exception_ptr>
// auto erase_cast(std::promise<void> w) {
//   return none<E>{std::move(w)};
// }

template <SenderTo<std::promise<void>, none_tag> S>
std::future<void> future_from(S sender) {
  std::promise<void> p;
  auto result = p.get_future();
  submit(sender, std::move(p));
  return result;
}

} // namespace values
