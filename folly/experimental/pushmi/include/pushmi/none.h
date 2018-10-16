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
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); };
    void (*op_)(data&, data*) = s_op;
    void (*done_)(data&) = s_done;
    void (*error_)(data&, E) noexcept = s_error;
  };
  static constexpr vtable const noop_ {};
  vtable  const* vptr_ = &noop_;
  template <class Wrapped>
  none(Wrapped obj, std::false_type) : none() {
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
    };
    static const vtable vtable_v{s::op, s::done, s::error};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v;
  }
  template <class Wrapped>
  none(Wrapped obj, std::true_type) noexcept : none() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, none>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_none<>>;

  none() = default;
  none(none&& that) noexcept : none() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires NoneReceiver<wrapped_t<Wrapped>, E>)
  explicit none(Wrapped obj) noexcept(insitu<Wrapped>())
    : none{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
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
constexpr typename none<E>::vtable const none<E>::noop_;

template <class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class none<EF, DF> {
  static_assert(
    !detail::is_v<EF, on_value_fn>,
    "the first parameter is the error implementation, but on_value{} was passed");
  static_assert(
    !detail::is_v<EF, single>,
    "the first parameter is the error implementation, but a single<> was passed");
  bool done_ = false;
  EF ef_;
  DF df_;

public:
  using properties = property_set<is_receiver<>, is_none<>>;

  none() = default;
  constexpr explicit none(EF ef)
      : none(std::move(ef), DF{}) {}
  constexpr explicit none(DF df)
      : none(EF{}, std::move(df)) {}
  constexpr none(EF ef, DF df)
      : done_(false), ef_(std::move(ef)), df_(std::move(df)) {}

  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
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

template <PUSHMI_TYPE_CONSTRAINT(Receiver<is_none<>>) Data, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class none<Data, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DEF ef_;
  DDF df_;
  static_assert(
    !detail::is_v<DEF, on_value_fn>,
    "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(
    !detail::is_v<Data, single>,
    "none should not be used to wrap a single<>");
public:
  using properties = property_set<is_receiver<>, is_none<>>;

  constexpr explicit none(Data d) : none(std::move(d), DEF{}, DDF{}) {}
  constexpr none(Data d, DDF df)
      : done_(false), data_(std::move(d)), ef_(), df_(std::move(df)) {}
  constexpr none(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), ef_(std::move(ef)),
        df_(std::move(df)) {}
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<DEF&, Data&, E>)
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

////////////////////////////////////////////////////////////////////////////////
// make_flow_single
inline auto make_none() -> none<> {
  return {};
}
PUSHMI_TEMPLATE(class EF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF> PUSHMI_AND not defer::Invocable<EF&>)))
auto make_none(EF ef) -> none<EF, ignoreDF> {
  return none<EF, ignoreDF>{std::move(ef)};
}
PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<DF>)))
auto make_none(DF df) -> none<abortEF, DF> {
  return none<abortEF, DF>{std::move(df)};
}
PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF>)))
auto make_none(EF ef, DF df) -> none<EF, DF> {
  return {std::move(ef), std::move(df)};
}
PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>>))
auto make_none(Data d) -> none<Data, passDEF, passDDF> {
  return none<Data, passDEF, passDDF>{std::move(d)};
}
PUSHMI_TEMPLATE(class Data, class DEF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DEF&, Data&>)))
auto make_none(Data d, DEF ef) -> none<Data, DEF, passDDF> {
  return {std::move(d), std::move(ef)};
}
PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>> PUSHMI_AND
    defer::Invocable<DDF&, Data&>))
auto make_none(Data d, DDF df) -> none<Data, passDEF, DDF> {
  return {std::move(d), std::move(df)};
}
PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>> PUSHMI_AND
    defer::Invocable<DDF&, Data&>))
auto make_none(Data d, DEF ef, DDF df) -> none<Data, DEF, DDF> {
  return {std::move(d), std::move(ef), std::move(df)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
none() -> none<>;

PUSHMI_TEMPLATE(class EF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF> PUSHMI_AND not defer::Invocable<EF&>)))
none(EF) -> none<EF, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<DF>)))
none(DF) -> none<abortEF, DF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF>)))
none(EF, DF) -> none<EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>>))
none(Data) -> none<Data, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DEF&, Data&>)))
none(Data, DEF) -> none<Data, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>> PUSHMI_AND
    defer::Invocable<DDF&, Data&>))
none(Data, DDF) -> none<Data, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_none<>> PUSHMI_AND not defer::Receiver<Data, is_single<>> PUSHMI_AND
    defer::Invocable<DDF&, Data&>))
none(Data, DEF, DDF) -> none<Data, DEF, DDF>;
#endif

template <class E = std::exception_ptr>
using any_none = none<E>;

template<>
struct construct_deduced<none> {
  template<class... AN>
  auto operator()(AN&&... an) const -> decltype(pushmi::make_none((AN&&) an...)) {
    return pushmi::make_none((AN&&) an...);
  }
};

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

PUSHMI_TEMPLATE (class Out)
  (requires SenderTo<Out, std::promise<void>, is_none<>>)
std::future<void> future_from(Out out) {
  std::promise<void> p;
  auto result = p.get_future();
  submit(out, std::move(p));
  return result;
}

} // namespace pushmi
