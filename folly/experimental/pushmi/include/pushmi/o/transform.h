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

namespace pushmi {

namespace detail {


// extracted this to workaround cuda compiler failure to compute the static_asserts in the nested lambda context
template<class F>
struct transform_on_value {
  F f_;
  template<class Out, class V>
  auto operator()(Out& out, V&& v) {
    using Result = decltype(f_((V&&) v));
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::SingleReceiver<Out, Result>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_value(out, f_((V&&) v));
  }
};

struct transform_fn {
  template <class... FN>
  auto operator()(FN... fn) const;
};

template <class... FN>
auto transform_fn::operator()(FN... fn) const {
  auto f = ::pushmi::overload(std::move(fn)...);
  using F = decltype(f);
  return ::pushmi::constrain(::pushmi::lazy::Sender<::pushmi::_1>, [f = std::move(f)](auto in) {
    using In = decltype(in);
    // copy 'f' to allow multiple calls to connect to multiple 'in'
    return ::pushmi::detail::deferred_from<In, ::pushmi::single<>>(
      std::move(in),
      ::pushmi::detail::submit_transform_out<In>(
        ::pushmi::constrain(::pushmi::lazy::Receiver<::pushmi::_1>, [f](auto out) {
          using Out = decltype(out);
          return ::pushmi::detail::out_from_fn<In>()(
            std::move(out),
            // copy 'f' to allow multiple calls to submit
            ::pushmi::on_value(
              transform_on_value<F>{f}
              // [f](Out& out, auto&& v) {
              //   using V = decltype(v);
              //   using Result = decltype(f((V&&) v));
              //   static_assert(::pushmi::SemiMovable<Result>,
              //     "none of the functions supplied to transform can convert this value");
              //   static_assert(::pushmi::SingleReceiver<Out, Result>,
              //     "Result of value transform cannot be delivered to Out");
              //   ::pushmi::set_value(out, f((V&&) v));
              // }
            )
          );
        })
      )
    );
  });
}

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::transform_fn transform{};
} // namespace operators

} // namespace pushmi
