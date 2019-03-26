/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>

namespace folly {
namespace pushmi {
namespace detail {
struct single_empty_impl : pipeorigin {
  using properties = property_set<
      is_sender<>,
      is_single<>,
      is_always_blocking<>>;

  PUSHMI_TEMPLATE(class Out)
  (requires Receiver<Out>&& Invocable<decltype(set_done)&, Out&>) //
      void submit(Out&& out) {
    set_done(out);
  }
};
} // namespace detail

namespace operators {
inline detail::single_empty_impl empty() {
  return {};
}

} // namespace operators
} // namespace pushmi
} // namespace folly
