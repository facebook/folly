#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "none.h"
#include "executor.h"
#include "trampoline.h"

namespace pushmi {
namespace detail {
struct erase_sender_t {};
} // namespace detail

template <class E>
class sender<detail::erase_sender_t, E> {
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
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_none<E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_none<E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  sender(Wrapped obj, std::false_type) : sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_none<E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  sender(Wrapped obj, std::true_type) noexcept : sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_none<E> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_none<>>;

  sender() = default;
  sender(sender&& that) noexcept : sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires SenderTo<wrapped_t<Wrapped>, any_none<E>, is_none<>>)
  explicit sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~sender() {
    vptr_->op_(data_, nullptr);
  }
  sender& operator=(sender&& that) noexcept {
    this->~sender();
    new ((void*)this) sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_none<E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class E>
constexpr typename sender<detail::erase_sender_t, E>::vtable const
    sender<detail::erase_sender_t, E>::noop_;

template <class SF, class EXF>
class sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_none<>>;

  constexpr sender() = default;
  constexpr explicit sender(SF sf) : sf_(std::move(sf)) {}
  constexpr sender(SF sf, EXF exf) : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_none<>>) Data, class DSF, class DEXF>
class sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;
    static_assert(Sender<Data, is_none<>>, "The Data template parameter "
    "must satisfy the Sender concept.");

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_none<>>>;

  constexpr sender() = default;
  constexpr explicit sender(Data data)
      : data_(std::move(data)) {}
  constexpr sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<DSF&, Data&, Out>)
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class sender<>
    : public sender<ignoreSF, trampolineEXF> {
public:
  sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_sender
PUSHMI_INLINE_VAR constexpr struct make_sender_fn {
  inline auto operator()() const {
    return sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires Sender<Wrapped, is_none<>>)
  auto operator()(Wrapped w) const {
    return sender<detail::erase_sender_t, std::exception_ptr>{std::move(w)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_none<>>)
  auto operator()(Data data, DSF sf) const {
    return sender<Data, DSF, passDEXF>{std::move(data), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_none<>>)
  auto operator()(Data data, DSF sf, DEXF exf) const {
    return sender<Data, DSF, DEXF>{std::move(data), std::move(sf), std::move(exf)};
  }
} const make_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
sender() -> sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
sender(SF) -> sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
sender(SF, EXF) -> sender<SF, EXF>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires Sender<Wrapped, is_none<>>)
sender(Wrapped) ->
    sender<detail::erase_sender_t, std::exception_ptr>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_none<>>)
sender(Data, DSF) -> sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_none<>>)
sender(Data, DSF, DEXF) -> sender<Data, DSF, DEXF>;
#endif

template <class E = std::exception_ptr>
using any_sender = sender<detail::erase_sender_t, E>;

template<>
struct construct_deduced<sender> : make_sender_fn {};

// template <SenderTo<any_none<std::exception_ptr>, is_none<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return sender<detail::erase_sender_t, std::exception_ptr>{std::move(w)};
// }
//
// template <class E, SenderTo<any_none<E>, is_none<>> Wrapped>
//   requires Same<is_none<>, properties_t<Wrapped>>
// auto erase_cast(Wrapped w) {
//   return sender<detail::erase_sender_t, E>{std::move(w)};
// }

} // namespace pushmi
