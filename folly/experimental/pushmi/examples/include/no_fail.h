#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <pushmi/single_deferred.h>
#include <pushmi/o/submit.h>

namespace pushmi {

namespace detail {

struct no_fail_fn {
  auto operator()() const {
    return constrain(lazy::Sender<_1>, [](auto in) {
      using In = decltype(in);
      return ::pushmi::detail::deferred_from<In, single<>>(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          constrain(lazy::Receiver<_1>, [](auto out) {
            using Out = decltype(out);
            return ::pushmi::detail::out_from_fn<In>()(
              std::move(out),
              ::pushmi::on_error([](auto&, auto&&) noexcept {
                std::abort();
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
PUSHMI_INLINE_VAR constexpr detail::no_fail_fn no_fail{};
} // namespace operators

} // namespace pushmi
