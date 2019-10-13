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

#include <exception>
#include <utility>

#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/sender/concepts.h>
#include <folly/experimental/pushmi/sender/primitives.h>

namespace folly {
namespace pushmi {

struct ignoreSF {
  void operator()(detail::any) {}
  void operator()(detail::any, detail::any) {}
};

struct passDSF {
  PUSHMI_TEMPLATE(class Data, class Out)
  (requires SenderTo<Data&, Out>) //
  void operator()(Data& in, Out out) {
    submit(in, std::move(out));
  }
  // TODO Constrain me, please!
  template <class Data, class TP, class Out>
  void operator()(Data& in, TP at, Out out) {
    submit(in, std::move(at), std::move(out));
  }
};

template <class... Fns>
struct on_submit_fn : overload_fn<Fns...> {
  constexpr on_submit_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_submit(Fns... fns) -> on_submit_fn<Fns...> {
  return on_submit_fn<Fns...>{std::move(fns)...};
}

} // namespace pushmi
} // namespace folly
