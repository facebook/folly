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

#include "../subject.h"

namespace pushmi {

namespace detail {

template<class T>
struct share_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      subject<T, properties_t<In>> sub;
      PUSHMI_IF_CONSTEXPR( ((bool)TimeSender<In>) (
        ::pushmi::submit(in, ::pushmi::now(id(in)), sub.receiver());
      ) else (
        ::pushmi::submit(id(in), sub.receiver());
      ));
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

template<class T>
PUSHMI_INLINE_VAR constexpr detail::share_fn<T> share{};

} // namespace operators

} // namespace pushmi
