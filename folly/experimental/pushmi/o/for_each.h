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
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/Function.h>

namespace folly {
namespace pushmi {
namespace detail {

struct for_each_fn {
 private:
  template<class Up>
  struct request_fn {
    Up up_;
    explicit request_fn(Up up) : up_(std::move(up)) {}
    request_fn(request_fn&& o) : up_(std::move(o.up_)) {}
    void operator()(std::ptrdiff_t requested) {
      ::folly::pushmi::set_value(up_, requested);
    }
  };
  template <class Out>
  struct Pull {
    Out out_;
    explicit Pull(Out out) : out_(std::move(out)) {}
    using receiver_category = flow_receiver_tag;
    folly::Function<void(std::ptrdiff_t)> pull;
    template <class... VN>
    void value(VN&&... vn) {
      ::folly::pushmi::set_value(out_, (VN &&) vn...);
      pull(1);
    }
    template <class E>
    void error(E&& e) noexcept {
      // break circular reference
      pull = nullptr;
      ::folly::pushmi::set_error(out_, (E &&) e);
    }
    void done() {
      // break circular reference
      pull = nullptr;
      ::folly::pushmi::set_done(out_);
    }
    PUSHMI_TEMPLATE(class Up)
    (requires ReceiveValue<Up, std::ptrdiff_t>)
    void starting(Up up) {
      pull = request_fn<Up>{std::move(up)};
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
    (requires ReceiveValue<Up> && not ReceiveValue<Up, std::ptrdiff_t>)
    void starting(Up) {}
  };
  template <class... AN>
  struct adapt_impl {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
    (requires FlowSender<In> && Constructible<std::tuple<AN...>, const std::tuple<AN...>&>)
    void operator()(In&& in) & {
      // Strip flow:
      using C =
        std::conditional_t<SingleSender<In>, single_sender_tag, sender_tag>;
      //  copy args to allow adapt to be called multiple times
      auto out{receiver_from_fn<C>()(args_)};
      using Out = decltype(out);
      ::folly::pushmi::submit(
          (In &&) in,
          receiver_from_fn<In>()(Pull<Out>{std::move(out)}));
    }
    PUSHMI_TEMPLATE(class In)
    (requires FlowSender<In> && Constructible<std::tuple<AN...>, std::tuple<AN...>&&>)
    void operator()(In&& in) && {
      // Strip flow:
      using C =
        std::conditional_t<SingleSender<In>, single_sender_tag, sender_tag>;
      auto out{receiver_from_fn<C>()(std::move(args_))};
      using Out = decltype(out);
      ::folly::pushmi::submit(
          (In &&) in,
          receiver_from_fn<In>()(Pull<Out>{std::move(out)}));
    }
  };

 public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return for_each_fn::adapt_impl<AN...>{std::tuple<AN...>{(AN &&) an...}};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::for_each_fn for_each{};
} // namespace operators

} // namespace pushmi
} // namespace folly
