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

#include <folly/experimental/pushmi/executor.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/piping.h>

namespace folly {
namespace pushmi {

namespace detail {

struct on_fn {
 private:
  template <class In, class Out>
  struct on_value_impl {
    In in_;
    Out out_;
    void operator()(any) {
      submit(in_, std::move(out_));
    }
  };
  template <class Factory>
  struct submit_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class In, class Out)
    (requires SenderTo<In, Out>) //
        void
        operator()(In&& in, Out out) const {
      auto exec = ::folly::pushmi::make_strand(ef_);
      submit(
          ::folly::pushmi::schedule(exec),
          ::folly::pushmi::make_receiver(on_value_impl<std::decay_t<In>, Out>{
              (In&&) in, std::move(out)}));
    }
  };
  template <class Factory>
  struct adapt_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<std::decay_t<In>>) //
        auto
        operator()(In&& in) const {
      return ::folly::pushmi::detail::sender_from(
          (In&&) in, submit_impl<Factory>{ef_});
    }
  };

 public:
  PUSHMI_TEMPLATE(class Factory)
  (requires StrandFactory<Factory>) //
      auto
      operator()(Factory ef) const {
    return adapt_impl<Factory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::on_fn on{};

} // namespace operators

} // namespace pushmi
} // namespace folly
