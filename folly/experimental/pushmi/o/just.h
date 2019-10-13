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

PUSHMI_INLINE_VAR constexpr struct just_fn {
 private:
  template <class... VN>
  struct task : single_sender_tag::with_values<VN...>::no_error {
  private:
    std::tuple<VN...> vn_;
  public:
    using properties = property_set<is_always_blocking<>>;

    task() = default;
    explicit task(VN&&... vn) : vn_{(VN&&) vn...} {}

    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<Out&, VN...>) //
    void submit(Out&& out) && {
      ::folly::pushmi::apply(
        [&out](VN&&... vn) { set_value(out, (VN&&) vn...); },
        std::move(vn_)
      );
      set_done(out);
    }

    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<Out&, VN&...>) //
    void submit(Out&& out) & {
      ::folly::pushmi::apply(
        [&out](VN&... vn) { set_value(out, vn...); },
        vn_
      );
      set_done(out);
    }
  };

 public:
  PUSHMI_TEMPLATE(class... VN)
  (requires And<SemiMovable<VN>...>) //
  auto operator()(VN... vn) const {
    return task<VN...>{std::move(vn)...};
  }
} just{};
} // namespace operators

} // namespace pushmi
} // namespace folly
