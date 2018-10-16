#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct switch_on_error_fn {
  PUSHMI_TEMPLATE(class ErrorSelector)
    (requires SemiMovable<ErrorSelector>)
  auto operator()(ErrorSelector es) const {
    return constrain(lazy::Sender<_1>, [es = std::move(es)](auto in) {
      using In = decltype(in);
      return ::pushmi::detail::deferred_from<In, single<>>(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          constrain(lazy::Receiver<_1>, [es](auto out) {
            using Out = decltype(out);
            return ::pushmi::detail::out_from_fn<In>()(
              std::move(out),
              // copy 'es' to allow multiple calls to submit
              ::pushmi::on_error([es](auto& out, auto&& e) noexcept {
                static_assert(::pushmi::NothrowInvocable<ErrorSelector, decltype(e)>,
                  "switch_on_error - error selector function must be noexcept");
                auto next = es(std::move(e));
                ::pushmi::submit(next, std::move(out));
              })
            );
          })
        )
      );
    });
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::switch_on_error_fn switch_on_error{};
} // namespace operators

} // namespace pushmi
