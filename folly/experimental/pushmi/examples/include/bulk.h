#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <pushmi/single_deferred.h>

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) {__VA_ARGS__}
#else
#define MAKE(x) make_ ## x
#endif

namespace pushmi {

namespace operators {

template<class F, class ShapeBegin, class ShapeEnd, class Target, class IF, class RS>
auto bulk(
    F&& func,
    ShapeBegin sb,
    ShapeEnd se,
    Target&& driver,
    IF&& initFunc,
    RS&& selector) {
      return [func, sb, se, driver, initFunc, selector](auto in){
          return MAKE(single_deferred)([in, func, sb, se, driver, initFunc, selector](auto out) mutable {
              submit(in, MAKE(single)(std::move(out),
                  [func, sb, se, driver, initFunc, selector](auto& out, auto input){
                      driver(initFunc, selector, std::move(input), func, sb, se, std::move(out));
                  }
              ));
          });
      };
  }

} // namespace operators

} // namespace pushmi
