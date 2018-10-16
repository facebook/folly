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

PUSHMI_TEMPLATE(class V)
  (requires SemiMovable<V>)
auto just(V v) {
  return make_single_deferred(
    constrain(lazy::SingleReceiver<_1, V>,
      [v = std::move(v)](auto out) mutable {
        ::pushmi::set_value(out, std::move(v));
      }
    )
  );
}

} // namespace operators

} // namespace pushmi
