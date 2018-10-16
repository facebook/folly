#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "traits.h"

template <class In, pushmi::Invocable<In> Op>
decltype(auto) operator|(In&& in, Op op) {
  return op((In&&) in);
}

namespace pushmi {

template<class T, class... FN>
auto pipe(T t, FN... fn) -> decltype((t | ... | fn)) {
  return (t | ... | fn);
}

} // namespace pushmi
