#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "none.h"

namespace pushmi {

namespace detail {

template<class data, class op, class V, class E>
struct single_vtable {
  void (*op_)(op, data&, data*) = +[](op, data&, data*) {};
  void (*done_)(data&) = +[](data&) {};
  void (*error_)(data&, E) noexcept = +[](data&, E) noexcept {
    std::terminate();
  };
  void (*rvalue_)(data&, V&&) = +[](data&, V&&) {};
  void (*lvalue_)(data&, V&) = +[](data&, V&) {};
  static constexpr single_vtable const noop_ = {};
};

template <class data, class op, class V, class E, class Wrapped, bool insitu>
struct single_vtable_v;

template <class data, class op, class V, class E, class Wrapped>
struct single_vtable_v<data, op, V, E, Wrapped, false> {
  static constexpr single_vtable<data, op, V, E> const vtable_v = {
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
      +[](data& src, V&& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), (V&&) v);
      },
      +[](data& src, V& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), v);
      }
  };
};

template <class data, class op, class V, class E, class Wrapped>
struct single_vtable_v<data, op, V, E, Wrapped, true> {
  static constexpr single_vtable<data, op, V, E> const vtable_v = {
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
    +[](data& src, E e) noexcept {
      ::pushmi::set_error(
        *static_cast<Wrapped*>((void*)src.buffer_),
        std::move(e));
    },
    +[](data& src, V&& v) {
      ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), (V&&) v);
    },
    +[](data& src, V& v) {
      ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), v);
    }
  };
};

// Class static definitions:

template<class data, class op, class V, class E>
constexpr single_vtable<data, op, V, E> const single_vtable<data, op, V, E>::noop_;

} // namespace detail

template <class V, class E>
class single<V, E> {
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
  using vtable = detail::single_vtable<data, op, V, E>;
  vtable const* vptr_ = &vtable::noop_;
public:
  using receiver_category = single_tag;

