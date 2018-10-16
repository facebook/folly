#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <future>
#include "none.h"

namespace pushmi {

template <class V, class E>
class single<V, E> {
  bool done_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a std::promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() noexcept {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); }
    static void s_rvalue(data&, V&&) {}
    static void s_lvalue(data&, V&) {}
    void (*op_)(data&, data*) = s_op;
    void (*done_)(data&) = s_done;
    void (*error_)(data&, E) noexcept = s_error;
    void (*rvalue_)(data&, V&&) = s_rvalue;
    void (*lvalue_)(data&, V&) = s_lvalue;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, single>::value, U>;
  template <class Wrapped>
  static void check() {
    static_assert(Invocable<decltype(::pushmi::set_value), Wrapped, V>,
      "Wrapped single must support values of type V");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, std::exception_ptr>,
      "Wrapped single must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped single must support E and be noexcept");
  }
  template<class Wrapped>
  single(Wrapped obj, std::false_type) : single() {
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
      static void rvalue(data& src, V&& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), (V&&) v);
      }
      static void lvalue(data& src, V& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rvalue, s::lvalue};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template<class Wrapped>
  single(Wrapped obj, std::true_type) noexcept : single() {
    struct s {
      static void op(data& src, data* dst) {
          if (dst)
            new (dst->buffer_) Wrapped(
                std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
          static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {
        ::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
      static void rvalue(data& src, V&& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), (V&&) v);
      }
      static void lvalue(data& src, V& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rvalue, s::lvalue};
    new ((void*)data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
public:
  using properties = property_set<is_receiver<>, is_single<>>;

  single() = default;
  single(single&& that) noexcept : single() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires SingleReceiver<wrapped_t<Wrapped>, V, E>)
  explicit single(Wrapped obj) noexcept(insitu<Wrapped>())
    : single{std::move(obj), bool_<insitu<Wrapped>()>{}} {
    check<Wrapped>();
  }
  ~single() {
    vptr_->op_(data_, nullptr);
  }
  single& operator=(single&& that) noexcept {
    this->~single();
    new ((void*)this) single(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&&, V&&>)
  void value(T&& t) {
    if (!done_) {
      done_ = true;
      vptr_->rvalue_(data_, (T&&) t);
    }
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&, V&>)
  void value(T& t) {
    if (!done_) {
      done_ = true;
      vptr_->lvalue_(data_, t);
    }
  }
  void error(E e) noexcept {
    if (!done_) {
      done_ = true;
      vptr_->error_(data_, std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      vptr_->done_(data_);
    }
  }
};

// Class static definitions:
template <class V, class E>
constexpr typename single<V, E>::vtable const single<V, E>::noop_;

template <class VF, class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class single<VF, EF, DF> {
  bool done_ = false;
  VF vf_;
  EF ef_;
  DF df_;

  static_assert(
      !detail::is_v<VF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(NothrowInvocable<EF&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");
 public:
  using properties = property_set<is_receiver<>, is_single<>>;

  single() = default;
  constexpr explicit single(VF vf) : single(std::move(vf), EF{}, DF{}) {}
  constexpr explicit single(EF ef) : single(VF{}, std::move(ef), DF{}) {}
  constexpr explicit single(DF df) : single(VF{}, EF{}, std::move(df)) {}
  constexpr single(EF ef, DF df)
      : done_(false), vf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr single(VF vf, EF ef, DF df = DF{})
      : done_(false), vf_(std::move(vf)), ef_(std::move(ef)), df_(std::move(df))
  {}

  PUSHMI_TEMPLATE (class V)
    (requires Invocable<VF&, V>)
  void value(V&& v) {
    if (done_) {return;}
    done_ = true;
    vf_((V&&) v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
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

template <PUSHMI_TYPE_CONSTRAINT(Receiver) Data, class DVF, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class single<Data, DVF, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DVF vf_;
  DEF ef_;
  DDF df_;

  static_assert(
      !detail::is_v<DVF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

 public:
  using properties = property_set<is_receiver<>, is_single<>>;

  constexpr explicit single(Data d)
      : single(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr single(Data d, DDF df)
      : done_(false), data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr single(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr single(Data d, DVF vf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(vf), ef_(ef), df_(df) {}

  PUSHMI_TEMPLATE(class V)
    (requires Invocable<DVF&, Data&, V>)
  void value(V&& v) {
    if (!done_) {
      done_ = true;
      vf_(data_, (V&&) v);
    }
  }
  PUSHMI_TEMPLATE(class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
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
class single<>
    : public single<ignoreVF, abortEF, ignoreDF> {
public:
  single() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_single
inline auto make_single() -> single<> {
  return {};
}
PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF> PUSHMI_AND not defer::Invocable<VF&>)))
auto make_single(VF vf) -> single<VF, abortEF, ignoreDF> {
  return single<VF, abortEF, ignoreDF>{std::move(vf)};
}
template <class... EFN>
auto make_single(on_error_fn<EFN...> ef) -> single<ignoreVF, on_error_fn<EFN...>, ignoreDF> {
  return single<ignoreVF, on_error_fn<EFN...>, ignoreDF>{std::move(ef)};
}
PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<DF>)))
auto make_single(DF df) -> single<ignoreVF, abortEF, DF> {
  return single<ignoreVF, abortEF, DF>{std::move(df)};
}
PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF> PUSHMI_AND not defer::Invocable<EF&>)))
auto make_single(VF vf, EF ef) -> single<VF, EF, ignoreDF> {
  return {std::move(vf), std::move(ef)};
}
PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF>)))
auto make_single(EF ef, DF df) -> single<ignoreVF, EF, DF> {
  return {std::move(ef), std::move(df)};
}
PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF>)))
auto make_single(VF vf, EF ef, DF df) -> single<VF, EF, DF> {
  return {std::move(vf), std::move(ef), std::move(df)};
}
PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>>))
auto make_single(Data d) -> single<Data, passDVF, passDEF, passDDF> {
  return single<Data, passDVF, passDEF, passDDF>{std::move(d)};
}
PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DVF&, Data&>)))
auto make_single(Data d, DVF vf) -> single<Data, DVF, passDEF, passDDF> {
  return {std::move(d), std::move(vf)};
}
PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>>))
auto make_single(Data d, on_error_fn<DEFN...> ef) ->
    single<Data, passDVF, on_error_fn<DEFN...>, passDDF> {
  return {std::move(d), std::move(ef)};
}
PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
auto make_single(Data d, DDF df) -> single<Data, passDVF, passDEF, DDF> {
  return {std::move(d), std::move(df)};
}
PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DEF&, Data&>)))
auto make_single(Data d, DVF vf, DEF ef) -> single<Data, DVF, DEF, passDDF> {
  return {std::move(d), std::move(vf), std::move(ef)};
}
PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
auto make_single(Data d, DEF ef, DDF df) -> single<Data, passDVF, DEF, DDF> {
  return {std::move(d), std::move(ef), std::move(df)};
}
PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
auto make_single(Data d, DVF vf, DEF ef, DDF df) -> single<Data, DVF, DEF, DDF> {
  return {std::move(d), std::move(vf), std::move(ef), std::move(df)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
single() -> single<>;

PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF> PUSHMI_AND not defer::Invocable<VF&>)))
single(VF) -> single<VF, abortEF, ignoreDF>;

