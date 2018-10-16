// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../single.h"
#include "../single_deferred.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct defer_fn {
private:
  template <class F>
  struct impl {
    F f_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) {
      auto sender = f_();
      PUSHMI_IF_CONSTEXPR( ((bool)TimeSender<decltype(sender)>) (
        ::pushmi::submit(sender, ::pushmi::now(id(sender)), std::move(out));
      ) else (
        ::pushmi::submit(sender, std::move(out));
      ));
    }
  };
public:
  PUSHMI_TEMPLATE(class F)
    (requires Invocable<F&>)
  auto operator()(F f) const {
    return make_single_deferred(impl<F>{std::move(f)});
  }
} defer {};

} // namespace operators

} // namespace pushmi
