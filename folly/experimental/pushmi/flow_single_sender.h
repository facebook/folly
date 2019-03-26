/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/pushmi/flow_receiver.h>
#include <folly/experimental/pushmi/executor.h>
#include <folly/experimental/pushmi/trampoline.h>
#include <type_traits>

namespace folly {
namespace pushmi {

template <class PE, class E, class... VN>
class any_flow_single_sender {
  union data {
    void* pobj_ = nullptr;
    std::aligned_storage_t<
      sizeof(std::tuple<VN...>), alignof(std::tuple<VN...>)> buffer_;
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_submit(data&, any_flow_receiver<PE, std::ptrdiff_t, E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*submit_)(data&, any_flow_receiver<PE, std::ptrdiff_t, E, VN...>) =
        vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_flow_single_sender(Wrapped obj, std::false_type)
      : any_flow_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void submit(
          data& src,
          any_flow_receiver<PE, std::ptrdiff_t, E, VN...> out) {
        ::folly::pushmi::submit(
            *static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_flow_single_sender(Wrapped obj, std::true_type) noexcept
    : any_flow_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (&dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)&src.buffer_)));
        static_cast<Wrapped const*>((void*)&src.buffer_)->~Wrapped();
      }
      static void submit(
          data& src,
          any_flow_receiver<PE, std::ptrdiff_t, E, VN...> out) {
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
    std::enable_if_t<!std::is_same<U, any_flow_single_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  any_flow_single_sender() = default;
  any_flow_single_sender(any_flow_single_sender&& that) noexcept
      : any_flow_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>> &&is_single_v<wrapped_t<Wrapped>>)
  explicit any_flow_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_flow_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_flow_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_flow_single_sender& operator=(any_flow_single_sender&& that) noexcept {
    this->~any_flow_single_sender();
    new ((void*)this) any_flow_single_sender(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE(class Out)
  (requires ReceiveError<Out, E>&& ReceiveValue<Out, VN...>) //
      void submit(Out&& out) {
    vptr_->submit_(data_, any_flow_receiver<PE, std::ptrdiff_t, E, VN...>{(Out &&) out});
  }
};

// Class static definitions:
template <class PE, class E, class... VN>
constexpr typename any_flow_single_sender<PE, E, VN...>::vtable const
    any_flow_single_sender<PE, E, VN...>::noop_;

template <class SF>
class flow_single_sender<SF> {
  SF sf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(SF sf)
      : sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <
    PUSHMI_TYPE_CONSTRAINT(Sender<is_single<>, is_flow<>>) Data,
    class DSF>
class flow_single_sender<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using properties = property_set_insert_t<
      properties_t<Data>,
      property_set<is_sender<>, is_flow<>, is_single<>>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr flow_single_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class flow_single_sender<>
    : public flow_single_sender<ignoreSF> {
public:
  flow_single_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_single_sender
PUSHMI_INLINE_VAR constexpr struct make_flow_single_sender_fn {
  inline auto operator()() const {
    return flow_single_sender<ignoreSF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return flow_single_sender<SF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && FlowSender<Data> && is_single_v<Data>)
  auto operator()(Data d) const {
    return flow_single_sender<Data, passDSF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires FlowSender<Data> && is_single_v<Data>)
  auto operator()(Data d, DSF sf) const {
    return flow_single_sender<Data, DSF>{std::move(d), std::move(sf)};
  }
} const make_flow_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_single_sender() -> flow_single_sender<ignoreSF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_single_sender(SF) -> flow_single_sender<SF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && FlowSender<Data> && is_single_v<Data>)
flow_single_sender(Data) -> flow_single_sender<Data, passDSF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires FlowSender<Data> && is_single_v<Data>)
flow_single_sender(Data, DSF) -> flow_single_sender<Data, DSF>;
#endif

template<>
struct construct_deduced<flow_single_sender>
  : make_flow_single_sender_fn {};


} // namespace pushmi
} // namespace folly
