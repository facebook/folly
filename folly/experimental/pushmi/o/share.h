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
#include <folly/experimental/pushmi/receiver/receiver.h>

#include <folly/experimental/pushmi/subject/subject.h>

namespace folly {
namespace pushmi {

namespace detail {

template <class... TN>
struct share_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>)
    auto operator()(In in) const {
      subject<properties_t<In>, TN...> sub;
      submit(in, sub.receiver());
      return sub;
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace operators {

template <class... TN>
PUSHMI_INLINE_VAR constexpr detail::share_fn<TN...> share{};

} // namespace operators

} // namespace pushmi
} // namespace folly
