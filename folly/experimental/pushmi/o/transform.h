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
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/receiver.h>

namespace folly {
namespace pushmi {

namespace detail {

template <class F, class Tag, bool IsFlow = false>
struct transform_on;

template <class F>
struct transform_on<F, is_single<>> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f) : f_(std::move(f)) {}
  struct value_fn {
    F f_;
    value_fn() = default;
    constexpr explicit value_fn(F f) : f_(std::move(f)) {}
    template <class Out, class V0, class... VN>
    auto operator()(Out& out, V0&& v0, VN&&... vn) {
      using Result = ::folly::pushmi::invoke_result_t<F, V0, VN...>;
      static_assert(
          ::folly::pushmi::SemiMovable<Result>,
          "none of the functions supplied to transform can convert this value");
      static_assert(
          ::folly::pushmi::ReceiveValue<Out, Result>,
          "Result of value transform cannot be delivered to Out");
      set_value(out, f_((V0 &&) v0, (VN &&) vn...));
    }
  };
  template <class Out>
  auto operator()(Out out) const {
    return ::folly::pushmi::make_receiver(std::move(out), value_fn{f_});
  }
};

template <class F>
struct transform_on<F, is_single<>, true> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f) : f_(std::move(f)) {}
  template <class Out>
  auto operator()(Out out) const {
    return make_flow_single(std::move(out), on_value(*this));
  }
  template <class Out, class V0, class... VN>
  auto operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::folly::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(
        ::folly::pushmi::SemiMovable<Result>,
        "none of the functions supplied to transform can convert this value");
    static_assert(
        ::folly::pushmi::FlowReceiveValue<Out, Result>,
        "Result of value transform cannot be delivered to Out");
    set_value(out, f_((V0 &&) v0, (VN &&) vn...));
  }
};

template <class F>
struct transform_on<F, is_many<>> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f) : f_(std::move(f)) {}
  template <class Out>
  auto operator()(Out out) const {
    return ::folly::pushmi::make_receiver(std::move(out), on_value(*this));
  }
  template <class Out, class V0, class... VN>
  auto operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::folly::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(
        ::folly::pushmi::SemiMovable<Result>,
        "none of the functions supplied to transform can convert this value");
    static_assert(
        ::folly::pushmi::ReceiveValue<Out, Result>,
        "Result of value transform cannot be delivered to Out");
    set_value(out, f_((V0 &&) v0, (VN &&) vn...));
  }
};

template <class F>
struct transform_on<F, is_many<>, true> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f) : f_(std::move(f)) {}
  template <class Out>
  auto operator()(Out out) const {
    return make_flow_receiver(std::move(out), on_value(*this));
  }
  template <class Out, class V0, class... VN>
  void operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::folly::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(
        ::folly::pushmi::SemiMovable<Result>,
        "none of the functions supplied to transform can convert this value");
    static_assert(
        ::folly::pushmi::FlowReceiveValue<Out, Result>,
        "Result of value transform cannot be delivered to Out");
    set_value(out, f_((V0 &&) v0, (VN &&) vn...));
  }
};

struct transform_fn {
 private:
  template <class F, class In>
  struct submit_impl {
    F f_;
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<std::decay_t<Out>>) //
        auto
        operator()(SIn&& in, Out&& out) const {
      using Cardinality = property_set_index_t<properties_t<In>, is_single<>>;
      // copy 'f_' to allow multiple calls to connect to multiple 'in'
      ::folly::pushmi::submit(
          (In &&) in,
          transform_on<
              F,
              Cardinality,
              property_query_v<properties_t<In>, is_flow<>>>{f_}((Out &&) out));
    }
  };

  template <class F>
  struct adapt_impl {
    F f_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<std::decay_t<In>>) //
        auto
        operator()(In&& in) const {
      // copy 'f_' to allow multiple calls to connect to multiple 'in'
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<F, In&&>{f_});
    }
  };

 public:
  template <class... FN>
  auto operator()(FN... fn) const {
    auto f = ::folly::pushmi::overload(std::move(fn)...);
    using F = decltype(f);
    return adapt_impl<F>{std::move(f)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::transform_fn transform{};
} // namespace operators

} // namespace pushmi
} // namespace folly