  single() = default;
  single(single&& that) noexcept : single() {
    that.vptr_->op_(op::move, that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same_v<U, single>, U>;
  template <class Wrapped>
   requires SingleReceiver<wrapped_t<Wrapped>, V, E>
  explicit single(Wrapped obj) : single() {
    static_assert(Invocable<decltype(::pushmi::set_value), Wrapped, V>,
      "Wrapped single must support values of type V");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, std::exception_ptr>,
      "Wrapped single must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped single must support E and be noexcept");
    if constexpr (insitu<Wrapped>())
      new (data_.buffer_) Wrapped(std::move(obj));
    else
      data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &detail::single_vtable_v<data, op, V, E, Wrapped, insitu<Wrapped>()>::vtable_v;
  }
  ~single() {
    vptr_->op_(op::destroy, data_, nullptr);
  }
  single& operator=(single&& that) noexcept {
    this->~single();
    new ((void*)this) single(std::move(that));
    return *this;
  }
  template<class T>
  requires ConvertibleTo<T&&, V&&>
  void value(T&& t) {
    if (done_) {return;}
    done_ = true;
    vptr_->rvalue_(data_, (T&&) t);
  }
  template<class T>
  requires ConvertibleTo<T&, V&>
  void value(T& t) {
    if (done_) {return;}
    done_ = true;
    vptr_->lvalue_(data_, t);
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


template <class VF, class EF, class DF>
requires Invocable<DF&> class single<VF, EF, DF> {
  bool done_ = false;
  VF vf_;
  EF ef_;
  DF df_;

 public:
  using receiver_category = single_tag;

  static_assert(
      !detail::is_v<VF, on_error>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(NothrowInvocable<EF, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

  single() = default;
  constexpr explicit single(VF vf) : single(std::move(vf), EF{}, DF{}) {}
  constexpr explicit single(EF ef) : single(VF{}, std::move(ef), DF{}) {}
  constexpr explicit single(DF df) : single(VF{}, EF{}, std::move(df)) {}
  constexpr single(EF ef, DF df)
      : done_(false), vf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr single(VF vf, EF ef, DF df = DF{})
      : done_(false), vf_(std::move(vf)), ef_(std::move(ef)), df_(std::move(df)) {}
  template <class V>
  requires Invocable<VF&, V> void value(V&& v) {
    if (done_) {return;}
    done_ = true;
    vf_((V&&) v);
  }
  template <class E>
  requires Invocable<EF&, E> void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    if (done_) {return;}
    done_ = true;
    ef_(std::move(e));
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    df_();
  }
};

template <Receiver Data, class DVF, class DEF, class DDF>
requires Invocable<DDF&, Data&> class single<Data, DVF, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DVF vf_;
  DEF ef_;
  DDF df_;

 public:
  using receiver_category = single_tag;

  static_assert(
      !detail::is_v<DVF, on_error>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

  template<class... AN>
  constexpr explicit single(std::tuple<AN...> t) : single(std::apply(construct<Data>{}, std::move(t)), DEF{}, DDF{}) {}
  constexpr explicit single(Data d)
      : single(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr single(Data d, DDF df)
      : done_(false), data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr single(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr single(Data d, DVF vf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(vf), ef_(ef), df_(df) {}
  template <class V>
  requires Invocable<DVF&, Data&, V> void value(V&& v) {
    if (done_) {return;}
    done_ = true;
    vf_(data_, (V&&) v);
  }
  template <class E>
  requires Invocable<DEF&, Data&, E> void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    if (done_) {return;}
    done_ = true;
    ef_(data_, std::move(e));
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    df_(data_);
  }
};

template <>
class single<>
    : public single<ignoreVF, abortEF, ignoreDF> {
};

using archetype_single = single<>;

single()->single<>;

template <class... AN>
single(std::tuple<AN...>) ->
    single<decltype(single{std::declval<AN&&>()...}), passDVF, passDEF, passDDF>;

template <class VF>
    requires !Receiver<VF> && !detail::is_v<VF, on_error> &&
    !detail::is_v<VF, on_done>
single(VF) -> single<VF, abortEF, ignoreDF>;

template <class... EFN>
single(on_error<EFN...>) -> single<ignoreVF, on_error<EFN...>, ignoreDF>;

template <class DF>
single(on_done<DF>) -> single<ignoreVF, abortEF, on_done<DF>>;

template <class V, class E, class Wrapped>
  requires SingleReceiver<Wrapped, V, E>
single(Wrapped)->single<V, E>;

template <class VF, class EF>
    requires !Receiver<VF> && !detail::is_v<VF, on_error> &&
    !detail::is_v<VF, on_done> && !detail::is_v<EF, on_value> &&
    !detail::is_v<EF, on_done> && !Invocable<VF&> && !Invocable<EF&>
single(VF, EF) -> single<VF, EF, ignoreDF>;

template <class... EFN, class DF>
single(on_error<EFN...>, on_done<DF>) ->
    single<ignoreVF, on_error<EFN...>, on_done<DF>>;

template <class VF, class EF, class DF>
  requires !Invocable<VF&> && !Invocable<EF&> && Invocable<DF&>
single(VF, EF, DF)->single<VF, EF, DF>;

template <Receiver<single_tag> Data>
single(Data d) -> single<Data, passDVF, passDEF, passDDF>;

template <Receiver<single_tag> Data, class DVF>
    requires !detail::is_v<DVF, on_error> &&
    !detail::is_v<DVF, on_done>
single(Data d, DVF vf)
    ->single<Data, DVF, passDEF, passDDF>;

template <Receiver<single_tag> Data, class... DEFN>
single(Data d, on_error<DEFN...>)
    ->single<Data, passDVF, on_error<DEFN...>, passDDF>;

template <Receiver<single_tag> Data, class DVF, class DEF>
    requires !detail::is_v<DVF, on_error> && !detail::is_v<DVF, on_done> &&
    !detail::is_v<DEF, on_done> && !Invocable<DVF&, Data&> && !Invocable<DEF&, Data&>
single(Data d, DVF vf, DEF ef) -> single<Data, DVF, DEF, passDDF>;

template <Receiver<single_tag> Data, class... DEFN, class DDF>
single(Data d, on_error<DEFN...>, on_done<DDF>) ->
    single<Data, passDVF, on_error<DEFN...>, on_done<DDF>>;

template <Receiver<single_tag> Data, class DDF>
single(Data d, on_done<DDF>)->single<Data, passDVF, passDEF, on_done<DDF>>;

template <Receiver<single_tag> Data, class DVF, class DEF, class DDF>
requires !Invocable<DVF&, Data&> && !Invocable<DEF&, Data&> && Invocable<DDF&, Data&>
single(Data d, DVF vf, DEF ef, DDF df) -> single<Data, DVF, DEF, DDF>;

template <class V, class E = std::exception_ptr>
using any_single = single<V, E>;

// template <class V, class E = std::exception_ptr, class Wrapped>
//     requires SingleReceiver<Wrapped, V, E> && !detail::is_v<Wrapped, none>
// auto erase_cast(Wrapped w) {
//   return single<V, E>{std::move(w)};
// }

template<class T>
std::future<T> future_from(auto singleSender) {
  std::promise<T> p;
  auto result = p.get_future();
  submit(singleSender, single{std::move(p)});
  return result;
}

} // namespace values
