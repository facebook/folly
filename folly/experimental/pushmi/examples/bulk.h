/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/experimental/pushmi/sender/single_sender.h>

namespace folly {
namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct bulk_fn {
  template <
      class F,
      class ShapeBegin,
      class ShapeEnd,
      class Target,
      class IF,
      class RS>
  auto operator()(
      F&& func,
      ShapeBegin sb,
      ShapeEnd se,
      Target&& driver,
      IF&& initFunc,
      RS&& selector) const {
    return [func, sb, se, driver, initFunc, selector](auto in) mutable {
      return make_single_sender(
          [in, func, sb, se, driver, initFunc, selector](auto out) mutable {
            using Out = decltype(out);
            struct data : Out {
              data(Out out) : Out(std::move(out)) {}
              bool empty = true;
            };
            submit(
                in,
                make_receiver(
                    data{std::move(out)},
                    [func, sb, se, driver, initFunc, selector](
                        auto& out_, auto input) mutable noexcept {
                      out_.empty = false;
                      driver(
                          initFunc,
                          selector,
                          std::move(input),
                          func,
                          sb,
                          se,
                          std::move(static_cast<Out&>(out_)));
                    },
                    // forward to output
                    [](auto o, auto e) noexcept {set_error(o, e);},
                    // only pass done through when empty
                    [](auto o){ if (o.empty) { set_done(o); }}));
          });
    };
  }
} bulk{};

} // namespace operators

} // namespace pushmi
} // namespace folly
