// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../single.h"

namespace pushmi {

namespace operators {

template <class V>
auto just(V v) {
  return single_deferred{[v = std::move(v)]<class Out>(
      Out out) mutable PUSHMI_VOID_LAMBDA_REQUIRES(SingleReceiver<Out, V>){
      ::pushmi::set_value(out, std::move(v));
  }};
}

} // namespace operators

} // namespace pushmi
