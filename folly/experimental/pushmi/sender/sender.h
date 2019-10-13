/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <type_traits>

#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/receiver/receiver.h>
#include <folly/experimental/pushmi/sender/concepts.h>
#include <folly/experimental/pushmi/sender/functional.h>
#include <folly/experimental/pushmi/sender/primitives.h>
#include <folly/experimental/pushmi/sender/tags.h>

namespace folly {
namespace pushmi {

template <class E, class... VN>
class any_sender
: public sender_tag::with_values<VN...>::template with_error<E> {
  using insitu_t = void*[2];
  union data {
    void* pobj_ = nullptr;
    std::aligned_union_t<0, insitu_t> buffer_;
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_submit(data&, any_receiver<E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*submit_)(data&, any_receiver<E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_{};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_sender(Wrapped obj, std::false_type) : any_sender() {
    static_assert(SenderTo<wrapped_t<Wrapped>&, any_receiver<E, VN...>>, "Wrapped object does not support w.submit(any_receiver) - perhaps std::move(w).submit(any_receiver) is supported");
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::folly::pushmi::submit(
            *static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_sender(Wrapped obj, std::true_type) noexcept : any_sender() {
    static_assert(SenderTo<wrapped_t<Wrapped>&, any_receiver<E, VN...>>, "Wrapped object does not support w.submit(any_receiver) - perhaps std::move(w).submit(any_receiver) is supported");
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (&dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)&src.buffer_)));
        static_cast<Wrapped const*>((void*)&src.buffer_)->~Wrapped();
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::folly::pushmi::submit(
            *static_cast<Wrapped*>((void*)&src.buffer_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    new (&data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
      std::enable_if_t<!std::is_same<U, any_sender>::value, U>;

 public:
  any_sender() = default;
  any_sender(any_sender&& that) noexcept : any_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires Sender<wrapped_t<Wrapped>>) //
  explicit any_sender(Wrapped obj) noexcept(insitu<Wrapped>())
      : any_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_sender& operator=(any_sender&& that) noexcept {
    this->~any_sender();
    new ((void*)this) any_sender(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE_DEBUG(class Out)
  (requires ReceiveError<Out, E>&& ReceiveValue<Out, VN...> && Constructible<any_receiver<E, VN...>, Out>) //
      void submit(Out&& out) {
    vptr_->submit_(data_, any_receiver<E, VN...>{(Out &&) out});
  }
};

// Class static definitions:
template <class E, class... VN>
constexpr typename any_sender<E, VN...>::vtable const
    any_sender<E, VN...>::noop_;

template <class SF>
class sender<SF> : public sender_tag {
  SF sf_;

 public:
  constexpr sender() = default;
  constexpr explicit sender(SF sf) : sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE_DEBUG(class Out)
  (requires PUSHMI_EXP(
      lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<SF&, Out>)) //
      void submit(Out out) & {
    sf_(std::move(out));
  }

  PUSHMI_TEMPLATE_DEBUG(class Out)
  (requires PUSHMI_EXP(
      lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<SF&&, Out>)) //
      void submit(Out out) && {
    std::move(sf_)(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender) Data, class DSF>
class sender<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using sender_category = sender_tag;
  using properties = properties_t<Data>;

  static_assert(
      Sender<Data>,
      "Data must be a sender");

  constexpr sender() = default;
  constexpr explicit sender(Data data) : data_(std::move(data)) {}
  constexpr sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE_DEBUG(class Out)
  (requires PUSHMI_EXP(
      lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<DSF&, Data&, Out>)) //
      void submit(Out out) & {
    sf_(data_, std::move(out));
  }
  PUSHMI_TEMPLATE_DEBUG(class Out)
  (requires PUSHMI_EXP(
      lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<DSF&&, Data&&, Out>)) //
      void submit(Out out) && {
    std::move(sf_)(std::move(data_), std::move(out));
  }
};

template <>
class sender<> : public sender<ignoreSF> {
 public:
  sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_sender
PUSHMI_INLINE_VAR constexpr struct make_many_sender_fn {
  inline auto operator()() const {
    return sender<ignoreSF>{};
  }
  PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not Sender<SF>)) //
      auto
      operator()(SF sf) const {
    return sender<SF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data)
  (requires True<>&& Sender<Data>) //
      auto
      operator()(Data d) const {
    return sender<Data, passDSF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data>) //
      auto
      operator()(Data d, DSF sf) const {
    return sender<Data, DSF>{std::move(d), std::move(sf)};
  }
} const make_sender{};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
sender()->sender<ignoreSF>;

PUSHMI_TEMPLATE(class SF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not Sender<SF>)) //
    sender(SF)
        ->sender<SF>;

PUSHMI_TEMPLATE(class Data)
(requires True<>&& Sender<Data>) //
    sender(Data)
        ->sender<Data, passDSF>;

PUSHMI_TEMPLATE(class Data, class DSF)
(requires Sender<Data>) //
    sender(Data, DSF)
        ->sender<Data, DSF>;
#endif

template <>
struct construct_deduced<sender> : make_many_sender_fn {};

} // namespace pushmi
} // namespace folly
