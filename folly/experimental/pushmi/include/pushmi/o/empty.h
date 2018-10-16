// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <meta/meta.hpp>

#include "../deferred.h"
#include "../single_deferred.h"
#include "../detail/functional.h"

namespace pushmi {

namespace operators {

template <class V>
auto empty() {
  return make_single_deferred(
    constrain(lazy::SingleReceiver<_1, V>, [](auto out) mutable {
      ::pushmi::set_done(out);
    })
  );
}

inline auto empty() {
  return make_deferred(
    constrain(lazy::NoneReceiver<_1>, [](auto out) mutable {
      ::pushmi::set_done(out);
    })
  );
}

} // namespace operators
} // namespace pushmi
