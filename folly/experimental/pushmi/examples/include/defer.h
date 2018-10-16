// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <pushmi/single.h>
#include <pushmi/o/submit.h>
#include <pushmi/o/extension_operators.h>

namespace pushmi {

namespace operators {

PUSHMI_TEMPLATE(class F)
  (requires Invocable<F>)
auto defer(F f) {
  return make_single_deferred(
    constrain(lazy::Receiver<_1>, 
      [f = std::move(f)](auto out) mutable {
        auto sender = f();
        PUSHMI_IF_CONSTEXPR( ((bool)TimeSender<decltype(sender)>) (
          ::pushmi::submit(sender, ::pushmi::now(id(sender)), std::move(out));
        ) else (
          ::pushmi::submit(sender, std::move(out));
        ));
      }
    )
  );
}

} // namespace operators

} // namespace pushmi
