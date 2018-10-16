// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../deferred.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_TEMPLATE(class E)
  (requires SemiMovable<E>)
auto error(E e) {
  return make_deferred(
    constrain(lazy::NoneReceiver<_1, E>,
      [e = std::move(e)](auto out) mutable {
        ::pushmi::set_error(out, std::move(e));
      }
    )
  );
}

PUSHMI_TEMPLATE(class V, class E)
  (requires SemiMovable<V> && SemiMovable<E>)
auto error(E e) {
  return make_single_deferred(
    constrain(lazy::SingleReceiver<_1, V, E>,
      [e = std::move(e)](auto out) mutable {
        ::pushmi::set_error(out, std::move(e));
      }
    )
  );
}

} // namespace operators

} // namespace pushmi
