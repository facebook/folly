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

#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/sender/properties.h>

namespace folly {
namespace pushmi {
namespace operators {

PUSHMI_INLINE_VAR constexpr struct error_fn {
private:
  template <class E>
  struct task : single_sender_tag::with_values<>::with_error<E> {
  private:
    E e_;
  public:
    using properties = property_set<is_always_blocking<>>;
    task() = default;
    explicit task(E e) : e_(std::move(e)) {}

    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveError<Out, E>)
    void submit(Out&& out) && {
      set_error(out, std::move(e_));
    }
  };

public:
  PUSHMI_TEMPLATE(class E)
  (requires SemiMovable<E>)
  auto operator()(E e) const {
    return task<E>{std::move(e)};
  }
} error {};

} // namespace operators
} // namespace pushmi
} // namespace folly
