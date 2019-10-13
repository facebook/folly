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

#include <chrono>
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
// define types for time executors

template <class E, class TP>
class any_time_executor {
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
  using exec_ref = any_time_executor_ref<E>;
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_consume_op(data&&, data*) {}
    static any_single_sender<E, exec_ref> s_schedule(data&) {
      std::terminate();
      return {};
    }
    static any_single_sender<E, exec_ref> s_schedule_time(data&, TP) {
      std::terminate();
      return {};
    }
    static TP s_now(data&) { return {}; }
    void (*op_)(data&, data*) = vtable::s_op;
    void (*consume_op_)(data&&, data*) = vtable::s_consume_op;
    TP (*now_)(data&) = vtable::s_now;
    any_single_sender<E, exec_ref> (*schedule_)(data&) = vtable::s_schedule;
    any_single_sender<E, exec_ref> (*schedule_time_)(data&, TP) =
      vtable::s_schedule_time;
  };
  static constexpr vtable const noop_{};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_time_executor(Wrapped obj, std::false_type) : any_time_executor() {
    struct s {
      static void op(data& src, data* dst) {
        dst->pobj_ = new Wrapped(*static_cast<Wrapped const*>(src.pobj_));
      }
      static void consume_op(data&& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static TP now(data& src) {
        return ::folly::pushmi::now(*static_cast<Wrapped*>(src.pobj_));
      }
      static any_single_sender<E, exec_ref> schedule(data& src) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(src.pobj_))};
      }
      static any_single_sender<E, exec_ref> schedule_time(data& src, TP tp) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(src.pobj_), tp)};
      }
    };
    static const vtable vtbl{
      s::op,
      s::consume_op,
      s::now,
      s::schedule,
      s::schedule_time
    };
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_time_executor(Wrapped obj, std::true_type) noexcept
      : any_time_executor() {
    struct s {
      static void op(data& src, data* dst) {
          new (dst->buffer_)
              Wrapped(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void consume_op(data&& src, data* dst) {
        if (dst)
          new (dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static TP now(data& src) {
        return ::folly::pushmi::now(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static any_single_sender<E, exec_ref> schedule(data& src) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static any_single_sender<E, exec_ref> schedule_time(data& src, TP tp) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>((void*)src.buffer_), tp)};
      }
    };
    static const vtable vtbl{
      s::op,
      s::consume_op,
      s::now,
      s::schedule,
      s::schedule_time
    };
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
      std::enable_if_t<!std::is_same<U, any_time_executor>::value, U>;

 public:
  any_time_executor() = default;
  any_time_executor(const any_time_executor& that) noexcept : any_time_executor() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  any_time_executor(any_time_executor&& that) noexcept : any_time_executor() {
    that.vptr_->consume_op_(std::move(that.data_), &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires TimeExecutor<wrapped_t<Wrapped>>) //
  explicit any_time_executor(Wrapped obj) noexcept(insitu<Wrapped>())
      : any_time_executor{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_time_executor() {
    vptr_->consume_op_(std::move(data_), nullptr);
  }
  any_time_executor& operator=(const any_time_executor& that) noexcept {
    this->~any_time_executor();
    new ((void*)this) any_time_executor(that);
    return *this;
  }
  any_time_executor& operator=(any_time_executor&& that) noexcept {
    this->~any_time_executor();
    new ((void*)this) any_time_executor(std::move(that));
    return *this;
  }
  TP top() {
    return vptr_->now_(data_);
  }
  any_single_sender<E, any_time_executor_ref<E>> schedule() {
    return vptr_->schedule_(data_);
  }
  any_single_sender<E, any_time_executor_ref<E>> schedule(TP tp) {
    return vptr_->schedule_time_(data_, tp);
  }
};

// Class static definitions:
template <class E, class TP>
constexpr typename any_time_executor<E, TP>::vtable const
    any_time_executor<E, TP>::noop_;

template <class SF, class NF>
class time_executor<SF, NF> {
  SF sf_;
  NF nf_;

 public:
  constexpr time_executor() = default;
  constexpr explicit time_executor(SF sf) : sf_(std::move(sf)) {}
  constexpr time_executor(SF sf, NF nf) : sf_(std::move(sf)), nf_(std::move(nf)) {}

  invoke_result_t<NF&> top() {
    return nf_();
  }

  PUSHMI_TEMPLATE(class SF_ = SF)
  (requires Invocable<SF_&>) //
  auto schedule() {
    return sf_();
  }
};

template <
    PUSHMI_TYPE_CONSTRAINT(TimeExecutor) Data,
    class DSF,
    class DNF>
class time_executor<Data, DSF, DNF> {
  Data data_;
  DSF sf_;
  DNF nf_;

 public:
  using properties = properties_t<Data>;

  constexpr time_executor() = default;
  constexpr explicit time_executor(Data data) : data_(std::move(data)) {}
  constexpr time_executor(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr time_executor(Data data, DSF sf, DNF nf)
      : data_(std::move(data)), sf_(std::move(sf)), nf_(std::move(nf)) {}

  invoke_result_t<DNF&, Data&> top() {
    return nf_(data_);
  }

  PUSHMI_TEMPLATE(class DSF_ = DSF)
  (requires Invocable<DSF_&, Data&>) //
  auto schedule() {
    return sf_(data_);
  }
};

template <>
class time_executor<> : public time_executor<ignoreSF, systemNowF> {
 public:
  time_executor() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_time_executor
PUSHMI_INLINE_VAR constexpr struct make_time_executor_fn {
  inline auto operator()() const {
    return time_executor<ignoreSF, systemNowF>{};
  }
  PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not TimeExecutor<SF>)) //
  auto operator()(SF sf) const {
    return time_executor<SF, systemNowF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class NF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not TimeExecutor<SF>)) //
  auto operator()(SF sf, NF nf) const {
    return time_executor<SF, NF>{std::move(sf), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data)
  (requires True<>&& TimeExecutor<Data>) //
  auto operator()(Data d) const {
    return time_executor<Data, passDSF, passDNF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
  (requires TimeExecutor<Data>) //
  auto operator()(Data d, DSF sf) const {
    return time_executor<Data, DSF, passDNF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DNF)
  (requires TimeExecutor<Data>) //
  auto operator()(Data d, DSF sf, DNF nf) const {
    return time_executor<Data, DSF, DNF>{std::move(d), std::move(sf), std::move(nf)};
  }
} const make_time_executor{};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
time_executor()->time_executor<ignoreSF, systemNowF>;

PUSHMI_TEMPLATE(class SF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not TimeExecutor<SF>)) //
    time_executor(SF)
        ->time_executor<SF, systemNowF>;

PUSHMI_TEMPLATE(class SF, class NF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not TimeExecutor<SF>)) //
    time_executor(SF, NF)
        ->time_executor<SF, NF>;

PUSHMI_TEMPLATE(class Data)
(requires True<>&& TimeExecutor<Data>) //
    time_executor(Data)
        ->time_executor<Data, passDSF, passDNF>;

PUSHMI_TEMPLATE(class Data, class DSF)
(requires TimeExecutor<Data>) //
    time_executor(Data, DSF)
        ->time_executor<Data, DSF, passDNF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DNF)
(requires TimeExecutor<Data>) //
    time_executor(Data, DSF, DNF)
        ->time_executor<Data, DSF, DNF>;
#endif

template <>
struct construct_deduced<time_executor> : make_time_executor_fn {};

template <class E, class TP>
struct any_time_executor_ref {
 private:
  using This = any_time_executor_ref;
  using exec_ref = any_time_executor_ref<E, TP>;
  void* pobj_;
  struct vtable {
    TP (*now_)(void*);
    any_single_sender<E, exec_ref> (*schedule_)(void*);
    any_single_sender<E, exec_ref> (*schedule_tp_)(void*, TP);
  } const* vptr_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_time_executor_ref>::value, U>;

 public:
  any_time_executor_ref() = delete;
  any_time_executor_ref(const any_time_executor_ref&) = default;

  PUSHMI_TEMPLATE(class Wrapped)
  (requires TimeExecutor<wrapped_t<Wrapped>>)
      any_time_executor_ref(Wrapped& w) {
    struct s {
      static TP now(void* pobj) {
        return ::folly::pushmi::now(*static_cast<Wrapped*>(pobj));
      }
      static any_single_sender<E, exec_ref> schedule(
          void* pobj) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(pobj),
            ::folly::pushmi::now(*static_cast<Wrapped*>(pobj)))};
      }
      static any_single_sender<E, exec_ref> schedule_tp(
          void* pobj,
          TP tp) {
        return any_single_sender<E, exec_ref>{
          ::folly::pushmi::schedule(*static_cast<Wrapped*>(pobj), tp)};
      }
    };
    static const vtable vtbl{s::now, s::schedule, s::schedule_tp};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  PUSHMI_TEMPLATE(class Wrapped)
  (requires TimeExecutor<wrapped_t<Wrapped>> && //
      std::is_rvalue_reference<Wrapped>::value) //
  any_time_executor_ref(Wrapped&& w) {
    struct s {
      static TP now(void* pobj) {
        return ::folly::pushmi::now(*static_cast<Wrapped*>(pobj));
      }
      static any_single_sender<E, exec_ref> schedule(
          void* pobj) {
        return any_single_sender<E,exec_ref>{::folly::pushmi::schedule(
            std::move(*static_cast<Wrapped*>(pobj)),
            ::folly::pushmi::now(*static_cast<Wrapped*>(pobj)))};
      }
      static any_single_sender<E, exec_ref> schedule_tp(void* pobj, TP tp) {
        return any_single_sender<E, any_time_executor_ref<E, TP>>{
          ::folly::pushmi::schedule(
            std::move(*static_cast<Wrapped*>(pobj)),
            tp)};
      }
    };
    static const vtable vtbl{s::now, s::schedule, s::schedule_tp};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  TP top() {
    return vptr_->now_(pobj_);
  }
  any_single_sender<E, any_time_executor_ref<E, TP>> schedule() {
    return vptr_->schedule_(pobj_);
  }
  any_single_sender<E, any_time_executor_ref<E, TP>> schedule(TP tp) {
    return vptr_->schedule_tp_(pobj_, tp);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_time_executor_ref
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
auto make_any_time_executor_ref() {
  return any_time_executor_ref<E, TP>{};
}

PUSHMI_TEMPLATE(class E = std::exception_ptr, class Ex)
(requires TimeExecutor<Ex> && !detail::is_v<Ex, any_time_executor_ref>) //
auto make_any_time_executor_ref(Ex& w) {
  return any_time_executor_ref<E, time_point_t<Ex>>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
any_time_executor_ref()
    ->any_time_executor_ref<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;

PUSHMI_TEMPLATE(class Ex)
(requires TimeExecutor<Ex> && !detail::is_v<Ex, any_time_executor_ref>) //
any_time_executor_ref(Ex&)
    ->any_time_executor_ref<std::exception_ptr, time_point_t<Ex>>;
#endif

} // namespace pushmi
} // namespace folly
