#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <pushmi/single_sender.h>

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct bulk_fn {
  template<class F, class ShapeBegin, class ShapeEnd, class Target, class IF, class RS>
  auto operator()(
      F&& func,
      ShapeBegin sb,
      ShapeEnd se,
      Target&& driver,
      IF&& initFunc,
      RS&& selector) const {
    return [func, sb, se, driver, initFunc, selector](auto in){
      return make_single_sender(
        [in, func, sb, se, driver, initFunc, selector](auto out) mutable {
          submit(in, make_receiver(std::move(out),
              [func, sb, se, driver, initFunc, selector](auto& out, auto input) {
                driver(initFunc, selector, std::move(input), func, sb, se, std::move(out));
              }
          ));
        });
    };
  }
} bulk {};

} // namespace operators

} // namespace pushmi
