// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../single_deferred.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct just_fn {
private:
  template <class V>
  struct impl {
    V v_;
    PUSHMI_TEMPLATE (class Out)
      (requires SingleReceiver<Out, V>)
    void operator()(Out out) {
      ::pushmi::set_value(out, std::move(v_));
    }
  };
public:
  PUSHMI_TEMPLATE(class V)
    (requires SemiMovable<V>)
  auto operator()(V v) const {
    return make_single_deferred(impl<V>{std::move(v)});
  }
} just {};
} // namespace operators

} // namespace pushmi
