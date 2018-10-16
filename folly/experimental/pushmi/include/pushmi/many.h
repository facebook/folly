#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <future>
#include "none.h"

namespace pushmi {

template <class V, class E>
class many<V, E> {
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
    static void s_rnext(data&, V&&) {}
    static void s_lnext(data&, V&) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*rnext_)(data&, V&&) = vtable::s_rnext;
    void (*lnext_)(data&, V&) = vtable::s_lnext;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, many>::value, U>;
  template <class Wrapped>
  static void check() {
    static_assert(Invocable<decltype(::pushmi::set_next), Wrapped, V>,
      "Wrapped many must support nexts of type V");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, std::exception_ptr>,
      "Wrapped many must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped many must support E and be noexcept");
  }
  template<class Wrapped>
  many(Wrapped obj, std::false_type) : many() {
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
      static void rnext(data& src, V&& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), (V&&) v);
      }
      static void lnext(data& src, V& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rnext, s::lnext};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template<class Wrapped>
  many(Wrapped obj, std::true_type) noexcept : many() {
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
      static void rnext(data& src, V&& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>((void*)src.buffer_), (V&&) v);
      }
      static void lnext(data& src, V& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>((void*)src.buffer_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rnext, s::lnext};
    new ((void*)data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
public:
  using properties = property_set<is_receiver<>, is_many<>>;

  many() = default;
  many(many&& that) noexcept : many() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires ManyReceiver<wrapped_t<Wrapped>, V, E>)
  explicit many(Wrapped obj) noexcept(insitu<Wrapped>())
    : many{std::move(obj), bool_<insitu<Wrapped>()>{}} {
    check<Wrapped>();
  }
  ~many() {
    vptr_->op_(data_, nullptr);
  }
  many& operator=(many&& that) noexcept {
    this->~many();
    new ((void*)this) many(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&&, V&&>)
  void next(T&& t) {
    if (!done_) {
      vptr_->rnext_(data_, (T&&) t);
    }
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&, V&>)
  void next(T& t) {
    if (!done_) {
      vptr_->lnext_(data_, t);
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
constexpr typename many<V, E>::vtable const many<V, E>::noop_;

template <class NF, class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class many<NF, EF, DF> {
  bool done_ = false;
  NF nf_;
  EF ef_;
  DF df_;

  static_assert(
      !detail::is_v<NF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");
  static_assert(NothrowInvocable<EF&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");
 public:
  using properties = property_set<is_receiver<>, is_many<>>;

  many() = default;
  constexpr explicit many(NF nf) : many(std::move(nf), EF{}, DF{}) {}
  constexpr explicit many(EF ef) : many(NF{}, std::move(ef), DF{}) {}
  constexpr explicit many(DF df) : many(NF{}, EF{}, std::move(df)) {}
  constexpr many(EF ef, DF df)
      : done_(false), nf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr many(NF nf, EF ef, DF df = DF{})
      : done_(false), nf_(std::move(nf)), ef_(std::move(ef)), df_(std::move(df))
  {}

  PUSHMI_TEMPLATE (class V)
    (requires Invocable<NF&, V>)
  void next(V&& v) {
    if (done_) {return;}
    nf_((V&&) v);
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

template <PUSHMI_TYPE_CONSTRAINT(Receiver) Data, class DNF, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class many<Data, DNF, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DNF nf_;
  DEF ef_;
  DDF df_;

  static_assert(
      !detail::is_v<DNF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

 public:
  using properties = property_set<is_receiver<>, is_many<>>;

  constexpr explicit many(Data d)
      : many(std::move(d), DNF{}, DEF{}, DDF{}) {}
  constexpr many(Data d, DDF df)
      : done_(false), data_(std::move(d)), nf_(), ef_(), df_(df) {}
  constexpr many(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), nf_(), ef_(ef), df_(df) {}
  constexpr many(Data d, DNF nf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), nf_(nf), ef_(ef), df_(df) {}

  PUSHMI_TEMPLATE(class V)
    (requires Invocable<DNF&, Data&, V>)
  void next(V&& v) {
    if (!done_) {
      nf_(data_, (V&&) v);
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
class many<>
    : public many<ignoreNF, abortEF, ignoreDF> {
public:
  many() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_many
PUSHMI_INLINE_VAR constexpr struct make_many_fn {
  inline auto operator()() const {
    return many<>{};
  }
  PUSHMI_TEMPLATE(class NF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
  auto operator()(NF nf) const {
    return many<NF, abortEF, ignoreDF>{std::move(nf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return many<ignoreNF, on_error_fn<EFN...>, ignoreDF>{std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return many<ignoreNF, abortEF, DF>{std::move(df)};
  }
  PUSHMI_TEMPLATE(class NF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(NF nf, EF ef) const {
    return many<NF, EF, ignoreDF>{std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return many<ignoreNF, EF, DF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class NF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df) const {
    return many<NF, EF, DF>{std::move(nf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>>))
  auto operator()(Data d) const {
    return many<Data, passDNXF, passDEF, passDDF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
  auto operator()(Data d, DNF nf) const {
    return many<Data, DNF, passDEF, passDDF>{std::move(d), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return many<Data, passDNXF, on_error_fn<DEFN...>, passDDF>{std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return many<Data, passDNXF, passDEF, DDF>{std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DNF nf, DEF ef) const {
    return many<Data, DNF, DEF, passDDF>{std::move(d), std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return many<Data, passDNXF, DEF, DDF>{std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df) const {
    return many<Data, DNF, DEF, DDF>{std::move(d), std::move(nf), std::move(ef), std::move(df)};
  }
} const make_many {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
many() -> many<>;

PUSHMI_TEMPLATE(class NF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
many(NF) -> many<NF, abortEF, ignoreDF>;

template <class... EFN>
many(on_error_fn<EFN...>) -> many<ignoreNF, on_error_fn<EFN...>, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
many(DF) -> many<ignoreNF, abortEF, DF>;

PUSHMI_TEMPLATE(class NF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
many(NF, EF) -> many<NF, EF, ignoreDF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
many(EF, DF) -> many<ignoreNF, EF, DF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
many(NF, EF, DF) -> many<NF, EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>>))
many(Data d) -> many<Data, passDNXF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DNF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
many(Data d, DNF nf) -> many<Data, DNF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>>))
many(Data d, on_error_fn<DEFN...>) ->
    many<Data, passDNXF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DDF) -> many<Data, passDNXF, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
many(Data d, DNF nf, DEF ef) -> many<Data, DNF, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DEF, DDF) -> many<Data, passDNXF, DEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DNF nf, DEF ef, DDF df) -> many<Data, DNF, DEF, DDF>;
#endif

template <class V, class E = std::exception_ptr>
using any_many = many<V, E>;

template<>
struct construct_deduced<many> : make_many_fn {};

// template <class V, class E = std::exception_ptr, class Wrapped>
//     requires ManyReceiver<Wrapped, V, E> && !detail::is_v<Wrapped, none>
// auto erase_cast(Wrapped w) {
//   return many<V, E>{std::move(w)};
// }

} // namespace pushmi
