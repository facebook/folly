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
#include <cassert>

namespace folly {
namespace pushmi {
namespace detail {

PUSHMI_TEMPLATE(class SideEffects, class Out)
(requires Receiver<SideEffects>&& Receiver<Out>) //
struct tap_ {
  SideEffects sideEffects;
  Out out;

  // side effect has no effect on the properties.
  using receiver_category = receiver_category_t<Out>;

  PUSHMI_TEMPLATE(class... VN)
  (requires ReceiveValue<SideEffects&, const std::remove_reference_t<VN>&...>&&
       ReceiveValue<Out&, VN...>) //
  void value(VN&&... vn) {
    set_value(sideEffects, folly::as_const(vn)...);
    set_value(out, (VN &&) vn...);
  }
  PUSHMI_TEMPLATE(class E)
  (requires ReceiveError<SideEffects&, const std::remove_reference_t<E>&>&&
       ReceiveError<Out&, E>) //
   void error(E&& e) noexcept {
    set_error(sideEffects, folly::as_const(e));
    set_error(out, (E&&) e);
  }
  void done() {
    set_done(sideEffects);
    set_done(out);
  }
  PUSHMI_TEMPLATE(class Up)
  (requires FlowReceiver<SideEffects>&& FlowReceiver<Out>) //
  void starting(Up&& up) {
    // up is not made const because sideEffects is allowed to call methods on up
    set_starting(sideEffects, up);
    set_starting(out, (Up &&) up);
  }
};

PUSHMI_INLINE_VAR constexpr struct make_tap_fn {
  PUSHMI_TEMPLATE(class SideEffects, class Out)
  (requires Receiver<std::decay_t<SideEffects>>&& Receiver<std::decay_t<Out>>&&
       Receiver<tap_<std::decay_t<SideEffects>, std::decay_t<Out>>>) //
  auto operator()(const SideEffects& se, Out&& out) const {
    return tap_<std::decay_t<SideEffects>, std::decay_t<Out>>{se, (Out &&) out};
  }
} const make_tap{};

struct tap_fn {
 private:
  struct impl_fn {
    PUSHMI_TEMPLATE(class In, class SideEffects)
    (requires SenderTo<In, SideEffects>) //
    auto operator()(In&& in, SideEffects&& sideEffects) {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in,
          submit_impl<In, std::decay_t<SideEffects>>{(SideEffects &&)
                                                       sideEffects});
    }
  };

  template <class... AN>
  struct adapt_impl {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
    auto operator()(In&& in) {
      return tap_fn::impl_fn{}(
          (In &&) in,
          receiver_from_fn<In>()(std::move(args_)));
    }
  };

  PUSHMI_TEMPLATE(class In, class SideEffects)
  (requires Sender<In> && Receiver<SideEffects>) //
  struct submit_impl {
    SideEffects sideEffects_;
    template <class Out>
    using tap_t = decltype(detail::make_tap(
        std::declval<const SideEffects&>(),
        std::declval<Out>()));
    template <class Out>
    using receiver_t =
        invoke_result_t<receiver_from_fn<std::decay_t<In>>, tap_t<Out>>;
    PUSHMI_TEMPLATE(class Data, class Out)
    (requires Receiver<std::decay_t<Out>> &&
      SenderTo<In, Out>&&
      SenderTo<In, receiver_t<Out>>) //
    auto operator()(Data&& in, Out&& out) const {
      auto gang{receiver_from_fn<std::decay_t<In>>()(
          detail::make_tap(sideEffects_, (Out &&) out))};
      submit((In &&) in, std::move(gang));
    }
  };

 public:
  template <class... AN>
  auto operator()(AN... an) const {
    return adapt_impl<AN...>{std::tuple<AN...>{std::move(an)...}};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::tap_fn tap{};
} // namespace operators

} // namespace pushmi
} // namespace folly
