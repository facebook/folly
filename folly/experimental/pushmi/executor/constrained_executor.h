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
// define types for constrained executors

template <class E, class CV>
class any_constrained_executor {
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
  using exec_ref = any_constrained_executor_ref<E>;
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_consume_op(data&&, data*) {}
    static any_single_sender<E, exec_ref> s_schedule(data&) {
      std::terminate();
      return {};
    }
    static any_single_sender<E, exec_ref> s_schedule_cv(data&, CV) {
      std::terminate();
      return {};
    }
    static CV s_top(data&) { return {}; }
    void (*op_)(data&, data*) = vtable::s_op;
    void (*consume_op_)(data&&, data*) = vtable::s_consume_op;
    CV (*top_)(data&) = vtable::s_top;
    any_single_sender<E, exec_ref> (*schedule_)(data&) = vtable::s_schedule;
    any_single_sender<E, exec_ref> (*schedule_cv_)(data&, CV) =
      vtable::s_schedule_cv;
  };
  static constexpr vtable const noop_{};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_constrained_executor(Wrapped obj, std::false_type) : any_constrained_executor() {
    struct s {
      static void op(data& src, data* dst) {
        dst->pobj_ = new Wrapped(*static_cast<Wrapped const*>(src.pobj_));
      }
      static void consume_op(data&& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static CV top(data& src) {
        return ::folly::pushmi::top(*static_cast<Wrapped*>(src.pobj_));
      }
      static any_single_sender<E, exec_ref> schedule(data& src) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(src.pobj_))};
      }
      static any_single_sender<E, exec_ref> schedule_cv(data& src, CV cv) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>(src.pobj_), cv)};
      }
    };
    static const vtable vtbl{
      s::op,
      s::consume_op,
      s::top,
      s::schedule,
      s::schedule_cv
    };
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_constrained_executor(Wrapped obj, std::true_type) noexcept
      : any_constrained_executor() {
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
      static CV top(data& src) {
        return ::folly::pushmi::top(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static any_single_sender<E, exec_ref> schedule(data& src) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static any_single_sender<E, exec_ref> schedule_cv(data& src, CV cv) {
        return any_single_sender<E, exec_ref>{::folly::pushmi::schedule(
            *static_cast<Wrapped*>((void*)src.buffer_), cv)};
      }
    };
    static const vtable vtbl{
      s::op,
      s::consume_op,
      s::top,
      s::schedule,
      s::schedule_cv
    };
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
      std::enable_if_t<!std::is_same<U, any_constrained_executor>::value, U>;

 public:
  any_constrained_executor() = default;
  any_constrained_executor(const any_constrained_executor& that) noexcept
  : any_constrained_executor() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  any_constrained_executor(any_constrained_executor&& that) noexcept
  : any_constrained_executor() {
    that.vptr_->consume_op_(std::move(that.data_), &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires ConstrainedExecutor<wrapped_t<Wrapped>>) //
      explicit any_constrained_executor(Wrapped obj) noexcept(insitu<Wrapped>())
      : any_constrained_executor{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_constrained_executor() {
    vptr_->consume_op_(std::move(data_), nullptr);
  }
  any_constrained_executor& operator=(const any_constrained_executor& that) noexcept {
    this->~any_constrained_executor();
    new ((void*)this) any_constrained_executor(that);
    return *this;
  }
  any_constrained_executor& operator=(any_constrained_executor&& that) noexcept {
    this->~any_constrained_executor();
    new ((void*)this) any_constrained_executor(std::move(that));
    return *this;
  }
  CV top() {
    return vptr_->top_(data_);
  }
  any_single_sender<E, any_constrained_executor_ref<E>> schedule() {
    return vptr_->schedule_(data_);
  }
  any_single_sender<E, any_constrained_executor_ref<E>> schedule(CV cv) {
    return vptr_->schedule_cv_(data_, cv);
  }
};

// Class static definitions:
template <class E, class CV>
constexpr typename any_constrained_executor<E, CV>::vtable const
    any_constrained_executor<E, CV>::noop_;

template <class SF, class ZF>
class constrained_executor<SF, ZF> {
  SF sf_;
  ZF zf_;

 public:
  constexpr constrained_executor() = default;
  constexpr explicit constrained_executor(SF sf) : sf_(std::move(sf)) {}
  constexpr constrained_executor(SF sf, ZF zf) : sf_(std::move(sf)), zf_(std::move(zf)) {}

  invoke_result_t<ZF&> top() {
    return zf_();
  }

  PUSHMI_TEMPLATE(class SF_ = SF)
  (requires Invocable<SF_&>) //
  auto schedule() {
    return sf_();
  }
};

template <
    PUSHMI_TYPE_CONSTRAINT(ConstrainedExecutor) Data,
    class DSF,
    class DZF>
class constrained_executor<Data, DSF, DZF> {
  Data data_;
  DSF sf_;
  DZF zf_;

 public:
  using properties = properties_t<Data>;

  constexpr constrained_executor() = default;
  constexpr explicit constrained_executor(Data data) : data_(std::move(data)) {}
  constexpr constrained_executor(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr constrained_executor(Data data, DSF sf, DZF zf)
      : data_(std::move(data)), sf_(std::move(sf)), zf_(std::move(zf)) {}

  invoke_result_t<DZF&, Data&> top() {
    return zf_(data_);
  }

  PUSHMI_TEMPLATE(class DSF_ = DSF)
  (requires Invocable<DSF_&, Data&>) //
  auto schedule() {
    return sf_(data_);
  }
};

template <>
class constrained_executor<> : public constrained_executor<ignoreSF, priorityZeroF> {
 public:
  constrained_executor() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_constrained_executor
PUSHMI_INLINE_VAR constexpr struct make_constrained_executor_fn {
  inline auto operator()() const {
    return constrained_executor<ignoreSF, priorityZeroF>{};
  }
  PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not ConstrainedExecutor<SF>)) //
  auto operator()(SF sf) const {
    return constrained_executor<SF, priorityZeroF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class ZF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not ConstrainedExecutor<SF>)) //
  auto operator()(SF sf, ZF zf) const {
    return constrained_executor<SF, ZF>{std::move(sf), std::move(zf)};
  }
  PUSHMI_TEMPLATE(class Data)
  (requires True<>&& ConstrainedExecutor<Data>) //
  auto operator()(Data d) const {
    return constrained_executor<Data, passDSF, passDZF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
  (requires ConstrainedExecutor<Data>) //
  auto operator()(Data d, DSF sf) const {
    return constrained_executor<Data, DSF, passDZF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DZF)
  (requires ConstrainedExecutor<Data>) //
  auto operator()(Data d, DSF sf, DZF zf) const {
    return constrained_executor<Data, DSF, DZF>{std::move(d), std::move(sf), std::move(zf)};
  }
} const make_constrained_executor{};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
constrained_executor()->constrained_executor<ignoreSF, priorityZeroF>;

PUSHMI_TEMPLATE(class SF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not ConstrainedExecutor<SF>)) //
    constrained_executor(SF)
        ->constrained_executor<SF, priorityZeroF>;

PUSHMI_TEMPLATE(class SF, class ZF)
(requires True<> PUSHMI_BROKEN_SUBSUMPTION(&&not ConstrainedExecutor<SF>)) //
    constrained_executor(SF, ZF)
        ->constrained_executor<SF, ZF>;

PUSHMI_TEMPLATE(class Data)
(requires True<>&& ConstrainedExecutor<Data>) //
    constrained_executor(Data)
        ->constrained_executor<Data, passDSF, passDZF>;

PUSHMI_TEMPLATE(class Data, class DSF)
(requires ConstrainedExecutor<Data>) //
    constrained_executor(Data, DSF)
        ->constrained_executor<Data, DSF, passDZF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DZF)
(requires ConstrainedExecutor<Data>) //
    constrained_executor(Data, DSF, DZF)
        ->constrained_executor<Data, DSF, DZF>;
#endif

template <>
struct construct_deduced<constrained_executor> : make_constrained_executor_fn {};

template <class E, class CV>
struct any_constrained_executor_ref {
 private:
  using This = any_constrained_executor_ref;
  void* pobj_;
  using exec_ref = any_constrained_executor_ref<E, CV>;
  struct vtable {
    CV (*top_)(void*);
    any_single_sender<E, CV, exec_ref> (*schedule_)(void*);
    any_single_sender<E, CV, exec_ref> (*schedule_cv_)(void*, CV);
  } const* vptr_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_constrained_executor_ref>::value, U>;

 public:
  any_constrained_executor_ref() = delete;
  any_constrained_executor_ref(const any_constrained_executor_ref&) = default;

  PUSHMI_TEMPLATE(class Wrapped)
  (requires ConstrainedExecutor<wrapped_t<Wrapped>>)
  any_constrained_executor_ref(Wrapped& w) {
    struct s {
      static CV top(void* pobj) {
        return ::folly::pushmi::top(*static_cast<Wrapped*>(pobj));
      }
      static any_single_sender<E, CV, exec_ref> schedule(void* pobj) {
        return ::folly::pushmi::schedule(
            *static_cast<Wrapped*>(pobj),
            ::folly::pushmi::top(*static_cast<Wrapped*>(pobj)));
      }
      static any_single_sender<E, CV, exec_ref> schedule_cv(void* pobj, CV cv) {
        return ::folly::pushmi::schedule(*static_cast<Wrapped*>(pobj), cv);
      }
    };
    static const vtable vtbl{s::top, s::schedule, s::schedule_cv};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  CV top() {
    return vptr_->top_(pobj_);
  }
  any_single_sender<E, any_constrained_executor_ref<E>> schedule() {
    return vptr_->schedule_(pobj_);
  }
  any_single_sender<E, any_constrained_executor_ref<E>> schedule(CV cv) {
    return vptr_->schedule_cv_(pobj_, cv);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_constrained_executor_ref
template <class E = std::exception_ptr, class CV = std::ptrdiff_t>
auto make_any_constrained_executor_ref() {
  return any_constrained_executor_ref<E, CV>{};
}

PUSHMI_TEMPLATE(class E = std::exception_ptr, class Ex)
(requires ConstrainedExecutor<Ex> &&
    !detail::is_v<Ex, any_constrained_executor_ref>) //
auto make_any_constrained_executor_ref(Ex& w) {
  return any_constrained_executor_ref<E, constraint_t<Ex>>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
any_constrained_executor_ref()
    ->any_constrained_executor_ref<std::exception_ptr, std::ptrdiff_t>;

PUSHMI_TEMPLATE(class Ex)
(requires ConstrainedExecutor<Ex> &&
    !detail::is_v<Ex, any_constrained_executor_ref>) //
    any_constrained_executor_ref(Ex&)
        ->any_constrained_executor_ref<
            std::exception_ptr,
            constraint_t<Ex>>;
#endif

} // namespace pushmi
} // namespace folly
