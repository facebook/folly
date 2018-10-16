// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../single.h"
#include "submit.h"
#include "extension_operators.h"
#include "via.h"

namespace pushmi {

template<typename In>
struct send_via {
    In in;
    template<class... AN>
    auto via(AN&&... an) {
        return in | ::pushmi::operators::via((AN&&) an...);
    }
};

namespace detail {

struct request_via_fn {
  inline auto operator()() const;
};

inline auto request_via_fn::operator()() const {
  return constrain(lazy::Sender<_1>, [](auto in) {
    using In = decltype(in);
    return send_via<In>{in};
  });
}

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::request_via_fn request_via{};

} // namespace operators

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>> && Sender<_1>)
auto via_cast(In in) {
  return in;
}

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>>)
auto via_cast(send_via<In> ss) {
  return ss.in;
}

} // namespace pushmi
