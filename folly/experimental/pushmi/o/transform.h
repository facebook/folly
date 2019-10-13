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

#include <folly/experimental/pushmi/receiver/flow_receiver.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/receiver/receiver.h>

namespace folly {
namespace pushmi {

namespace detail {

template <class F, class SenderCategory>
struct transform_on;

template <class F>
struct value_fn {
  F f_;
  value_fn() = default;
  constexpr explicit value_fn(F f) : f_(std::move(f)) {}
  template<class Out, class... VN>
  auto operator()(Out& out, VN&&... vn) {
    using Result = ::folly::pushmi::invoke_result_t<F&, VN...>;
    static_assert(
        ::folly::pushmi::SemiMovable<Result>,
        "none of the functions supplied to transform can convert this value");
    static_assert(
        ::folly::pushmi::ReceiveValue<Out&, Result>,
        "Result of value transform cannot be delivered to Out");
    set_value(out, ::folly::pushmi::invoke(f_, (VN &&) vn...));
  }
};

struct transform_fn {
 private:
  template <class F, class In>
  struct submit_impl {
    F f_;
    using maker_t = receiver_from_fn<std::decay_t<In>>;
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out> && Constructible<F, const F&> && SenderTo<In, invoke_result_t<maker_t, Out, value_fn<F>>>) //
    void operator()(SIn&& in, Out&& out) & {
      // copy 'f_' to allow multiple calls to connect to multiple 'in'
      ::folly::pushmi::submit(
          (In &&) in,
          maker_t{}((Out &&) out, value_fn<F>{f_}));
    }
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out> && MoveConstructible<F> && SenderTo<In, invoke_result_t<maker_t, Out, value_fn<F>>>) //
    void operator()(SIn&& in, Out&& out) && {
      ::folly::pushmi::submit(
          (In &&) in,
          maker_t{}((Out &&) out, value_fn<F>{std::move(f_)}));
    }
  };

  template <class F>
  struct adapt_impl {
    F f_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
    auto operator()(In&& in) & {
      // copy 'f_' to allow multiple calls to connect to multiple 'in'
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<F, In>{f_});
    }
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
        auto
        operator()(In&& in) && {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<F, In>{std::move(f_)});
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
