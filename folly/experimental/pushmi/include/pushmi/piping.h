#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "traits.h"

namespace pushmi {

PUSHMI_TEMPLATE (class In, class Op)
  (requires lazy::Sender<std::decay_t<In>> && lazy::Invocable<Op&, In>)
decltype(auto) operator|(In&& in, Op op) {
  return op((In&&) in);
}

PUSHMI_INLINE_VAR constexpr struct pipe_fn {
#if __cpp_fold_expressions >= 201603
  template<class T, class... FN>
  auto operator()(T t, FN... fn) const -> decltype((t | ... | fn)) {
    return (t | ... | fn);
  }
#else
  template<class T, class F>
  auto operator()(T t, F f) const -> decltype(t | f) {
    return t | f;
  }
  template<class T, class F, class... FN, class This = pipe_fn>
  auto operator()(T t, F f, FN... fn) const -> decltype(This()((t | f), fn...)) {
    return This()((t | f), fn...);
  }
#endif
} const pipe {};

} // namespace pushmi