template <class... EFN>
single(on_error_fn<EFN...>) -> single<ignoreVF, on_error_fn<EFN...>, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<DF>)))
single(DF) -> single<ignoreVF, abortEF, DF>;

PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF> PUSHMI_AND not defer::Invocable<EF&>)))
single(VF, EF) -> single<VF, EF, ignoreDF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<EF>)))
single(EF, DF) -> single<ignoreVF, EF, DF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(defer::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Receiver<VF>)))
single(VF, EF, DF) -> single<VF, EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>>))
single(Data d) -> single<Data, passDVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DVF&, Data&>)))
single(Data d, DVF vf) -> single<Data, DVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>>))
single(Data d, on_error_fn<DEFN...>) ->
    single<Data, passDVF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(defer::True<> PUSHMI_AND defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
single(Data d, DDF) -> single<Data, passDVF, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not defer::Invocable<DEF&, Data&>)))
single(Data d, DVF vf, DEF ef) -> single<Data, DVF, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
single(Data d, DEF, DDF) -> single<Data, passDVF, DEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(defer::Receiver<Data, is_single<>> PUSHMI_AND defer::Invocable<DDF&, Data&>))
single(Data d, DVF vf, DEF ef, DDF df) -> single<Data, DVF, DEF, DDF>;
#endif

template <class V, class E = std::exception_ptr>
using any_single = single<V, E>;

template<>
struct construct_deduced<single> {
  template<class... AN>
  auto operator()(AN&&... an) const -> decltype(pushmi::make_single((AN&&) an...)) {
    return pushmi::make_single((AN&&) an...);
  }
};

// template <class V, class E = std::exception_ptr, class Wrapped>
//     requires SingleReceiver<Wrapped, V, E> && !detail::is_v<Wrapped, none>
// auto erase_cast(Wrapped w) {
//   return single<V, E>{std::move(w)};
// }

PUSHMI_TEMPLATE (class T, class Out)
  (requires SenderTo<Out, std::promise<T>, is_none<>>)
std::future<T> future_from(Out singleSender) {
  std::promise<T> p;
  auto result = p.get_future();
  submit(singleSender, std::move(p));
  return result;
}

} // namespace pushmi
