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

namespace operators {

namespace detail {

struct transform_fn {
template <class... FN>
auto operator()(FN... fn) const;
};
template <class... FN>
auto transform_fn::operator()(FN... fn) const {
  auto f = overload{std::move(fn)...};
  return [f = std::move(f)]<class In>(In in) {
    // copy 'f' to allow multiple calls to connect to multiple 'in'
    return ::pushmi::detail::deferred_from<In, archetype_single>(
      std::move(in),
      ::pushmi::detail::submit_transform_out<In>(
        [f]<class Out>(Out out) {
          return ::pushmi::detail::out_from<In>(
            std::move(out),
            // copy 'f' to allow multiple calls to submit
            on_value{
              [f]<class V>(Out& out, V&& v) {
                using Result = decltype(f((V&&) v));
                static_assert(SemiMovable<Result>,
                  "none of the functions supplied to transform can convert this value");
                static_assert(SingleReceiver<Out, Result>,
                  "Result of value transform cannot be delivered to Out");
                ::pushmi::set_value(out, f((V&&) v));
              }
            }
          );
        }
      )
    );
  };
}

} // namespace detail

inline constexpr detail::transform_fn transform{};

} // namespace operators

} // namespace pushmi
