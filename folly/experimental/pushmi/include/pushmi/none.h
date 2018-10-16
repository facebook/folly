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
  struct vtable {
    void (*op_)(data&, data*) = +[](data&, data*) {};
    void (*done_)(data&) = +[](data&) {};
    void (*error_)(data&, E) noexcept = +[](data&, E) noexcept {
      std::terminate();
    };
    static constexpr vtable const noop_ = {};
  } const* vptr_ = &vtable::noop_;
  template <class Wrapped, bool = insitu<Wrapped>()>
  static constexpr vtable const vtable_v = {
      +[](data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
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
    that.vptr_->op_(that.data_, &data_);
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
    vptr_->op_(data_, nullptr);
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
    +[](data& src, data* dst) {
      if (dst)
        new (dst->buffer_)
            Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
      static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
    },
    +[](data& src) {
      ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
    },
    +[](data& src, E e) noexcept {::pushmi::set_error(
        *static_cast<Wrapped*>((void*)src.buffer_),
        std::move(e));
    }
};

template <class EF, class DF>
  requires Invocable<DF&>
class none<EF, DF> {
  static_assert(!detail::is_v<EF, on_value> && !detail::is_v<EF, single>);
  bool done_ = false;
  EF ef_{};
  DF df_{};

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
    static_assert(
        noexcept(ef_(std::move(e))),
        "error function must be noexcept");
    if (!done_) {
      done_ = true;
      ef_(std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      df_();
    }
  }
};

template <Receiver<none_tag> Data, class DEF, class DDF>
  requires Invocable<DDF&, Data&>
class none<Data, DEF, DDF> {
  bool done_ = false;
  Data data_{};
  DEF ef_{};
  DDF df_{};
  static_assert(!detail::is_v<DEF, on_value>);
  static_assert(!detail::is_v<Data, single>);
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
      : done_(false), data_(std::move(d)), ef_(std::move(ef)),
        df_(std::move(df)) {}
  template <class E>
    requires Invocable<DEF&, Data&, E>
  void error(E e) noexcept {
    static_assert(
        noexcept(ef_(data_, std::move(e))), "error function must be noexcept");
    if (!done_) {
      done_ = true;
      ef_(data_, std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      df_(data_);
    }
  }
};

template <>
class none<>
    : public none<abortEF, ignoreDF> {
};

none() -> none<>;

template <class EF>
none(EF) -> none<EF, ignoreDF>;

template <class DF>
  requires Invocable<DF&>
none(DF)->none<abortEF, DF>;

template <class EF, class DF>
  requires Invocable<DF&>
none(EF, DF)->none<EF, DF>;

template <Receiver<none_tag> Data>
  requires !Receiver<Data, single_tag>
none(Data) -> none<Data, passDEF, passDDF>;

template <Receiver<none_tag> Data, class DEF>
  requires !Receiver<Data, single_tag>
none(Data, DEF) -> none<Data, DEF, passDDF>;

template <Receiver<none_tag> Data, class DDF>
  requires Invocable<DDF&, Data&> && !Receiver<Data, single_tag>
none(Data, DDF) -> none<Data, passDEF, DDF>;

template <Receiver<none_tag> Data, class DEF, class DDF>
  requires !Receiver<Data, single_tag>
none(Data, DEF, DDF) -> none<Data, DEF, DDF>;

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

template <SenderTo<std::promise<void>, none_tag> Out>
std::future<void> future_from(Out out) {
  std::promise<void> p;
  auto result = p.get_future();
  submit(out, std::move(p));
  return result;
}

} // namespace pushmi
