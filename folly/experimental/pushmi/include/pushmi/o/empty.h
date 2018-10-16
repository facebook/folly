// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../deferred.h"
#include "../single_deferred.h"

namespace pushmi {

namespace operators {

template <class V>
auto empty() {
  return single_deferred{
      []<class Out>(Out out) mutable PUSHMI_VOID_LAMBDA_REQUIRES(
          SingleReceiver<Out, V>){
          ::pushmi::set_done(out);
  }};
}

inline auto empty() {
  return deferred{[]<class Out>(Out out) mutable PUSHMI_VOID_LAMBDA_REQUIRES(
      NoneReceiver<Out>){
      ::pushmi::set_done(out);
  }};
}

} // namespace operators

} // namespace pushmi
