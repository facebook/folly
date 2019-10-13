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

#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/sender/properties.h>

namespace folly {
namespace pushmi {
namespace operators {
PUSHMI_INLINE_VAR constexpr struct empty_fn {
private:
  struct task : pipeorigin, single_sender_tag::with_values<> {
    using properties = property_set<is_always_blocking<>>;

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out&& out) {
      set_done(out);
    }
  };
public:
  auto operator()() const {
    return task{};
  }
} empty {};

} // namespace operators
} // namespace pushmi
} // namespace folly
