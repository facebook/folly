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
#include <folly/experimental/pushmi/o/via.h>

namespace pushmi {

template<typename In>
struct send_via {
    In in;
    PUSHMI_TEMPLATE(class... AN)
      (requires Invocable<decltype(::pushmi::operators::via), AN...> &&
      Invocable<invoke_result_t<decltype(::pushmi::operators::via), AN...>, In>)
    auto via(AN&&... an) {
        return in | ::pushmi::operators::via((AN&&) an...);
    }
};

namespace detail {

struct request_via_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return send_via<In>{in};
    }
  };
public:
  inline auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::request_via_fn request_via{};

} // namespace operators

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>> && Sender<In>)
auto via_cast(In in) {
  return in;
}

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>>)
auto via_cast(send_via<In> ss) {
  return ss.in;
}

} // namespace pushmi
