#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

template <class In, class Operator>
auto operator|(In&& in, Operator op) -> decltype(op(std::forward<In>(in))) {
  return op(std::forward<In>(in));
}

namespace pushmi {

template<class T, class... FN>
auto pipe(T t, FN... fn) {
  return (t | ... | fn);
}

} // namespace pushmi
