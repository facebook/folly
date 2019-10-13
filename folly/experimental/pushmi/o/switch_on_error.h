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

#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/piping.h>

namespace folly {
namespace pushmi {

namespace detail {

struct switch_on_error_fn {
 private:
  template <class ErrorSelector>
  struct on_error_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE(class Out, class E)
    (requires Receiver<Out>&& Invocable<ErrorSelector&, E>&&
         SenderTo<::folly::pushmi::invoke_result_t<ErrorSelector&, E>, Out>)
    void operator()(Out& out, E&& e) noexcept {
      static_assert(
          ::folly::pushmi::NothrowInvocable<ErrorSelector&, E>,
          "switch_on_error - error selector function must be noexcept");
      auto next = es_((E &&) e);
      submit(std::move(next), std::move(out));
    }
  };
  template <class In, class ErrorSelector>
  struct out_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<std::decay_t<Out>>)
    auto operator()(SIn&& in, Out&& out) const {
      submit((In&&)in, ::folly::pushmi::detail::receiver_from_fn<In>()(
          (Out&&)out,
          // copy 'es' to allow multiple calls to submit
          ::folly::pushmi::on_error(on_error_impl<ErrorSelector>{es_})));
    }
  };
  template <class ErrorSelector>
  struct in_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>)
    auto operator()(In&& in) {
      return ::folly::pushmi::detail::sender_from(
          (In&&)in,
          out_impl<In, ErrorSelector>{std::move(es_)});
    }
  };

 public:
  PUSHMI_TEMPLATE(class ErrorSelector)
  (requires SemiMovable<ErrorSelector>)
  auto operator()(ErrorSelector es) const {
    return in_impl<ErrorSelector>{std::move(es)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::switch_on_error_fn switch_on_error{};
} // namespace operators

} // namespace pushmi
} // namespace folly
