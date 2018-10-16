#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct filter_fn {
  PUSHMI_TEMPLATE(class Predicate)
    (requires SemiMovable<Predicate>)
  auto operator()(Predicate p) const {
    return constrain(lazy::Sender<_1>, [p = std::move(p)](auto in) {
      using In = decltype(in);
      return ::pushmi::detail::deferred_from<In, single<>>(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          constrain(lazy::Receiver<_1>, [p](auto out) {
            using Out = decltype(out);
            return ::pushmi::detail::out_from_fn<In>()(
              std::move(out),
              // copy 'p' to allow multiple calls to submit
              ::pushmi::on_value([p](auto& out, auto&& v) {
                if (p(as_const(v))) {
                  ::pushmi::set_value(out, std::move(v));
                } else {
                  ::pushmi::set_done(out);
                }
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
PUSHMI_INLINE_VAR constexpr detail::filter_fn filter{};
} // namespace operators

} // namespace pushmi
