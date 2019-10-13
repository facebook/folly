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

#include <exception>
#include <functional>
#include <type_traits>

#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/functional.h>
#include <folly/experimental/pushmi/executor/primitives.h>
#include <folly/experimental/pushmi/receiver/receiver.h>
#include <folly/experimental/pushmi/sender/single_sender.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {
//
// define types for executors

template <class E>
class any_executor {
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
    static void s_consume_op(data&&, data*) {}
    static any_single_sender<E, any_executor_ref<E>> s_schedule(data&) { return {}; }
    void (*op_)(data&, data*) = vtable::s_op;
    void (*consume_op_)(data&, data*) = vtable::s_consume_op;
    any_single_sender<E, any_executor_ref<E>> (*schedule_)(data&) = vtable::s_schedule;
  };
  static constexpr vtable const noop_{};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_executor(Wrapped obj, std::false_type) : any_executor() {
    struct s {
      static void op(data& src, data* dst) {
        dst->pobj_ = new Wrapped(*static_cast<Wrapped const*>(src.pobj_));
      }
      static void consume_op(data&& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_single_sender<E, any_executor_ref<E>> schedule(data& src) {
        return any_single_sender<E, any_executor_ref<E>>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(src.pobj_))};
      }
    };
    static const vtable vtbl{s::op, s::consume_op, s::schedule};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_executor(Wrapped obj, std::true_type) noexcept
      : any_executor() {
    struct s {
      static void op(data& src, data* dst) {
        new (dst->buffer_)
            Wrapped(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void consume_op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_single_sender<E, any_executor_ref<E>> schedule(data& src) {
        return any_single_sender<E, any_executor_ref<E>>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>((void*)src.buffer_))};
      }
    };
    static const vtable vtbl{s::op, s::consume_op, s::schedule};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
      std::enable_if_t<!std::is_same<U, any_executor>::value, U>;

 public:
  any_executor() = default;
  any_executor(const any_executor& that) noexcept : any_executor() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  any_executor(any_executor&& that) noexcept : any_executor() {
    that.vptr_->consume_op_(std::move(that.data_), &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires Executor<wrapped_t<Wrapped>>) //
      explicit any_executor(Wrapped obj) noexcept(insitu<Wrapped>())
      : any_executor{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_executor() {
    vptr_->consume_op_(std::move(data_), nullptr);
  }
  any_executor& operator=(const any_executor& that) noexcept {
    this->~any_executor();
    new ((void*)this) any_executor(that);
    return *this;
  }
  any_executor& operator=(any_executor&& that) noexcept {
    this->~any_executor();
    new ((void*)this) any_executor(std::move(that));
    return *this;
  }
  any_single_sender<E, any_executor_ref<E>> schedule() {
    return vptr_->schedule_(data_);
  }
};

// Class static definitions:
template <class E>
constexpr typename any_executor<E>::vtable const
    any_executor<E>::noop_;

template <class SF>
class executor<SF> {
  SF sf_;

 public:
  constexpr executor() = default;
  constexpr explicit executor(SF sf) : sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class SF_ = SF)
  (requires Invocable<SF_&>) //
  auto schedule() {
    return sf_();
  }
};

template <
    PUSHMI_TYPE_CONSTRAINT(Executor) Data,
    class DSF>
class executor<Data, DSF> {
  Data data_;
  DSF sf_;

 public:
  using properties = properties_t<Data>;

  constexpr executor() = default;
  constexpr explicit executor(Data data) : data_(std::move(data)) {}
  constexpr executor(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}

  PUSHMI_TEMPLATE(class DSF_ = DSF)
  (requires Invocable<DSF_&, Data&>) //
  auto schedule() {
    return sf_(data_);
  }
};

template <>
class executor<> : public executor<ignoreSF> {
 public:
  executor() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_executor
PUSHMI_INLINE_VAR constexpr struct make_executor_fn {
  inline auto operator()() const {
    return executor<ignoreSF>{};
  }
  PUSHMI_TEMPLATE(class SF)
  (requires not Executor<SF>) //
  auto operator()(SF sf) const {
    return executor<SF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data)
  (requires True<>&& Executor<Data>) //
  auto operator()(Data d) const {
    return executor<Data, passDSF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Executor<Data>) //
  auto operator()(Data d, DSF sf) const {
    return executor<Data, DSF>{std::move(d), std::move(sf)};
  }
} const make_executor{};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
executor()->executor<ignoreSF>;

PUSHMI_TEMPLATE(class SF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not Executor<SF>)) //
    executor(SF)
        ->executor<SF>;

PUSHMI_TEMPLATE(class Data)
(requires True<>&& Executor<Data>) //
    executor(Data)
        ->executor<Data, passDSF>;

PUSHMI_TEMPLATE(class Data, class DSF)
(requires Executor<Data>) //
    executor(Data, DSF)
        ->executor<Data, DSF>;
#endif

template <>
struct construct_deduced<executor> : make_executor_fn {};

template <class E>
struct any_executor_ref {
 private:
  using This = any_executor_ref;
  void* pobj_;
  struct vtable {
    any_single_sender<E, any_executor_ref<E>> (*schedule_)(void*);
  } const* vptr_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_executor_ref>::value, U>;

 public:
  any_executor_ref() = delete;
  any_executor_ref(const any_executor_ref&) = default;

  PUSHMI_TEMPLATE(class Wrapped)
  (requires Executor<wrapped_t<Wrapped>>) //
      any_executor_ref(Wrapped& w) {
    struct s {
      static any_single_sender<E, any_executor_ref<E>> schedule(void* pobj) {
        return any_single_sender<E, any_executor_ref<E>>{
            ::folly::pushmi::schedule(*static_cast<Wrapped*>(pobj))};
      }
    };
    static const vtable vtbl{s::schedule};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  template<class... AN>
  any_single_sender<E, any_executor_ref<E>> schedule() {
    return vptr_->schedule_(pobj_);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_executor_ref
template <class E = std::exception_ptr>
auto make_any_executor_ref() {
  return any_executor_ref<E>{};
}

PUSHMI_TEMPLATE(class E = std::exception_ptr, class Ex)
(requires Executor<Ex> && !detail::is_v<Ex, any_executor_ref>) //
auto make_any_executor_ref(Ex& w) {
  return any_executor_ref<E>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
any_executor_ref()->any_executor_ref<std::exception_ptr>;

PUSHMI_TEMPLATE(class Ex)
(requires Executor<Ex> && !detail::is_v<Ex, any_executor_ref>) //
any_executor_ref(Ex&)
    ->any_executor_ref<std::exception_ptr>;
#endif

} // namespace pushmi
} // namespace folly
