// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <folly/experimental/pushmi/receiver.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/extension_operators.h>

#include <folly/experimental/pushmi/subject.h>

namespace pushmi {

namespace detail {

template<class... TN>
struct share_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      subject<properties_t<In>, TN...> sub;
      ::pushmi::submit(in, sub.receiver());
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

template<class... TN>
PUSHMI_INLINE_VAR constexpr detail::share_fn<TN...> share{};

} // namespace operators

} // namespace pushmi
