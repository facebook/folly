#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "single.h"

namespace pushmi {

template <
    class V,
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
class any_time_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static TP s_now(data&) { return TP{}; }
    static void s_submit(data&, TP, single<V, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    TP (*now_)(data&) = vtable::s_now;
    void (*submit_)(data&, TP, single<V, E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_time_single_sender(Wrapped obj, std::false_type)
    : any_time_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static TP now(data& src) {
        return ::pushmi::now(*static_cast<Wrapped*>(src.pobj_));
      }
      static void submit(data& src, TP at, single<V, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>(src.pobj_),
            std::move(at),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::now, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_time_single_sender(Wrapped obj, std::true_type) noexcept
    : any_time_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static TP now(data& src) {
        return ::pushmi::now(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void submit(data& src, TP tp, single<V, E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(tp),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::now, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_time_single_sender>::value, U>;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  any_time_single_sender() = default;
  any_time_single_sender(any_time_single_sender&& that) noexcept
      : any_time_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires TimeSenderTo<wrapped_t<Wrapped>, single<V, E>>)
  explicit any_time_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
  : any_time_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {
  }
  ~any_time_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_time_single_sender& operator=(any_time_single_sender&& that) noexcept {
    this->~any_time_single_sender();
    new ((void*)this) any_time_single_sender(std::move(that));
    return *this;
  }
  TP now() {
    vptr_->now_(data_);
  }
  void submit(TP at, single<V, E> out) {
    vptr_->submit_(data_, std::move(at), std::move(out));
  }
};

// Class static definitions:
template <class V, class E, class TP>
constexpr typename any_time_single_sender<V, E, TP>::vtable const
    any_time_single_sender<V, E, TP>::noop_;

template <class SF, class NF>
#if __cpp_concepts
  requires Invocable<NF&>
#endif
class time_single_sender<SF, NF> {
  SF sf_;
  NF nf_;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  constexpr time_single_sender() = default;
  constexpr explicit time_single_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr time_single_sender(SF sf, NF nf)
      : sf_(std::move(sf)), nf_(std::move(nf)) {}
  auto now() {
    return nf_();
  }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out, is_single<>> &&
      Invocable<SF&, TP, Out>)
  void submit(TP tp, Out out) {
    sf_(std::move(tp), std::move(out));
  }
};

namespace detail {
template <PUSHMI_TYPE_CONSTRAINT(TimeSender<is_single<>>) Data, class DSF, class DNF>
#if __cpp_concepts
  requires Invocable<DNF&, Data&>
#endif
class time_single_sender_2 {
  Data data_;
  DSF sf_;
  DNF nf_;

 public:
  using properties = property_set<is_time<>, is_single<>>;

  constexpr time_single_sender_2() = default;
  constexpr explicit time_single_sender_2(Data data)
      : data_(std::move(data)) {}
  constexpr time_single_sender_2(Data data, DSF sf, DNF nf = DNF{})
      : data_(std::move(data)), sf_(std::move(sf)), nf_(std::move(nf)) {}
  auto now() {
    return nf_(data_);
  }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out, is_single<>> &&
      Invocable<DSF&, Data&, TP, Out>)
  void submit(TP tp, Out out) {
    sf_(data_, std::move(tp), std::move(out));
  }
};

template <class A, class B, class C>
using time_single_sender_base =
  std::conditional_t<
    (bool)TimeSender<A, is_single<>>,
    time_single_sender_2<A, B, C>,
    any_time_single_sender<A, B, C>>;
} // namespace detail

template <class A, class B, class C>
struct time_single_sender<A, B, C>
  : detail::time_single_sender_base<A, B, C> {
  constexpr time_single_sender() = default;
  using detail::time_single_sender_base<A, B, C>::time_single_sender_base;
};

////////////////////////////////////////////////////////////////////////////////
// make_time_single_sender
PUSHMI_INLINE_VAR constexpr struct make_time_single_sender_fn {
  inline auto operator()() const  {
    return time_single_sender<ignoreSF, systemNowF>{};
  }
  template <class SF>
  auto operator()(SF sf) const {
    return time_single_sender<SF, systemNowF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE (class SF, class NF)
    (requires Invocable<NF&>)
  auto operator()(SF sf, NF nf) const {
    return time_single_sender<SF, NF>{std::move(sf), std::move(nf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF)
    (requires TimeSender<Data, is_single<>>)
  auto operator()(Data d, DSF sf) const {
    return time_single_sender<Data, DSF, passDNF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DNF)
    (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&>)
  auto operator()(Data d, DSF sf, DNF nf) const  {
    return time_single_sender<Data, DSF, DNF>{std::move(d), std::move(sf),
      std::move(nf)};
  }
} const make_time_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
time_single_sender() -> time_single_sender<ignoreSF, systemNowF>;

template <class SF>
time_single_sender(SF) -> time_single_sender<SF, systemNowF>;

PUSHMI_TEMPLATE (class SF, class NF)
  (requires Invocable<NF&>)
time_single_sender(SF, NF) -> time_single_sender<SF, NF>;

PUSHMI_TEMPLATE (class Data, class DSF)
  (requires TimeSender<Data, is_single<>>)
time_single_sender(Data, DSF) -> time_single_sender<Data, DSF, passDNF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DNF)
  (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&>)
time_single_sender(Data, DSF, DNF) -> time_single_sender<Data, DSF, DNF>;
#endif

template<>
struct construct_deduced<time_single_sender>
  : make_time_single_sender_fn {};

// template <
//     class V,
//     class E = std::exception_ptr,
//     class TP = std::chrono::system_clock::time_point,
//     TimeSenderTo<single<V, E>, is_single<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return time_single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
